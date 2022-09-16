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
  struct spinlock lock;
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

  initlock(&bcache.lock, "bcache");

  // 初始化所有的buf
  for(b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    // 所有的buf前后都指向自己
    b->prev = b;
    b->next = b;
    initsleeplock(&(b->lock), "buffer");
  }
  
  // 把buf均摊给各个bucket
  int gap = NBUF / NUMBUCKET;
  int mod = NBUF % NUMBUCKET;
  // 哈希桶锁的名字
  char lockname[16] = {0};
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
      bcache.buckets[i].head = b++;
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
      bcache.buckets[i].head = b;
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // acquire(&bcache.lock);

  // Is the block already cached?
  int idx = HASH(blockno);
  acquire(&(bcache.buckets[idx].lock)); // 获取哈希桶的锁
  // 查找桶中是不是已经有了blockno
  for(b = bcache.buckets[idx].head; b != 0; b = b->next)
  {
    if(b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&(bcache.buckets[idx].lock)); // 这里必须要首先释放再去获取桶的锁,否则可能会死锁
      acquiresleep(&(b->lock));
      return b;
    }
  }

  // 没有找到cache,需要分配
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    if(b->refcnt == 0)
    {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->next = bcache.buckets[idx].head;

      release(&(bcache.buckets[idx].lock)); // 这里必须要首先释放再去获取桶的锁,否则可能会死锁
      acquiresleep(&(b->lock));
      return b;
    }
  }
  

  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
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

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


