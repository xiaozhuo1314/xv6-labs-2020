#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "list.h"
#include "mbuf.h"
#include "net.h"
#include "defs.h"
#include "debug.h"
#include "tcp.h"

int tcp_v4_checksum(struct mbuf *m, uint32 saddr, uint32 daddr)
{
    return 0;
}

void tcp_transmit_mbuf(struct tcp_sock *ts, struct tcp_header *th, struct mbuf *m, uint32 seq)
{
  //主要是设置tcp头部
  th->doff = TCP_DOFFSET; // 设置tcp头部数据偏移,也就是以4字节为单位的tcp头部大小
  th->sport = htons(ts->sport); // 本地端口
  th->dport = htons(ts->dport); // 目的端口
  th->seq = htonl(seq); // 序列号
  th->acknum = htonl(ts->tcb.rcv_nxt); // 期望下次收到的序号
  th->reserved = 0; // 保留位
  th->winsize = htons(ts->tcb.rcv_wnd); // 本机接收窗口大小
  th->urg = 0;
  th->checksum = tcp_v4_checksum(m, htonl(ts->saddr), htonl(ts->daddr));

  // 发送出去
  net_tx_ip(m, IPPROTO_TCP, ts->daddr); // net_tx_ip中有htonl将ts->daddr转换为网络字节序,所以这里不需要
}

int tcp_send_reset(struct tcp_sock *ts)
{
  // 创建网卡需要发送的数据,也就是mbuf
  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if(m == 0)
    return -1;
  // 从MBUF_DEFAULT_HEADROOM中,也就是mbuf中的buf和head之间加入空的tcp头部
  struct tcp_header *th;
  th = mbufpushhdr(m, *th);
  th->rst = 1; // 因为是重置报文,所以应该让RST标志位为1
  // 下一个将要发送的序列号为snd_nxt,所以snd_una待验证的序列号需要设置为这个
  ts->tcb.snd_una = ts->tcb.snd_nxt;
  // 发送数据
  tcp_transmit_mbuf(ts, th, m, ts->tcb.snd_nxt);
  return 0;
}