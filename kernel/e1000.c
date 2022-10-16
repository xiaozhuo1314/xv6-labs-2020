#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"
#include "debug.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //

  // 打印debug
#ifdef CONFIG_DEBUG
  eth_dbg("[e1000] %d len data transmit\n", m->len);
#endif

  //获取锁
  acquire(&e1000_lock);
  // 读取E1000_TDT控制寄存器，向E1000询问等待下一个数据包的TX环索引
  uint32 idx = regs[E1000_TDT];
  // 检查环是否溢出
  if(idx >= TX_RING_SIZE)
  {
    release(&e1000_lock);
    return -1;
  }
  // 如果E1000_TXD_STAT_DD未在E1000_TDT索引的描述符中设置，则E1000尚未完成先前相应的传输请求，因此返回错误
  struct tx_desc *desc = &tx_ring[idx];
  if(desc == 0 || (desc->status & E1000_TXD_STAT_DD) == 0)
  {
    release(&e1000_lock);
    return -1;
  }
  // 释放从该描述符传输的最后一个mbuf,由于packet可能超过一个mbuf,所以需要循环
  struct mbuf *b = tx_mbufs[idx];
  struct mbuf *tmp;
  for(; b; b = tmp)
  {
    tmp = b->next;
    mbuffree(b);
  }
  // 填写描述符。m->head指向内存中数据包的内容，m->len是数据包的长度。设置必要的cmd标志，并保存指向mbuf的指针，以便稍后释放
  desc->addr = (uint64)m->head;
  desc->length = m->len;
  desc->cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
  tx_mbufs[idx] = m;
  // 通过将一加到E1000_TDT再对TX_RING_SIZE取模来更新环位置
  regs[E1000_TDT] = (idx + 1) % TX_RING_SIZE;
  // 释放锁
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //

  uint32 idx;
  struct rx_desc *desc;
  struct mbuf *m, *b;
  // 由于可能会到来多个,所以需要循环
  while(1)
  {
    //获取锁
    acquire(&e1000_lock);
    //首先通过提取E1000_RDT控制寄存器加一并对RX_RING_SIZE取模，向E1000询问下一个等待接收数据包（如果有）所在的环索引
    idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    // 然后通过检查描述符status部分中的E1000_RXD_STAT_DD位来检查新数据包是否可用。如果不可用，请停止
    desc = &rx_ring[idx];
    if(desc == 0 || (desc->status & E1000_RXD_STAT_DD) == 0)
    {
      release(&e1000_lock);
      break;
    }

    // 将mbuf的m->len更新为描述符中报告的长度。
    m = rx_mbufs[idx];
    mbufput(m, desc->length);
    
    // 使用mbufalloc()分配一个新的mbuf，以替换刚刚给net_rx()的mbuf,将其数据指针（m->head）编程到描述符中。将描述符的状态位清除为零
    b = mbufalloc(0);
    if(b == 0)
      panic("e1000_recv mbufalloc failed");
    rx_mbufs[idx] = b;
    desc->addr = (uint64)b->head;
    desc->status = 0;

    // 将E1000_RDT寄存器更新为最后处理的环描述符的索引
    regs[E1000_RDT] = idx;
    release(&e1000_lock); // 必须要在net_rx前面,否则会死锁,因为net_rx函数中调用的其它函数会调用e1000_transmit函数,这样就导致了死锁

  // 打印debug
#ifdef CONFIG_DEBUG
    eth_dbg("[e1000] %d len data received\n", m->len);
#endif
    
    // 使用net_rx()将mbuf传送到网络栈
    net_rx(m);
  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;
  e1000_recv();
}
