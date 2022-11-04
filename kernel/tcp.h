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
  uint ackseq;  // 确认号
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
  uint16 winsize; // 本机的接收窗口大小
  uint16 checksum; // 校验和
  uint16 urgptr; // 紧急指针,如果urg标志被设置,那么这个指针就有值
};

/* tcp状态 */
enum tcpstate {
  TCP_LISTEN,       /* represents waiting for a connection request from any remote
                   TCP and port. */
  TCP_SYN_SENT,     /* represents waiting for a matching connection request
                     after having sent a connection request. */
  TCP_SYN_RECEIVED, /* represents waiting for a confirming connection
                         request acknowledgment after having both received and sent a
                         connection request. */
  TCP_ESTABLISHED,  /* represents an open connection, data received can be
                        delivered to the user.  The normal state for the data transfer phase
                        of the connection. */
  TCP_FIN_WAIT_1,   /* represents waiting for a connection termination request
                       from the remote TCP, or an acknowledgment of the connection
                       termination request previously sent. */
  TCP_FIN_WAIT_2,   /* represents waiting for a connection termination request
                       from the remote TCP. */
  TCP_CLOSED,        /* represents no connection state at all. */
  TCP_CLOSE_WAIT,   /* represents waiting for a connection termination request
                       from the local user. */
  TCP_CLOSING,      /* represents waiting for a connection termination request
                    acknowledgment from the remote TCP. */
  TCP_LAST_ACK,     /* represents waiting for an acknowledgment of the
                     connection termination request previously sent to the remote TCP
                     (which includes an acknowledgment of its connection termination
                     request). */
  TCP_TIME_WAIT,    /* represents waiting for enough time to pass to be sure
                      the remote TCP received the acknowledgment of its connection
                      termination request. */
};

// tcp头部固定部分的字节数
#define TCP_HDR_LEN sizeof(struct tcp_header)
// tcp头部中doff,也就是头部的数据偏移,也就是tcp总头部(固定部分+变长部分)占多少个4字节,由于只支持固定部分,所以就是5
#define TCP_DOFFSET sizeof(struct tcp_header) / 4

// 设置标志位
#define TCP_FIN 0x01 // 关闭连接
#define TCP_SYN 0x02 // 建立连接
#define TCP_RST 0x04 // 异常的关闭连接,重置tcp连接
#define TCP_PSH 0x08 // 收到该标志位的tcp主机,需要尽快的将该tcp报文传递给应用层
#define TCP_ACK 0x10 // 确认标志

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
  uint32 snd_wl2;  // 上一次用于窗口大小更新的tcp确认报文段的序列号,即对方发送过来了含有窗口变化请求的报文,该报文头部中的确认号就是snd_wl2
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
  struct linked_list list;            // 用于标志该socket是在listen_queue还是accept_queue

  uint wait_connect;                  // 客户端调用connect后,直到三次握手完成,这期间本socket处于wait_connect睡眠状态
  uint wait_accept;                   // 服务端掉用了accept后,直到三次握手完成,这期间本socket处于wait_accept睡眠状态
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

/* tcp.c*/
// 打印tcp信息
void tcpdump(struct tcp_header *tcphdr, struct mbuf *m);
// 打印tcp socket信息
void tcpsock_dump(char *msg, struct tcp_sock *ts);
// 读取tcp数据
void net_rx_tcp(struct mbuf *m, uint16 len, struct ip *iphdr);
// 设置tco状态
void tcp_set_state(struct tcp_sock *ts, enum tcpstate state);

/* tcp_in.c*/
// 依据收到的tcp报文的标志位和本机tcp状态进行相应操作
int tcp_input_state(struct tcp_sock *ts, struct tcp_header *th, struct ip *iphdr, struct mbuf *m);

/* tcp_out.c */
void tcp_send_ack(struct tcp_sock *ts);
int tcp_send_reset(struct tcp_sock *ts);
void tcp_send_synack(struct tcp_sock *ts, struct tcp_header *th);

/* tcp_socket.c */
struct tcp_sock *tcp_sock_alloc();

#endif