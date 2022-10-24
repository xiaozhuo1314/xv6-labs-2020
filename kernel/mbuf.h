// packet buffer
#ifndef __MBUF_H__
#define __MBUF_H__

#define MBUF_SIZE 2048 // 每个存放packet的mbuf的数据区域的大小

/*
 * mbuf中buf是存放packet的数据区域,但是packet不一定就是从buf的起始内存地址开始的
 * 而是从mbuf的head位置开始的,head是在buf区域中,所以headroom就是buf和head之间的那部分空白区域
 * 也就是packet从buf+headroom位置处开始存放
 */
#define MBUF_DEFAULT_HEADROOM 128

struct mbuf {
  struct mbuf  *next; // the next mbuf in the chain
  char         *head; // the current start position of the buffer
  unsigned int len;   // the length of the buffer
  char         buf[MBUF_SIZE]; // the backing store

  // 加入tcp需要使用的
  int refcnt; // 引用计数
  uint32 seq; // tcp报文段的初始序列号,由于每个字节都有自己的序列号,所以一个tcp报文段中会有很多序列号
  uint32 end_seq; // tcp报文段的终止序列号
  struct linked_list list; // 待定
};

char *mbufpull(struct mbuf *m, unsigned int len);
char *mbufpush(struct mbuf *m, unsigned int len);
char *mbufput(struct mbuf *m, unsigned int len);
char *mbuftrim(struct mbuf *m, unsigned int len);

// The above functions manipulate the size and position of the buffer:
//            <- push            <- trim
//             -> pull            -> put
// [-headroom-][------buffer------][-tailroom-]
// |----------------MBUF_SIZE-----------------|
//
// These marcos automatically typecast and determine the size of header structs.
// In most situations you should use these instead of the raw ops above.
#define mbufpullhdr(mbuf, hdr) (typeof(hdr)*)mbufpull(mbuf, sizeof(hdr))
#define mbufpushhdr(mbuf, hdr) (typeof(hdr)*)mbufpush(mbuf, sizeof(hdr))
#define mbufputhdr(mbuf, hdr) (typeof(hdr)*)mbufput(mbuf, sizeof(hdr))
#define mbuftrimhdr(mbuf, hdr) (typeof(hdr)*)mbuftrim(mbuf, sizeof(hdr))

struct mbuf *mbufalloc(unsigned int headroom);
void mbuffree(struct mbuf *m);

struct mbufq {
  struct mbuf *head;  // the first element in the queue
  struct mbuf *tail;  // the last element in the queue
};

void mbufq_pushtail(struct mbufq *q, struct mbuf *m);
struct mbuf *mbufq_pophead(struct mbufq *q);
int mbufq_empty(struct mbufq *q);
void mbufq_init(struct mbufq *q);

/*
 * mbuf队列,不知道为啥有了mbufq还需要这个,貌似是tcp用到这个,udp用的mbufq
 * 待定
 */
struct mbuf_queue {
  struct linked_list head; // 队列头元素
  uint32 len; // 队列元素的个数
};

// mbuf_queue的长度
static _inline uint32 mbuf_queue_len(const struct mbuf_queue *q)
{
  return q->len;
}

// 初始化mbuf_queue
static _inline void mbuf_queue_init(struct mbuf_queue *q)
{
  list_init(&q->head);
}

// 往mbuf_queue队列添加新元素,将new_node添加到next_node前面
static _inline void mbuf_queue_add(struct mbuf_queue *q, struct mbuf *new_node, struct mbuf *next_node)
{
  list_add_tail(&new_node->list, &next_node->list);
  q->len++;
}

/*
 * 往mbuf_queue队列添加新元素,将new_node添加到head前面,也就是new_node成了最后一个
 * 这里采用的是队列的先进先出策略
 */
static _inline void mbuf_enqueue(struct mbuf_queue *q, struct mbuf *new_node)
{
  list_add_tail(&new_node->list, &q->head);
  q->len++;
}

/* 
 * 从mbuf_queue队列删除元素,并获取链表节点所在的mbuf
 * 这里采用的是队列的先进先出策略,所以删除的时候就是删除开头的元素
 * 由于head是哨兵,所以删除的是head后面的节点
 */
static _inline struct mbuf* mbuf_dequeue(struct mbuf_queue *q)
{
  struct mbuf *m = list_first_entry(&q->head, struct mbuf, list);
  list_del(&m->list);
  q->len--;
  return m;
}

// 判断mbuf_queue是否为空
static _inline int mbuf_queue_empty(struct mbuf_queue *q)
{
  return mbuf_queue_len(q) < 1;
}

// 获取mbuf_queue中的哨兵head下一个节点所在的mbuf
static _inline struct mbuf* mbuf_queue_peek(struct mbuf_queue *q)
{
  // head不算在mbuf_queue的长度里,所以最后剩下head时就已经长度小于1了,就会返回null
  if(mbuf_queue_empty(q))
    return NULL;
  return list_first_entry(&q->head, struct mbuf, list);
}

// 清空mbuf_queue
static _inline void mbuf_queue_free(struct mbuf_queue *q)
{
  struct mbuf *m = NULL;

  while((m = mbuf_queue_peek(q)) != NULL)
  {
    mbuf_dequeue(q);
    mbuffree(m);
  }
  // 最后会剩下哨兵head节点
}

#endif