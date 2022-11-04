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
  tcp_set_state(newts, TCP_SYN_RECEIVED);
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
  /*
   * 此时tcp sock处于syn sent状态
   * 收到的报文,如果是syn=1 & ack=1,且ack_num正好是正在等的,那么就是established状态
   * 如果收到了syn=1但是没有ack标志的报文,则需要设置为syn recv状态
   * 其他状态需要特殊分析
   */
  
  // 收到的报文里面有ack标志
  if(th->ack)
  {
    /* 
     * 发过来的tcp报文的ack seq,是对本方前一个报文的确认
     * 因此发过来的tcp报文的ack seq应该是与本方的发送序列号集合有关
     * 如果这个ack seq小于等于本方发送的第一个序列号(因为ack seq至少是加了1的),或者大于接下来将要发送的序列号
     * 说明肯定是不对的
     */ 
    if(th->ackseq <= ts->tcb.iss || th->ackseq > ts->tcb.snd_nxt)
    {
      // 重置连接
      tcp_send_reset(ts);
      goto discard;
    }
  }

  // 如果是rst
  if(th->rst)
  {
    // rst不应有ack,是不正确的
    if(th->ack)
    {
#ifdef CONFIG_DEBUG
      tcp_dbg("Error:connection reset\n");
#endif
      tcp_set_state(ts, TCP_CLOSED);
      wakeup(&ts->wait_connect);
      goto discard;
    }
  }

  // 收到的报文里有syn
  if(th->syn)
  {
    /*
     * 能到这里说明在有syn标志的前提下,要不就是有ack标志且序列号确认通过,要不就是没有确认标志
     * 不管哪种情况,都是正常情况,而且由于此时本机处于的是syn sent状态,所以收到的报文是本次连接过程中
     * 本机第一次收到的报文,所以不管哪种情况都需要设置初始接受的报文和待确认的下一次报文
     */
    ts->tcb.irs = th->seq;
    ts->tcb.rcv_nxt = th->seq + 1; // syn报文只有一个字节,且对方发过来的是syn报文
    // 既有syn又有ack, 说明本机的报文能被确认了,而且接收到的ackseq是期待收到的下一个序列号
    // 但是这里要注意的是,tcp报文不一定按照顺序到达,所以ackseq不一定就等于snd_nxt
    // 有可能本机发送了1 2 3三个报文,接下来要发4,但是收到的是对2的确认报文,所以此时ackseq为3,不等于4
    // 所以对于syn sent状态的本机来说,只要syn和ack标志有,且收到的报文ackseq序号大于本机已发送但待确认的初始序号
    // 即可认为对方是确认了连接请求
    if(th->ack && th->ackseq > ts->tcb.snd_una) // 对方确认了连接
    {
      // 对方确认了连接,那么本方成了established状态且需要再返回一个ack报文
      tcp_set_state(ts, TCP_ESTABLISHED);
      // 设置一些属性
      ts->tcb.snd_wnd = th->winsize;
      ts->tcb.snd_wl1 = th->seq;
      ts->tcb.snd_wl2 = th->ackseq;
      tcp_send_ack(ts);

      /* 
       * 更新已发送但是未确认的序列号集合的初始序列号,
       * 因为th->ackseq前面的都已经确认被对方收到了,
       * 所以此时本机已发送但待确认的初始序列号应该是从th->ackseq开始
       */
      ts->tcb.snd_una = th->ackseq;

      /*
       * 这里要解释一下,为什么发送完之后没有更改snd_nxt
       * 本机此时的状态为syn sent状态,且收到了带有ack标志和syn标志、序列号是正确的报文
       * 说明对方现在是syn rcvd状态,上面的代码中本机发了一个ack报文(假设序列号为x+1)过去后
       * 若对方收到了该报文,那么对方的状态是设置成了established状态,设置完后不会再给本机发送确认报文了
       * 也就是tcp_synrecv_ack函数不会发送报文,因为本机发送的x+1报文仅仅只是一个ack确认报文,未携带数据之类的
       * 所以对方不应该再对这个ack报文进行ack确认,ack确认报文,ack确认报文的ack确认报文,ack确认报文的ack确认报文的ack确认报文,这不就是死循环了嘛
       * 那么就相当于本机x+1序列号的ack报文永远不会被确认
       * 那么我们就可以复用这个序列号x+1,复用之后的序列号所在的报文就需要携带数据之类的进行通信了,这样的话就可以对这个序列号进行ack确认了
       * 而且tcp_synrecv_ack函数既没有对ack报文进行ack确认,也未改变rcv_nxt,也就是rcv_nxt一直是x+1未变化
       * 所以本机就可以复用上面ack报文(序列号为x+1)的序列号x+1,这样正好对方rcv_nxt也是期待收到x+1报文
       * 实现了序列号资源的节省
       */

#ifdef CONFIG_DEBUG
      tcp_dbg("Active three-way handshake successes!(SND.WIN:%d)\n", ts->tcb.snd_wnd);
#endif

      // 唤醒等待
      wakeup(&ts->wait_connect); // 之前本socket睡眠在了等待连接中,也就是在等待连接完成
    }
    else // 否则需要进入syc rcvd状态
    {
      tcp_set_state(ts, TCP_SYN_RECEIVED);
      // 进入了SYN_RECEIVED状态,就说明相当于本机第一次接收到了对方syn(不带ack)报文
      // 那么本机需要回复syn+ack报文,但是本机前面的syn报文还未被确认呢,所以snd_una需要为iss
      ts->tcb.snd_una = ts->tcb.iss;
      // 发送syn+ack报文
      tcp_send_synack(ts, th);
    }
  }
  discard:
    mbuffree(m);
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