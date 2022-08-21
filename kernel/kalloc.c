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
} kmem;

// user add: 物理页面引用计数
struct refcnt {
  struct spinlock lock;
  int cnt[PHYSTOP / PGSIZE];
} pageref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pageref.lock, "pageref");
  freerange(end, (void*)PHYSTOP);
  memset(&pageref, 0, sizeof(struct refcnt));
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

  // 减少一个引用后,再判断引用是否仍然大于0
  int idx = ((uint64)pa) / PGSIZE;
  acquire(&pageref.lock);
  if((--pageref.cnt[idx]) > 0)
  {
    release(&pageref.lock);
    return;
  }
  pageref.cnt[idx] = 0; // 设置引用为0
  release(&pageref.lock);

  // 下面要去释放内存
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
  {
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&pageref.lock);
    pageref.cnt[(uint64)r / PGSIZE] = 1; // 设置引用为1
    release(&pageref.lock);
  }
  return (void*)r;
}

/* user add: 增加物理页面引用计数 */
int irefcnt(uint64 addr)
{
  if(addr % PGSIZE != 0 || (char *)addr < end || addr >= PHYSTOP)
    return -1;
  int idx = grefcnt(addr);
  if(idx < 0)
    return -1;
  idx = addr / PGSIZE;
  acquire(&pageref.lock);
  pageref.cnt[idx] += 1;
  release(&pageref.lock);
  return pageref.cnt[idx];
}

/* user add: 设置物理页面引用计数 */
int srefcnt(uint64 addr, int cnt)
{
  if(addr % PGSIZE != 0 || (char *)addr < end || addr >= PHYSTOP || cnt < 1)
    return -1;
  int idx = addr / PGSIZE;
  acquire(&pageref.lock);
  pageref.cnt[idx] = cnt;
  release(&pageref.lock);
  return cnt;
}

/* user add: 获取物理页面引用计数 */
int grefcnt(uint64 addr)
{
  if(addr % PGSIZE != 0 || (char *)addr < end || addr >= PHYSTOP)
    return -1;
  int idx = addr / PGSIZE;
  acquire(&pageref.lock);
  idx = pageref.cnt[idx];
  release(&pageref.lock);
  return idx;
}