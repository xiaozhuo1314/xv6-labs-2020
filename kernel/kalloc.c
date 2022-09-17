// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmems[NCPU];

void
kinit()
{
  char lockname[8] = {0};
  for(int i = 0; i < NCPU; ++i)
  {
    snprintf(lockname, sizeof(lockname), "kmem_%d", i);
    initlock(&kmems[i].lock, lockname);
  }
  freerange(end, (void*)PHYSTOP); // 这里是将所有的内存都给了0号cpu
}

void kfree_new(void *pa, int id)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree_new");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmems[id].lock); // 获得对应的空闲列表的锁
  r->next = kmems[id].freelist;
  kmems[id].freelist = r;
  release(&kmems[id].lock); // 释放对应的空闲列表的锁
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  // for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  //   kfree(p);
  uint64 gap = (pa_end - pa_start) / NCPU; // 每个cpu能够初始时拿到多少内存
  gap = PGROUNDDOWN(gap); // 向下对齐
  char *end_pa; // 每一个cpu能够初始时分配的内存的终止位置,这里进行初始化
  for(int i = 0; i < NCPU; ++i)
  {
    if(i == NCPU - 1) // 最后一个cpu应该让其把剩下的全包了
      end_pa = (char*)pa_end;
    else // 不是最后一个cpu的话就设置其终止位置为起始位置+能够拿到的内存
      end_pa = p + gap;
    for(; p + PGSIZE <= end_pa; p += PGSIZE)
      kfree_new(p, i);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // 关中断
  push_off();

  //获得cpuid
  int id = cpuid();
  acquire(&kmems[id].lock); // 获得对应的空闲列表的锁
  r->next = kmems[id].freelist;
  kmems[id].freelist = r;
  release(&kmems[id].lock); // 释放对应的空闲列表的锁

  // 开中断
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // 关中断
  push_off();
  //获得cpuid
  int id = cpuid();
  acquire(&kmems[id].lock);
  r = kmems[id].freelist;
  if(r) // 如果本cpu的空闲列表有空闲页
  {
    kmems[id].freelist = r->next;
    release(&kmems[id].lock); // 这边不能忘了释放
  }
  else // 如果本cpu的空闲列表没有空闲页,需要去其他cpu夺取
  {
    /* 
     * 获取其他cpu空闲页前需要先释放自己的锁,否则持有自己的锁,其他人也持有自己的锁,互相获取对方锁的时候会造成死锁
     * 但其实先释放自己的锁看似也有一个小问题,就是该cpu上的进程发现当前cpu没有空闲页了,释放了当前cpu的锁,此时另一个进程被调度进入了当前cpu
     * 将持有的页释放了,那么当前cpu就有空闲页了,但是上一个进程已经到了要去其他cpu抢内存页的流程,不会再去判断当前cpu有没有空闲页了
     * 如果其他cpu都没有空闲页,那么上一个进程就无功而返,但其实此时应该是有空闲页的
     * 不过这种问题不会发生,因为上面已经关闭了中断,进程在执行的时候不会被调度出去,也就不会存在另一个进程释放内存页的行为
     * 只有当当前进程跑完了,另一个进程才能进入当前cpu释放内存页,获取进入其他cpu释放内存页
     */
    release(&kmems[id].lock);
    for(int i = 0; i < NCPU; ++i)
    {
      if(i == id)
        continue;
      // 获取其他cpu的锁
      acquire(&kmems[i].lock);
      r = kmems[i].freelist;
      if(r)
      {
        kmems[i].freelist = r->next;
        // 释放其他cpu的锁
        release(&kmems[i].lock);
        break;
      }
      // 释放其他cpu的锁
      release(&kmems[i].lock);
    }
  }

  // 开中断
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
