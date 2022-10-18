//
// networking protocol support (IP, UDP, ARP, etc.).
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "net.h"
#include "defs.h"
#include "debug.h" // user add

static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15); // qemu's idea of the guest IP
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint8 broadcast_mac[ETHADDR_LEN] = { 0xFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF };

// Strips data from the start of the buffer and returns a pointer to it.
// Returns 0 if less than the full requested length is available.
char *
mbufpull(struct mbuf *m, unsigned int len)
{
  char *tmp = m->head;
  if (m->len < len)
    return 0;
  m->len -= len;
  m->head += len;
  return tmp;
}

// Prepends data to the beginning of the buffer and returns a pointer to it.
char *
mbufpush(struct mbuf *m, unsigned int len)
{
  m->head -= len;
  if (m->head < m->buf)
    panic("mbufpush");
  m->len += len;
  return m->head;
}

// Appends data to the end of the buffer and returns a pointer to it.
char *
mbufput(struct mbuf *m, unsigned int len)
{
  char *tmp = m->head + m->len;
  m->len += len;
  if (m->len > MBUF_SIZE)
    panic("mbufput");
  return tmp;
}

// Strips data from the end of the buffer and returns a pointer to it.
// Returns 0 if less than the full requested length is available.
char *
mbuftrim(struct mbuf *m, unsigned int len)
{
  if (len > m->len)
    return 0;
  m->len -= len;
  return m->head + m->len;
}

// Allocates a packet buffer.
struct mbuf *
mbufalloc(unsigned int headroom)
{
  struct mbuf *m;
 
  if (headroom > MBUF_SIZE)
    return 0;
  m = kalloc();
  if (m == 0)
    return 0;
  m->next = 0;
  m->head = (char *)m->buf + headroom;
  m->len = 0;
  memset(m->buf, 0, sizeof(m->buf));
  return m;
}

// Frees a packet buffer.
void
mbuffree(struct mbuf *m)
{
  kfree(m);
}

// Pushes an mbuf to the end of the queue.
void
mbufq_pushtail(struct mbufq *q, struct mbuf *m)
{
  m->next = 0;
  if (!q->head){
    q->head = q->tail = m;
    return;
  }
  q->tail->next = m;
  q->tail = m;
}

// Pops an mbuf from the start of the queue.
struct mbuf *
mbufq_pophead(struct mbufq *q)
{
  struct mbuf *head = q->head;
  if (!head)
    return 0;
  q->head = head->next;
  return head;
}

// Returns one (nonzero) if the queue is empty.
int
mbufq_empty(struct mbufq *q)
{
  return q->head == 0;
}

// Intializes a queue of mbufs.
void
mbufq_init(struct mbufq *q)
{
  q->head = 0;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

// sends an ethernet packet
static void
net_tx_eth(struct mbuf *m, uint16 ethtype)
{
  struct eth *ethhdr;

  ethhdr = mbufpushhdr(m, *ethhdr);
  memmove(ethhdr->shost, local_mac, ETHADDR_LEN);
  // In a real networking stack, dhost would be set to the address discovered
  // through ARP. Because we don't support enough of the ARP protocol, set it
  // to broadcast instead.
  memmove(ethhdr->dhost, broadcast_mac, ETHADDR_LEN);
  ethhdr->type = htons(ethtype);
  if (e1000_transmit(m)) {
    mbuffree(m);
  }
}

// sends an IP packet
static void
net_tx_ip(struct mbuf *m, uint8 proto, uint32 dip)
{
  struct ip *iphdr;

  // push the IP header
  iphdr = mbufpushhdr(m, *iphdr);
  memset(iphdr, 0, sizeof(*iphdr));
  iphdr->ip_vhl = (4 << 4) | (20 >> 2);
  iphdr->ip_p = proto;
  iphdr->ip_src = htonl(local_ip);
  iphdr->ip_dst = htonl(dip);
  iphdr->ip_len = htons(m->len);
  iphdr->ip_ttl = 100;
  iphdr->ip_sum = in_cksum((unsigned char *)iphdr, sizeof(*iphdr));

  // now on to the ethernet layer
  net_tx_eth(m, ETHTYPE_IP);
}

// sends a UDP packet
void
net_tx_udp(struct mbuf *m, uint32 dip,
           uint16 sport, uint16 dport)
{
  struct udp *udphdr;

  // put the UDP header
  udphdr = mbufpushhdr(m, *udphdr);
  udphdr->sport = htons(sport);
  udphdr->dport = htons(dport);
  udphdr->ulen = htons(m->len);
  udphdr->sum = 0; // zero means no checksum is provided

  // now on to the IP layer
  net_tx_ip(m, IPPROTO_UDP, dip);
}

// sends an ARP packet
static int
net_tx_arp(uint16 op, uint8 dmac[ETHADDR_LEN], uint32 dip)
{
  struct mbuf *m;
  struct arp *arphdr;

  m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!m)
    return -1;

  // generic part of ARP header
  arphdr = mbufputhdr(m, *arphdr);
  arphdr->hrd = htons(ARP_HRD_ETHER);
  arphdr->pro = htons(ETHTYPE_IP);
  arphdr->hln = ETHADDR_LEN;
  arphdr->pln = sizeof(uint32);
  arphdr->op = htons(op);

  // ethernet + IP part of ARP header
  memmove(arphdr->sha, local_mac, ETHADDR_LEN);
  arphdr->sip = htonl(local_ip);
  memmove(arphdr->tha, dmac, ETHADDR_LEN);
  arphdr->tip = htonl(dip);

  // header is ready, send the packet
  net_tx_eth(m, ETHTYPE_ARP);
  return 0;
}

// receives an ARP packet
static void
net_rx_arp(struct mbuf *m)
{
  struct arp *arphdr;
  uint8 smac[ETHADDR_LEN];
  uint32 sip, tip;

  arphdr = mbufpullhdr(m, *arphdr);
  if (!arphdr)
    goto done;

  // validate the ARP header
  if (ntohs(arphdr->hrd) != ARP_HRD_ETHER ||
      ntohs(arphdr->pro) != ETHTYPE_IP ||
      arphdr->hln != ETHADDR_LEN ||
      arphdr->pln != sizeof(uint32)) {
    goto done;
  }

  // only requests are supported so far
  // check if our IP was solicited
  tip = ntohl(arphdr->tip); // target IP address
  if (ntohs(arphdr->op) != ARP_OP_REQUEST || tip != local_ip)
    goto done;

  // handle the ARP request
  memmove(smac, arphdr->sha, ETHADDR_LEN); // sender's ethernet address
  sip = ntohl(arphdr->sip); // sender's IP address (qemu's slirp)
  net_tx_arp(ARP_OP_REPLY, smac, sip);

done:
  mbuffree(m);
}

// receives a UDP packet
static void
net_rx_tcp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
  struct tcp *tcphdr;
  tcphdr = mbufpullhdr(m, *tcphdr);
  if(!tcphdr)
    goto fail;
  
#ifdef CONFIG_DEBUG
  tcpdump(tcphdr, m);
#endif
  
  // 判断长度,若过长则将m->len和m->head指向tcp数据部分的长度和起始位置
  // mbufpullhdr会将此时的m->head向后走struct tcp字节,恰好越过了tcp固定头部部分,但是tcp头部可能还会有变长部分,也得越过
  if(tcphdr->doff > TCP_MIN_DATA_OFF) // 头部长度超过20字节了
  {
    // 为了使m->head指向数据部分,则还得使其往后走,越过tcp头部的变长部分
    m->head += (tcphdr->doff - TCP_MIN_DATA_OFF) * 4;
    m->len -= (tcphdr->doff - TCP_MIN_DATA_OFF) * 4;
  }

fail:
  mbuffree(m);
}

// receives a UDP packet
static void
net_rx_udp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
  struct udp *udphdr;
  uint32 sip;
  uint16 sport, dport;


  udphdr = mbufpullhdr(m, *udphdr);
  if (!udphdr)
    goto fail;

  // TODO: validate UDP checksum

  // validate lengths reported in headers
  if (ntohs(udphdr->ulen) != len)
    goto fail;
  len -= sizeof(*udphdr);
  if (len > m->len)
    goto fail;
  // minimum packet size could be larger than the payload
  mbuftrim(m, m->len - len);

  // parse the necessary fields
  sip = ntohl(iphdr->ip_src);
  sport = ntohs(udphdr->sport);
  dport = ntohs(udphdr->dport);
  sockrecvudp(m, sip, dport, sport);
  return;

fail:
  mbuffree(m);
}

// receives an IP packet
static void
net_rx_ip(struct mbuf *m)
{
  struct ip *iphdr;
  uint16 len;

  iphdr = mbufpullhdr(m, *iphdr);
  if (!iphdr)
	  goto fail;

#ifdef CONFIG_DEBUG
  ipdump(iphdr, m);
#endif

  // check IP version and header len
  if (iphdr->ip_vhl != ((4 << 4) | (20 >> 2)))
    goto fail;
  // validate IP checksum
  if (in_cksum((unsigned char *)iphdr, sizeof(*iphdr)))
    goto fail;
  // can't support fragmented IP packets
  if (htons(iphdr->ip_off) != 0)
    goto fail;
  // is the packet addressed to us?
  if (htonl(iphdr->ip_dst) != local_ip)
    goto fail;
  
  len = ntohs(iphdr->ip_len) - sizeof(*iphdr);
  // support UDP and TCP
  if (iphdr->ip_p == IPPROTO_UDP)
    net_rx_udp(m, len, iphdr);
  else if(iphdr->ip_p == IPPROTO_TCP)
    net_rx_tcp(m, len, iphdr);
  else
    goto fail;

  return;

fail:
  mbuffree(m);
}

// called by e1000 driver's interrupt handler to deliver a packet to the
// networking stack
void net_rx(struct mbuf *m)
{
  struct eth *ethhdr;
  uint16 type;

  ethhdr = mbufpullhdr(m, *ethhdr);
  if (!ethhdr) {
    mbuffree(m);
    return;
  }

  type = ntohs(ethhdr->type);
  if (type == ETHTYPE_IP)
    net_rx_ip(m);
  else if (type == ETHTYPE_ARP)
    net_rx_arp(m);
  else
    mbuffree(m);
}

/* uint32位的ip地址转换为十进制点分制 */
char *ip2host(uint32 ip, char *dst, uint sz)
{
  memset((void *)dst, 0, sz);
  uint8 *p = (uint8 *)&ip;
  snprintf(dst, sz, "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
  return dst;
}

/* ip信息打印 */
void ipdump(struct ip *iphdr, struct mbuf *m)
{
  ip_dbg("ip\n");
  ip_dbg("version: %d, header length: %d\n", (iphdr->ip_vhl & 0xf0) >> 4, (iphdr->ip_vhl & 0x0f) << 2); // 单位是4字节
  ip_dbg("type of service: 0x%x\n", iphdr->ip_tos);
  ip_dbg("total length: %d\n", ntohs(iphdr->ip_len));
  ip_dbg("identification: 0x%x\n", ntohs(iphdr->ip_id));
  ip_dbg("flags: 0x%x, offset: 0x%x\n", (iphdr->ip_off & 0xe0) >> 5, ntohs(iphdr->ip_off) & 0x1fff);
  ip_dbg("time to live: %d\n", iphdr->ip_ttl);
  if(iphdr->ip_p == IPPROTO_ICMP)
    ip_dbg("protocol: ICMP\n");
  else if(iphdr->ip_p == IPPROTO_TCP)
    ip_dbg("protocol: TCP\n");
  else if(iphdr->ip_p == IPPROTO_UDP)
    ip_dbg("protocol: UDP\n");
  else
    ip_dbg("unknown protocol: %d\n", iphdr->ip_p);
  ip_dbg("checksum: 0x%x\n", ntohs(iphdr->ip_sum));
  char ip_addr[IPSTR_LEN];
  ip_dbg("source addr: %s\n", ip2host(iphdr->ip_src, ip_addr, IPSTR_LEN));
  ip_dbg("destination addr: %s\n", ip2host(iphdr->ip_dst, ip_addr, IPSTR_LEN));

  hexdump((void *)m->head, m->len); // 打印ip数据部分
}

/* tcp信息打印 */
void tcpdump(struct tcp *tcphdr, struct mbuf *m)
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