// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NUMBUCKET 13
#define HASH(id) (id % NUMBUCKET)

/* 
 * 单个散列桶结构体
 * 包含锁和桶中的头节点
 */
struct hashbuf {
  struct spinlock lock; // 锁
  struct buf *head; // 桶中的头节点
};

struct {
  // struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct hashbuf buckets[NUMBUCKET]; // 哈希表放置不同的block
} bcache;

void
binit(void)
{
  struct buf *b;

  // 锁的名字
  char lockname[16] = {0};

  // 初始化所有的buf
  for(b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // 所有的buf前后都指向自己
    b->prev = b;
    b->next = b;
    snprintf(lockname, sizeof(lockname), "buffer_%d", b - bcache.buf);
    initsleeplock(&(b->lock), lockname);
  }
  
  // 把buf均摊给各个bucket
  int gap = NBUF / NUMBUCKET;
  int mod = NBUF % NUMBUCKET;
  
  b = bcache.buf;
  // 初始化桶的结构体
  for(int i = 0; i < NUMBUCKET; ++i)
  {
    snprintf(lockname, sizeof(lockname), "bcache_%d", i);
    initlock(&(bcache.buckets[i].lock), lockname); // 初始化锁
    // 设置每个桶中头节点
    bcache.buckets[i].head = b++; // 由于前面初始化buf的时候已经将buf前后都指向了自己,所以这里就不重复执行了
    // 将buf均摊给bucket,每个获得gap个
    for(int j = 1; j < gap; ++j)
    {
      // 头插法
      b->prev = bcache.buckets[i].head->prev;
      b->next = bcache.buckets[i].head;
      bcache.buckets[i].head->prev->next = b;
      bcache.buckets[i].head->prev = b;
      ++b;
    }
  }
  // 是否还剩下一些buf未分配,是的话就再从头开始分配,每个桶一个,直到分配完
  if(mod > 0)
  {
    for(int i = 0; b < bcache.buf + NBUF; b++, i++)
    {
      // 头插法
      b->prev = bcache.buckets[i].head->prev;
      b->next = bcache.buckets[i].head;
      bcache.buckets[i].head->prev->next = b;
      bcache.buckets[i].head->prev = b;
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b, *res = 0;

  // Is the block already cached?
  int idx = HASH(blockno);
  int cycle = 0;
  acquire(&(bcache.buckets[idx].lock)); // 获取哈希桶的锁

  /*
   * 首先要看一下当前桶是否是空的,如果为空,就只能去其他桶中获取了
   */
  if(bcache.buckets[idx].head != 0)
  {
    // 查找桶中是不是已经有了blockno,有的话就引用计数加一返回,否则就去桶中LRU找一个
    // 由于桶中的链表为双链表,所以下面的for循环需要判断b是否为第二次遍历到了head,即b != bcache.buckets[idx].head
    // 但是该条件很显然一上来就不满足,所以需要一个变量表示当前是不是第一次遍历到head
    // b != bcache.buckets[idx].head可以进行循环或者cycle == 0也可以进行循环
    for(b = bcache.buckets[idx].head, cycle = 0; b != bcache.buckets[idx].head || cycle == 0; b = b->next, cycle++)
    {
      if(b->dev == dev && b->blockno == blockno) // 找到了缓存
      {
        b->refcnt++;
        // 这里不设置时间,否则会引起死锁
        // acquire(&tickslock);
        // b->lasttime = ticks;
        // release(&tickslock);

        release(&(bcache.buckets[idx].lock)); // 这里必须要首先释放再去获取桶的锁,否则可能会死锁
        acquiresleep(&(b->lock));
        return b;
      }
    }

    /*
    * 没有找到cache,需要LRU分配,分配策略为:
    * 从本桶中根据LRU算法寻找空buf
    */
    if(bcache.buckets[idx].head != 0)
    {
      for(b = bcache.buckets[idx].head, cycle = 0; b != bcache.buckets[idx].head || cycle == 0; b = b->next, cycle++)
      {
        if(b->refcnt == 0 && (res == 0 || b->lasttime < res->lasttime))
          res = b;
      }
      // 在本桶中找到了
      if(res)
      {
        res->blockno = blockno;
        res->dev = dev;
        res->valid = 0; // 数据还未从磁盘中读取
        res->refcnt = 1;
        // 这里不设置时间,否则会引起死锁
        // acquire(&tickslock);
        // b->lasttime = ticks;
        // release(&tickslock);

        release(&(bcache.buckets[idx].lock)); // 这里必须要首先释放再去获取桶的锁,否则可能会死锁
        acquiresleep(&(res->lock));
        return res;
      }
    }
  }


  /*
   * 本桶中未找到,需要从其它桶根据LRU获取
   * 首先需要释放本桶的锁,如果不释放可能会产生死锁,即使采用holding函数来判断
   * holding函数判断的是当前的cpu有没有该锁,如果另一个进程运行在另一个cpu上,想要获取当前桶的锁
   * 当前的进程想要另一个cpu上进程的桶的锁,就会死锁
   * 所以这里就只能牺牲原子性
   */
  release(&(bcache.buckets[idx].lock));
  for(int i = 0; i < NUMBUCKET; ++i)
  {
    if(i == idx)
      continue;
    /*
     * 这里去acquire(&(bcache.buckets[i].lock))时会不会出现同一个cpu上重复acquire呢?
     * 由于acquire里面会关闭中断,因此不会出现当前进程获取到了锁,然后下一个进程被调度进当前cpu再次去获取锁,因为中断关了
     */
    acquire(&(bcache.buckets[i].lock));
    if(bcache.buckets[i].head == 0)
    {
      release(&(bcache.buckets[i].lock));
      continue;
    }
    for(b = bcache.buckets[i].head, cycle = 0; b != bcache.buckets[i].head || cycle == 0; b = b->next, cycle++)
    {
      if(b->refcnt == 0 && (res == 0 || b->lasttime < res->lasttime))
        res = b;
    }
    // 在其他桶找到了
    if(res)
    {
      if(res->next == res) // 其他桶中只剩一个了
      {
        bcache.buckets[i].head = 0;
      }
      else
      {
        res->next->prev = res->prev;
        res->prev->next = res->next;
        if(bcache.buckets[i].head == res) // 如果拿走的是其他桶的头节点
          bcache.buckets[i].head = res->prev; // 设置其他桶的头节点为随便一个即可
      }
      // 释放其他桶的锁
      release(&(bcache.buckets[i].lock));
      // 加入到当前桶
      acquire(&(bcache.buckets[idx].lock));
      if(bcache.buckets[idx].head == 0) // 当前桶中没有数据,也就是加进来的是第一个
      {
        res->prev = res;
        res->next = res;
        bcache.buckets[idx].head = res; // 由于是第一个,所以需要设置头
      }
      else
      {
        // 头插法
        res->prev = bcache.buckets[idx].head->prev;
        res->next = bcache.buckets[idx].head;
        bcache.buckets[idx].head->prev->next = res;
        bcache.buckets[idx].head->prev = res;
      }
      // 设置信息
      res->blockno = blockno;
      res->dev = dev;
      res->valid = 0; // 数据还未从磁盘中读取
      res->refcnt = 1;
      // 这里不设置时间,否则会引起死锁
      // acquire(&tickslock);
      // b->lasttime = ticks;
      // release(&tickslock);

      release(&(bcache.buckets[idx].lock));
      acquiresleep(&(res->lock));
      return res;
    }
    release(&(bcache.buckets[i].lock));
  }
  
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int idx = HASH(b->blockno);
  // 为了避免死锁,在这里获取ticks
  uint lasttime;
  acquire(&tickslock);
  lasttime = ticks;
  release(&tickslock);

  acquire(&(bcache.buckets[idx].lock));
  b->refcnt--;
  b->lasttime = lasttime;
  // 应该要放回桶内,但是由于buf已经在桶里了,所以不需要干啥

  release(&(bcache.buckets[idx].lock));
}

void
bpin(struct buf *b) {
  int idx = HASH(b->blockno);
  acquire(&(bcache.buckets[idx].lock));
  b->refcnt++;
  release(&(bcache.buckets[idx].lock));
}

void
bunpin(struct buf *b) {
  int idx = HASH(b->blockno);
  acquire(&(bcache.buckets[idx].lock));
  b->refcnt--;
  release(&(bcache.buckets[idx].lock));
}