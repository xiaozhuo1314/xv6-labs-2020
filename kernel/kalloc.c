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

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
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
  }
  else // 如果本cpu的空闲列表没有空闲页,需要去其他cpu夺取
  {
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
  release(&kmems[id].lock);

  // 开中断
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
