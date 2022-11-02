#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "list.h"
#include "defs.h"
#include "mbuf.h"
#include "tcp.h"

extern uint32 local_ip;

// 分配tcp socket内存
struct tcp_sock *tcp_sock_alloc()
{
  // 开辟内存
  struct tcp_sock *ts = (struct tcp_sock*)kalloc();
  if(ts == NULL)
    return NULL;

  // 初始化为0
  memset((void *)ts, 0, sizeof(*ts));
  // 设置结构体信息,ts中的信息都应该是本地字节序,这里只设置了三个属性
  ts->saddr = local_ip;
  ts->state = TCP_CLOSED; // 初始状态为closed
  ts->tcb.rcv_wnd = TCP_DEFAULT_WINDOW; // 设置默认接收窗口
  // 初始化监听队列
  list_init(&ts->listen_queue);
  // 初始化接收队列
  list_init(&ts->accept_queue);

  // 初始化乱序队列
  mbuf_queue_init(&ts->ofo_queue);
  // 初始化接收队列
  mbuf_queue_init(&ts->rcv_queue);
  // 初始化写队列
  mbuf_queue_init(&ts->write_queue);

  // 初始化自旋锁
  initlock(&ts->lock, "tcp_sock_lock");

  // 获取整个tcp socket锁,并将该socket放到socket链上去
  acquire(&tcpsocks_list_lk);
  list_add(&ts->tcpsock_list, &tcpsocks_list_head);
  release(&tcpsocks_list_lk);

  return ts;
}