#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h" // user add
#include "proc.h" // user add

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  
  if(pte == 0 || (*pte & PTE_V) == 0)
  {
    if(is_lazypage(myproc(), va))
      return (uint64)lazyalloc(pagetable, va);
    else
      return 0;
  }
  if(is_cowpage(myproc(), va)) {
    return (uint64)cowalloc(pagetable, va);
  }
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  /*
   * 这里解释一下为什么下面用到了两个continue
   * 首先解释一下walk函数,这个函数会去寻找最后一级(level 0)的pte,它走到level 1时,拿到了level 1的pagetable,此时for循环执行完了
   * 然后从level 1的pagetable中依据27位索引中的最后9位索引找到了pte,并将该pte返回
   * 很明显在这个过程中,如果是walk函数的for循环中的pte是invalid,会返回0,那么uvmunmap中pte==0这个条件就有了,此时是因为没有分配,所以是continue
   * 如果for循环中的pte都找到了,而从level 1的pagetable中依据27位索引中的最后9位索引找到并返回的pte,有可能还未分配,但是它不为0,那么上面的条件就无法满足
   * 但是却满足了(*pte & PTE_V) == 0条件,所以此时也是未分配的,应该是continue
   * 下面举例子,首先看这个页表：
   * page table 0x0000000087f55000
     ..0: pte 0x0000000021fd3c01 pa 0x0000000087f4f000         (level 2)
     .. ..0: pte 0x0000000021fd4001 pa 0x0000000087f50000      (level 1)
     .. .. ..0: pte 0x0000000021fd445f pa 0x0000000087f51000   (level 0)
     .. .. ..1: pte 0x0000000021fd4cdf pa 0x0000000087f53000
     .. .. ..2: pte 0x0000000021fd900f pa 0x0000000087f64000
     .. .. ..3: pte 0x0000000021fd5cdf pa 0x0000000087f57000
     ..255: pte 0x0000000021fd5001 pa 0x0000000087f54000       (level 2)
     .. ..511: pte 0x0000000021fd4801 pa 0x0000000087f52000    (level 1)
     .. .. ..510: pte 0x0000000021fd58c7 pa 0x0000000087f56000 (level 0)
     .. .. ..511: pte 0x0000000020001c4b pa 0x0000000080007000
   * 1.若此时通过虚拟地址{000 0000 00}[00 0000 010](0 0000 0000) 0000 0000 0000来uvmunmap
   *   很明显能通过{000 0000 00}得到level 2的pte为..0: pte 0x0000000021fd3c01 pa 0x0000000087f4f000
   *   然后通过[00 0000 010]得知应该去获取level 1中索引为2的pte,但是页表中没有,只有索引为0的pte,此时就是walk的for循环中pte为invalid,此时walk函数返回0
   *   这样就到了pte==0,也就是说这个页面还未分配,需要continue,后面访问页面的时候再分配,而不是仅仅获取其pte就要去分配
   * 2.若此时通过虚拟地址{000 0000 00}[00 0000 000](0 0000 0100) 0000 0000 0000来uvmunmap
   *   很明显能通过{000 0000 00}得到level 2的pte为..0: pte 0x0000000021fd3c01 pa 0x0000000087f4f000
   *   能通过[00 0000 010]得到level 1的pte为.. ..0: pte 0x0000000021fd4001 pa 0x0000000087f50000
   *   此时walk函数的for循环结束,通过level 1的pagetable拿到索引为(0 0000 0100)也就是4的pte返回
   *   虽然在页表中level 1的pagetable没有索引为4的pte,但是walk函数还是拿到了索引为3的pte后面的内存地址上的值返回了
   *   返回的pte不为0,pte==0条件不满足,但是由于其是invalid,所以走到了(*pte & PTE_V) == 0条件,此时由于也是因为未分配引起的
   *   后面访问页面的时候再分配,而不是仅仅获取其pte就要去分配,所以仍然是continue
   * 同理uvmcopy也是这样
   */
  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      // panic("uvmunmap: walk");
      continue;
    if((*pte & PTE_V) == 0)
      // panic("uvmunmap: not mapped");
      continue;
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  // char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      // panic("uvmcopy: pte should exist");
      continue;
    if((*pte & PTE_V) == 0)
      // panic("uvmcopy: page not present");
      continue;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    // if((mem = kalloc()) == 0)
    //   goto err;
    // memmove(mem, (char*)pa, PGSIZE);
    
    flags &= (~PTE_W);
    // 增加物理页面引用计数,进行map
    if(irefcnt(pa) < 1 || mappages(new, i, PGSIZE, pa, flags | PTE_COW) != 0)
    {
      // kfree(mem);
      goto err;
    }
    else
    {
      *pte = ((*pte) & (~PTE_W)) | PTE_COW; // 父进程也清除PTE_W标志,设置PTE_COW标志
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);

    if(pa0 == 0)
      return -1;
    
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

/* user add: 为cow分配页面 */
void *cowalloc(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, 0); // 查找pte
  uint64 origin_pa = PTE2PA(*pte); // 结果一定是对齐的
  if(origin_pa == 0)
    return 0;
  int cnt;
  if((cnt = grefcnt(origin_pa)) == 1) // 页面引用次数为1,就直接设置为正常界面即可
  {
    *pte &= (~PTE_COW);
    *pte |= PTE_W;
    return (void *)origin_pa;
  }
  else if(cnt < 1)
  {
    return 0;
  }
  else
  {
    uint64 pa = (uint64)kalloc();
    if(pa == 0)
      return 0;
    // 将原始数据拷贝过去
    memmove((void *)pa, (void *)origin_pa, PGSIZE);
    *pte &= (~PTE_V); // mappages会找到pte,若不加这一句,就会panic说已经分配过了
    uint flags = (PTE_FLAGS(*pte) & (~PTE_COW)) | PTE_W;
    // 这里要是mappages失败了是不是应该drefcnt(origin_pa)减少的引用计数再加1呢?
    // 很显然是不需要的,因为mappages失败后进程就会退出,前面减少是对的
    if(mappages(pagetable, PGROUNDDOWN(va), PGSIZE, pa, flags) != 0)
    {
      kfree((void *)pa);
      *pte |= PTE_V; // 还原PTE_V
      return 0;
    }
    else
    {
      kfree((void *)origin_pa); // map成功后减少原始物理页面引用次数
      return (void *)pa;
    }
  }
}

/* user add: 为lazy分配页面 */
void *lazyalloc(pagetable_t pagetable, uint64 va)
{
  uint64 pa = (uint64)kalloc();
  if(pa == 0)
    return 0;
  memset((void *)pa, 0 ,PGSIZE);
  if(mappages(pagetable, PGROUNDDOWN(va), PGSIZE, pa, PTE_R | PTE_W | PTE_X | PTE_U) != 0)
  {
    kfree((void *)pa);
    return 0;
  }
  return (void *)pa;
}

/* user add: 判断是否是cow page */
int is_cowpage(struct proc *p, uint64 va)
{
  if(va >= MAXVA || va >= p->sz)
    return 0;
  pte_t *pte = walk(p->pagetable, va, 0); // 查找pte
  if(pte == 0 || (*pte & PTE_V) == 0)
    return 0;
  if(*pte & PTE_COW)
    return 1;
  else
    return 0;
}

/* user add: 判断是否是lazy page */
int is_lazypage(struct proc *p, uint64 va)
{
  if(va >= MAXVA)
    return 0;
  uint64 sp = PGROUNDUP(p->trapframe->sp) - 1;
  if(va <= sp || va >= p->sz)
    return 0;
  pte_t *pte = walk(p->pagetable, va, 0); // 查找pte
  if(pte == 0 || (*pte & PTE_V) == 0)
    return 1;
  else
    return 0;
}