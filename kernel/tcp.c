#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "list.h"
#include "mbuf.h"
#include "net.h"
#include "defs.h"
#include "debug.h"
#include "tcp.h"

const char *tcp_state_dbg[] = {
  "TCP_LISTEN",
  "TCP_SYNSENT",
  "TCP_SYN_RECEIVED",
  "TCP_ESTABLISHED",
  "TCP_FIN_WAIT_1",
  "TCP_FIN_WAIT_2",
  "TCP_CLOSE",
  "TCP_CLOSE_WAIT",
  "TCP_CLOSING",
  "TCP_LAST_ACK",
  "TCP_TIME_WAIT"
};

/* tcp信息打印 */
void tcpdump(struct tcp_header *tcphdr, struct mbuf *m)
{
  tcp_dbg("tcp\n");
  tcp_dbg("src port: %d\n", ntohs(tcphdr->sport));
  tcp_dbg("dst port: %d\n", ntohs(tcphdr->dport));
  tcp_dbg("seq: %d\n", ntohl(tcphdr->seq));
  tcp_dbg("ack number: %d\n", ntohl(tcphdr->acknum));
  tcp_dbg("data offset: %d, reserved: 0x%x\n", tcphdr->doff, tcphdr->reserved);
  tcp_dbg("FIN: %d, SYN: %d, RST: %d, PSH: %d, ACK: %d, URG: %d, ECE: %d, CWR: %d\n",
          tcphdr->fin, tcphdr->syn, tcphdr->rst, tcphdr->psh, tcphdr->urg, tcphdr->ece, tcphdr->cwr);
  tcp_dbg("window size: %d\n", ntohs(tcphdr->winsize));
  tcp_dbg("checksum: 0x%x\n", ntohs(tcphdr->checksum));
  tcp_dbg("urgptr: 0x%x\n", ntohs(tcphdr->urgptr));

  tcp_dbg("tcp data len: %d\n", m->len); // 由于拿出了tcp头部,此时m中的head指向了tcp数据部分的开始位置
  hexdump((void *)m->head, m->len); // 打印tcp数据部分
}

/* tcpsock信息打印 */
void tcpsock_dump(char *msg, struct tcp_sock *ts)
{
  tcp_dbg("%s:::TCP x:%d > %d.%d.%d.%d:%d (snd_una %d, snd_nxt %d, snd_wnd %d, "
         "snd_wl1 %d, snd_wl2 %d, rcv_nxt %d, rcv_wnd %d recv-q %d send-q %d "
         " backlog %d) state %s\n", msg,
         ts->sport, (uint8)(ts->daddr >> 24), (uint8)(ts->daddr >> 16), (uint8)(ts->daddr >> 8), (uint8)(ts->daddr >> 0),
         ts->dport, (ts)->tcb.snd_una - (ts)->tcb.iss,
         (ts)->tcb.snd_nxt - (ts)->tcb.iss, (ts)->tcb.snd_wnd,
         (ts)->tcb.snd_wl1, (ts)->tcb.snd_wl2,
         (ts)->tcb.rcv_nxt - (ts)->tcb.irs, (ts)->tcb.rcv_wnd,
         ts->rcv_queue.len, ts->write_queue.len, (ts)->backlog,
         tcp_state_dbg[ts->state]);
}

/* tcp header中的多字节的序号之类的转换为本地字节序 */
void tcpheader_ntoh(struct tcp_header *t)
{
  t->sport = ntohs(t->sport);
  t->dport = ntohs(t->dport);
  t->seq = ntohl(t->seq);
  t->acknum = ntohl(t->acknum);
  t->winsize = ntohs(t->winsize);
  t->checksum = ntohs(t->checksum);
  t->urgptr = ntohs(t->urgptr);
}

/* 初始化tcp段 */
void tcp_init_segment(struct tcp_header *t, struct mbuf *m)
{
  tcpheader_ntoh(t);
  m->seq = t->seq;
  m->end_seq = m->seq + m->len;
}

/*
 * 从tcp_sock链表中获取匹配的tcp sock,若该连接还未建立,就根据监听端口找
 * src: 源地址
 * dst: 目的地址
 * sport: 源端口,本地字节序
 * dport: 目的端口,本地字节序
 */
struct tcp_sock *tcp_sock_lookup(uint32 src, uint32 dst, uint16 sport, uint16 dport)
{
  struct tcp_sock *tcpsock = NULL, *it;

#ifdef CONFIG_DEBUG
  tcp_dbg("sport: %d, dport: %d\n", sport, dport);
#endif

  /* 
   * 首先遍历当前已经有了的连接,这些连接可能处在多种tcp状态,如ESTABLISHED、CLOSE_WAIT、TIME_WAIT等等
   * 但肯定不是LISTEN和CLOSED状态,因为这两个状态要不就是在监听端口以等待连接到来,要不就是已经断开了连接
   */
  acquire(&tcpsocks_list_lk);
  // 遍历所有tcp_sock结构体
  list_for_each_entry(it, &tcpsocks_list_head, tcpsock_list) {
    /*
     * src为对方tcp报文中的源地址,也就是对方的ip; dst为对方报文中的目的地址,也就是收到该报文的本机的地址
     * sport为对方tcp报文中的源端口,也就是对方的端口; dport为对方报文中的目的端口,也就是收到该报文的本机的端口
     * 所以寻找到的it中,it->daddr(本机的目的地址)应该为src,it->saddr(本机的源地址)应该为dst
     * it->dport(本机的目的端口)应该为sport,it->sport(本机的源端口)应该为dport
     * 这里没有再去判断tcp的状态是否是建立的
     */
    if(src == it->daddr && dst == it->saddr && sport == it->dport && dport == it->sport)
    {
      tcpsock = it;
      break;
    }
  }

  // 已经有了的连接中未找到,说明这是一个新连接
  if(tcpsock == NULL)
  {
    list_for_each_entry(it, &tcpsocks_list_head, tcpsock_list) {
      // 需要看一下新连接的端口是否与监听的端口一致
      if(dport == it->sport && it->state == TCP_LISTEN)
      {
        tcpsock = it;
        break;
      }
    }
  }
  
  release(&tcpsocks_list_lk); 
  return tcpsock;
}

// receives a TCP packet
void net_rx_tcp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
  struct tcp_header *tcphdr;
  tcphdr = mbufpullhdr(m, *tcphdr);
  if(!tcphdr)
  {
    tcp_dbg("tcp header is null\n");
    mbuffree(m);
    return;
  }
  
#ifdef CONFIG_DEBUG
  tcpdump(tcphdr, m);
#endif
  
  // 判断长度,若过长则将m->len和m->head指向tcp数据部分的长度和起始位置
  // mbufpullhdr会将此时的m->head向后走struct tcp_header字节,恰好越过了tcp固定头部部分,但是tcp头部可能还会有变长部分,也得越过
  if(tcphdr->doff > TCP_MIN_DATA_OFF) // 头部长度超过20字节了
  {
    // 为了使m->head指向数据部分,则还得使其往后走,越过tcp头部的变长部分
    m->head += (tcphdr->doff - TCP_MIN_DATA_OFF) * 4;
    m->len -= (tcphdr->doff - TCP_MIN_DATA_OFF) * 4;
  }

  // 初始化读取到的tcp段
  tcp_init_segment(tcphdr, m);

  // 获取源ip和目的ip
  uint32 src = ntohl(iphdr->ip_src);
  uint32 dst = ntohl(iphdr->ip_dst);

  // 获取tcp sock, 由于tcp_init_segment中已经将网络字节序转换为了本地字节序,所以这里直接用端口就是了
  struct tcp_sock *tcpsock = tcp_sock_lookup(src, dst, tcphdr->sport, tcphdr->dport);
  if(tcpsock == NULL)
  {
    tcp_dbg("No TCP socket for sport:%d  dport:%d\n", tcphdr->sport, tcphdr->dport);
    mbuffree(m);
    return;
  }

#ifdef CONFIG_DEBUG
  tcp_dbg("tcp socket has found, sport: %d, dport: %d, state: %s\n", tcpsock->sport, tcpsock->dport, tcp_state_dbg[tcpsock->state]);
#endif

  // 获取锁
  acquire(&tcpsock->lock);
  // 根据查找到的tcp socket和收到的tcp报文去检查
  int ret = tcp_input_state(tcpsock, tcphdr, iphdr, m);
  if(ret == 0) // 待定,难道不为0就不释放锁了吗?
    release(&tcpsock->lock);
}