#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "list.h"
#include "mbuf.h"
#include "net.h"
#include "defs.h"
#include "debug.h"
#include "tcp.h"

// 创建新的tcp socket
static struct tcp_sock *create_child_tcpsock(struct tcp_sock *ts, struct tcp_header *th, struct ip *iphdr)
{
  // 创建新tcp socket初始化
  struct tcp_sock *newts = tcp_sock_alloc();
  if(newts == NULL)
    return NULL;
  // 由于只有在tcp socket处于listening状态时才会去接收syn报文,所以创建完socket后要设置为syn recv状态
  tcp_set_state(newts, TCP_SYN_RECV);
  // 设置新socket属性
  newts->saddr = ntohl(iphdr->ip_dst);
  newts->daddr = ntohl(iphdr->ip_src);
  newts->sport = th->dport; // 已经转为了本地字节序
  newts->dport = th->sport; // 已经转为了本地字节序
  newts->parent = ts; // 设置父socket,不知道啥用,待定
  list_add(&newts->list, &ts->listen_queue); // 加入到父亲的监听队列,不知道为啥加到父亲上,待定

  return newts;
}

// 开辟初始发送序列号
unsigned int alloc_new_iss(void)
{
  static unsigned int seq = 12344;
  if((++seq) >= 0xffffffff)
    seq = 12345;
  return seq;
}

// tcpsock队列中找到的socket显示该tcp已经关闭了
static int tcp_closed(struct tcp_sock *ts, struct tcp_header *th, struct mbuf *m)
{
#ifdef CONFIG_DEBUG
  tcp_dbg("this tcp socket is closed\n");
#endif
  // TCP/IP Illustrated Vol.2, tcp_input() L291-292
  mbuffree(m);
  return 0;
}

// tcpsock队列中找到的socket显示该端口正在监听
static int tcp_in_listen(struct tcp_sock *ts, struct tcp_header *th, struct ip *iphdr, struct mbuf *m)
{
#ifdef CONFIG_DEBUG
  tcp_dbg("tcp: is listening\n");
#endif
  /*
   * 判断特殊情况
   * 1. rst报文,直接干掉该报文
   * 2. ack报文,由于监听状态收到的是第一个syn报文,不应该有ack标志,所以告知对方重置连接,并丢弃该报文
   * 3. 由于监听状态收到的是第一个syn报文,如果syn状态未被设置,直接干掉该报文
   */
  if(th->rst) // 1. rst报文,直接干掉该报文
    goto discard;
  if(th->ack) // 2. ack报文,由于监听状态收到的是第一个syn报文,不应该有ack标志,所以告知对方重置连接,并丢弃该报文
  {
    tcp_send_reset(ts);
    goto discard;
  }
  if(!th->syn) // 3. 由于监听状态收到的是第一个syn报文,如果syn状态未被设置,直接干掉该报文
    goto discard;

  /*
   * 创建新tcp socket并加入队列
   */
  struct tcp_sock *newts = create_child_tcpsock(ts, th, iphdr);
  if(newts == NULL)
  {
#ifdef CONFIG_DEBUG
    tcp_dbg("cannot alloc new tcp socket\n");
#endif
    goto discard;
  }
  // 设置新socket
  newts->tcb.irs = th->seq; // 初始的接收序列号
  newts->tcb.iss = alloc_new_iss(); // 初始的发送序列号
  newts->tcb.rcv_nxt = th->seq + 1; // 由于第一个syn报文数据部分只有一个字节,所以下一次期望接收到的起始序列号为th->seq + 1
  newts->tcb.snd_nxt = newts->tcb.iss + 1; // 返回的ack报文也是一字节,所以这里也是加一
  newts->tcb.snd_una = newts->tcb.iss; // 未确认的报文就从初始的发送序列号开始
  // 发送syn ack报文
  tcp_send_synack(newts, th);

  discard:
    mbuffree(m);
  return 0;
}

// tcpsock队列中找到的socket显示本机刚处于syn sent状态
static int tcp_synsent(struct tcp_sock *ts, struct tcp_header *th, struct mbuf *m)
{
    return 0;
}

// 检查序列号
static int tcp_verify_seq(struct tcp_sock *ts, struct tcp_header *th, struct mbuf *m)
{
    return 0;
}

/* 
 * 依据本地存储的tcp socket状态和收到的报文的标志位进行相应操作
 * ts为本地tcp_sock队列中找到的与对方连接的tcp socket
 * th为本机接收的tcp报文的头部,已经转为本地字节序
 * iphdr为本机接收的ip报文的头部
 * m为本机接收的链路层的buf,包含链路层头部和数据
 */
int tcp_input_state(struct tcp_sock *ts, struct tcp_header *th, struct ip *iphdr, struct mbuf *m)
{
// 打印tcp socket信息
#ifdef CONFIG_DEBUG
  tcpsock_dump("input state", ts);
#endif
  /*
   * 收到tcp报文后,首先要看一下序列号是否是对的,也就是看一下是否是期望的序列号,然后根据本机中tcp socket队列中相应socket的状态,也就是本机的状态去进行判断,并执行相应操作
   * 但是某几个状态是不需要判断序列号的. closed状态表示该tcp端口关闭了,直接把包丢弃即可;listening状态表示正在监听本端口,对方主动发起连接,此时没有期望序列号,也就不需要看序列号是不是对的;syn_sent状态表示本机主动给对方发起连接,此时也没有期望序列号,也不需要看序列号是不是对的
   * 除此之外的状态需要先查看序列号是否是期望序列号,如果不是,则依据rfc文档描述,需要判断是否是rst报文,如果不是的话需要将报文丢弃
   * 如果序列号检查通过,需要去判断收到的报文的标志位和当前socket的状态进行操作,顺序为
   * 1. 检查序列号
   * 2. 检查是否是rst报文
   * 3. 安全检查和优先权检查
   * 4. 检查是否是syn报文
   * 5. 检查是否有ack标志
   * 6. 检查urg标志
   * 7. 处理数据部分
   * 8. 检查fin标志
   */

  // 某几个状态是不需要判断序列号的
  switch (ts->state)
  {
  case TCP_CLOSED: // 本机找到的tcp socket信息显示已经关闭了
    return tcp_closed(ts, th, m);
  case TCP_LISTEN: // 本机找到的tcp socket信息表示本机正在监听本端口
    return tcp_in_listen(ts, th, iphdr, m);
  case TCP_SYN_SENT:
    return tcp_synsent(ts, th, m);
  }

#ifdef CONFIG_DEBUG
  tcp_dbg("1. check tcp seq\n");
#endif
  /* 
   * 检查序列号,为什么要先检查序列号?
   * 因为序列号不对的话,说明该报文是无效的,也就不需要进行其他检查或步骤了
   */
  if(tcp_verify_seq(ts, th, m) == -1) // 检查没通过
  {
    // 检查是否是rst报文,如果不是的话,就需要返回一个ack以便让对方更改状态,并直接丢弃该报文,如果是的话,需要去后面的rst检查步骤中检查
    if(!th->rst)
    {
        tcp_send_ack(ts);
        goto drop;
    }  
  }

#ifdef CONFIG_DEBUG
  tcp_dbg("2. check tcp RST\n");
#endif
  /*
   * 检查是否是rst报文,为什么先检查rst标志位再去搞其他的步骤?
   * 因为rst表示对方想要强制关闭该连接,即然对方想强制关闭了,那么做其他标志位检查也没啥作用了
   */
  if(th->rst)
  {

  }

#ifdef CONFIG_DEBUG
  tcp_dbg("3. check tcp security and precedence\n");
#endif
  /*
   * 安全检查和优先权检查,这里就不做了
   */
  
#ifdef CONFIG_DEBUG
  tcp_dbg("4. check tcp SYN\n");
#endif
  /*
   * 检查是否是syn报文,为什么先检查syn再去搞其他步骤?
   * 能到这里说明序号是对的,也非rst报文,那么就只剩下syn、普通的报文和fin,除了第一个syn报文,其他报文都有ack标志位
   * 如果是syn报文,那么肯定是不对的,因为只有listening和syn_sent状态才能接收syn报文,其他状态正常情况下不会接收到syn报文
   * 也就是说这个syn状态是一个特殊情况,需要提前判断
   * 而由于除了第一个syn报文其他报文都有ack标志,所以需要处理ack标志位,由于普通报文和fin都可以携带数据,所以还需要处理数据,处理完后发现还有fin标志,就代表是关闭报文
   */
  if(th->syn)
  {

  }

#ifdef CONFIG_DEBUG
  tcp_dbg("5. check tcp ACK\n");
#endif
  if(!th->ack) // 由于除了syn报文都有ack,如果没有的话说明该报文有问题,直接丢弃
    goto drop;
  // 处理ACK标志

#ifdef CONFIG_DEBUG
  tcp_dbg("6. check tcp URG\n");
#endif
  /*
   * 检查urg标志,这里就不做了
   */
  if(th->urg) {}

#ifdef CONFIG_DEBUG
  tcp_dbg("7. check tcp segment data\n");
#endif
  /*
   * 处理数据部分
   */

#ifdef CONFIG_DEBUG
  tcp_dbg("8. check tcp FIN\n");
#endif
  /*
   * 检查fin标志
   */

  drop:
    mbuffree(m);
  return 0;
}