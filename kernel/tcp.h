#ifndef __TCP_H__
#define __TCP_H__

#define TCP_MIN_DATA_OFF 5 // tcp数据偏移字段最小几个4字节,也就是tcp头部长度最小几个4字节;tcp头部最小20字节,data offset占4个比特位,故tcp头部长度为[20,60]
#define TCP_DEFAULT_WINDOW 40960 // 滑动窗口的最大值
#define TCP_DEFALUT_MSS 536 // mss值,mss指的是tcp报文的数据部分的最大字节数

/*
 * listen函数中有个backlog,内核为任何一个给定的监听套接字维护两个队列,由于端口上会有很多客户端的连接到来,所以这两个队列中可能会有很多元素
 *   一是未完成连接队列,每个这样的SYN分节对应其中一项: 已由某个客户发出并到达服务器,而服务器正在等待完成相应的TCP三路握手过程,这些套接字术语SYN_RCVD状态
 *   二是已完成连接队列,每个已完成TCP三路握手过程的客户对应其中一项,这些套接字处于ESTABLISHED状态。
 * backlog参数就是上面两个队列总和的最大值,TCP_MAX_BACKLOG就是默认的backlog最大值
 */
#define TCP_MAX_BACKLOG 128


extern struct spinlock tcpsocks_list_lk; //  tcp socks列表锁
extern struct linked_list tcpsocks_list_head; // tcp socks列表头节点

// tcp结构体
struct tcp_header {
  uint16 sport; // 源端口
  uint16 dport; // 目的端口
  uint seq;     // 序号
  uint acknum;  // 确认号
  uint reserved:4, // 保留位的前4位
       doff:4;     // 数据偏移
  uint fin:1,
       syn:1,
       rst:1,
       psh:1,
       ack:1,
       urg:1,
       ece:1,
       cwr:1; // ece和cwr是保留位的后两位,且cwr的位数比ece高
  uint16 winsize; // 窗口大小
  uint16 checksum; // 校验和
  uint16 urgptr; // 紧急指针,如果urg标志被设置,那么这个指针就有值
};

/* tcp状态 */
enum tcpstate {
  TCP_CLOSED,
	TCP_LISTEN,
	TCP_SYN_RECV,
	TCP_SYN_SENT,
	TCP_ESTABLISHED,
	TCP_CLOSE_WAIT,
	TCP_LAST_ACK,
	TCP_FIN_WAIT1,
	TCP_FIN_WAIT2,
	TCP_CLOSING,
	TCP_TIME_WAIT,
	TCP_MAX_STATE
};

// tcp头部固定部分的字节数
#define TCP_HDR_LEN sizeof(struct tcp_header)
// tcp头部中doff,也就是头部的数据偏移,也就是tcp总头部(固定部分+变长部分)占多少个4字节,由于只支持固定部分,所以就是5
#define TCP_DOFFSET sizeof(struct tcp_header) / 4

// 设置标志位
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_MSL 100 // 10 sec
#define TCP_TIMEWAIT_TIMEOUT (2 * TCP_MSL) // time_wait状态等待的时间

/*
 * 传输控制块TCB
 * TCB是一个包含了TCP连接的所有会话状态的资源块，用于在不同设备之间传输数据时由于有设备差异，因此采用TCB进行封装数据。
 * 在发起TCP连接请求前，客户端会主动创建TCB，而服务器会被动建立TCB，从而才能合理获取到请求信息的内容
 */
struct tcb {
  /* 
   * 发送序列
   * 这里要注意的是:
   * snd_una之前的报文是发送了且经过确认的
   * [snd_una, snd_nxt)是已经发送但未收到确认的
   * [snd_nxt, snd_nxt + snd_wnd)是接下来可用的序列号
   * 大于等于snd_nxt + snd_wnd是暂时不能使用的序列号,后面随着发送窗口的滑动可能会用到
   */
  uint32 snd_una;  // 发送但未确认的序列号集合的开始序列号
  uint32 snd_nxt;  // 下一个要发送的序列号
  uint32 snd_wnd;  // 发送窗口大小
  uint32 snd_up;   // 要发送的紧急指针的地址
  uint32 snd_wl1;  // 上一次用于窗口大小更新的tcp报文段的序列号,即某一方认为自己接受窗口东西太多或太少,在发送的报文里面加入要对方发送窗口变化的信息,将这个报文的序号记录在此
  uint32 snd_wl2;  // 上一次用于窗口大小更新的tcp确认报文段的序列号,即确认对方发送过来的含有窗口变化请求的报文的确认号
  uint32 iss;      // 初始的发送序列号

  /* 
   * 接受序列
   * 这里要注意的是:
   * rcv_nxt之前的是已接收且发送了对应的ACK
   * [rcv_nxt, rcv_nxt + rcv_wnd)是期待接收到的数据的序列号
   * 大于rcv_nxt + rcv_wnd是暂时不能使用的序列号,后面随着接收窗口的滑动可能会用到
   */
  uint32 rcv_nxt;  // 期望能接收到的序列号集合的初始序列号
  uint32 rcv_wnd;  // 接收窗口的大小
  uint32 rcv_up;   // 接收的紧急指针的地址
  uint32 irs;      // 初始的接收序列号
};

/*
 * tcp连接的socket信息
 * 所有的tcp连接(已经建立了连接、未建立连接、正在监听端口并等待对方来连接的都有)会被放到一个链表里面,即使每个tcp连接的端口号、地址啥的不一样
 * 将链表串联起来的就是tcpsock_list
 */
struct tcp_sock {
  struct linked_list tcpsock_list;    // 由于会有很多tcp socket,所以通过这个链表连接起来

  uint32 saddr;                       // 本地ipv4地址
  uint32 daddr;                       // 远程ipv4地址
  uint16 sport;                       // 本地端口
  uint16 dport;                       // 远程端口

  int backlog;                        // connection队列中元素个数,即未完成连接的队列元素个数
  int accept_backlog;                 // accept队列中元素个数,即已完成连接的队列元素个数
  struct linked_list listen_queue;    // 三次握手中处于SYN_RECV状态的队列
  struct linked_list accept_queue;    // 三次握手中处于ESTABLISHED状态的队列,接下来就需要接收数据了
  struct linked_list list;            // 待定

  uint wait_connect;                  // 待定
  uint wait_accept;                   // 睡眠唤醒条件
  uint wait_rcv;                      // 待定

  struct tcp_sock *parent;            // 父socket
  struct tcb tcb;                     // 传输控制块的结构体对象
  uint state;                         // tcp状态
  uint flags;                         // tcp的一些标志

  struct mbuf_queue ofo_queue;        // tcp乱序队列,有一些序号的tcp报文可能会提前到来,就需要放到乱序队列
  struct mbuf_queue rcv_queue;        // 按照序号顺序到来的接收队列
  struct mbuf_queue write_queue;      // 写队列
  
  struct spinlock lock;               // 自旋锁
};

/* 一堆函数,放在这里而不是def.h是因为,在这里的话可以将tcp相关的头文件和源文件一起拿出去用 */

// tcp.c
void tcpdump(struct tcp_header *, struct mbuf *);
void net_rx_tcp(struct mbuf *, uint16, struct ip *);

#endif