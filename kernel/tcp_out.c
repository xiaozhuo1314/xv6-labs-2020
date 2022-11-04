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

/* 
 * 每两个字节(16bits)作为uint16类型进行整数相加求和
 * addr表示起始字节位置
 * cnt表示一共有多少个字节
 */
uint32 sum_every_2bytes(void *addr, int cnt)
{
  register uint32 sum = 0;
  uint16 *p = (uint16 *)addr;
  while(cnt > 1)
  {
    sum += *(p++);
    cnt -= 2; // 由于每个uint16占2字节,所以应该减去2
  }
  // 若还剩下一个字节,则也需要加上
  if(cnt > 0)
  {
    sum += *((uint8 *)p);
  }
  return sum;
}

// 设置通用校验值 https://tools.ietf.org/html/rfc1071
uint16 checksum(void *addr, int cnt, int start)
{
  // 将addr开始的内存位置处,每两个字节作为一个uint16整形数字,加到start上,若和超过了16bits,需要将前16bits和后16bits相加
  uint32 sum = start;
  sum += sum_every_2bytes(addr, cnt);

  // 32bits 转为 16bits
  while((sum >> 16))
    sum = (sum & 0xffff) + (sum >> 16);
  
  // 返回值取反
  return ~sum;
}

// 设置tcp ipv4的校验值
int tcp_v4_checksum(struct mbuf *m, uint32 saddr, uint32 daddr)
{
  uint32 sum = 0;
  sum += saddr;
  sum += daddr;
  sum += htons(IPPROTO_TCP);
  sum += htonl(m->len);
  return checksum(m->head, m->len, sum);
}

// 设置tcp头部并发送出去,ts中的信息都是本地字节序
void tcp_transmit_mbuf(struct tcp_sock *ts, struct tcp_header *th, struct mbuf *m, uint32 seq)
{
  //主要是设置tcp头部
  th->doff = TCP_DOFFSET; // 设置tcp头部数据偏移,也就是以4字节为单位的tcp头部大小
  th->sport = htons(ts->sport); // 本地端口
  th->dport = htons(ts->dport); // 目的端口
  th->seq = htonl(seq); // 序列号
  th->ackseq = htonl(ts->tcb.rcv_nxt); // 期望下次收到的序号
  th->reserved = 0; // 保留位
  th->winsize = htons(ts->tcb.rcv_wnd); // 本机接收窗口大小
  th->urg = 0;
  th->checksum = tcp_v4_checksum(m, htonl(ts->saddr), htonl(ts->daddr));

  // 发送出去
  net_tx_ip(m, IPPROTO_TCP, ts->daddr); // net_tx_ip中有htonl将ts->daddr转换为网络字节序,所以这里不需要
}

// 发送reset报文
int tcp_send_reset(struct tcp_sock *ts)
{
  // 创建网卡需要发送的数据,也就是mbuf
  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if(m == NULL)
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

// 发送syn确认报文,这里的th为对方发过来的syn报文头,这里直接复用了其内存位置
void tcp_send_synack(struct tcp_sock *ts, struct tcp_header *srcth)
{
  if(srcth->rst)
    return;
  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if(m == NULL)
    return;
  // 从MBUF_DEFAULT_HEADROOM中,也就是mbuf中的buf和head之间加入空的tcp头部
  struct tcp_header *th;
  th = mbufpushhdr(m, *th);
  th->ack = 1; // syn的确认报文
  th->syn = 1; // syn的确认报文也需要进行握手请求

  tcp_transmit_mbuf(ts, th, m, ts->tcb.iss);
}

// 发送ack确认报文,不带数据的
void tcp_send_ack(struct tcp_sock *ts)
{
  if(ts->state == TCP_CLOSED)
    return;
  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if(m == NULL)
    return;
  struct tcp_header *th;
  th = mbufpushhdr(m, *th);
  th->ack = 1; // 确认报文标志

  tcp_transmit_mbuf(ts, th, m, ts->tcb.snd_nxt);
}