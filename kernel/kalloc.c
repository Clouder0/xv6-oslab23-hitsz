// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);
void freerange_cpu(void *pa_start, void *pa_end, int cpu_id);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

struct mem
{
  struct spinlock lock;
  struct run *freelist;
  uint64 freenum;
} kmem[NCPU];

void freerange_lockfree(uint64 s, uint64 e, struct mem *m)
{
  m->freenum += (e - s) / PGSIZE;
  for (uint64 p = s; p + PGSIZE <= e; p += PGSIZE)
  {
    struct run *r = (struct run *)p;
    r->next = m->freelist;
    m->freelist = r;
  }
}

void kinit()
{
  for (int i = 0; i < NCPU; ++i)
    initlock(&kmem[i].lock, "kmem");
  int avail_cpus[NCPU], cpu_num = 0;
  for (int i = 0; i < NCPU; ++i)
    if (cpus[i].available)
      avail_cpus[cpu_num++] = i;
  uint64 start = PGROUNDUP((uint64)end);
  uint64 len = (PHYSTOP - start) / cpu_num;
  uint64 now_start = start, now_end = start + len;
  memset((void *)start, 1, PHYSTOP - start); // fill with junk at once
  for (int i = 0; i < cpu_num - 1; ++i)
  {
    freerange_lockfree(now_start, now_end, &kmem[avail_cpus[i]]);
    now_start += len, now_end += len;
  }
  // special for last cpu
  freerange_lockfree(now_start, PHYSTOP, &kmem[avail_cpus[cpu_num - 1]]);
}

void kfree_cpu(void *pa, int cpu_id)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
  {
    printf("%p\n", pa);
    panic("kfree");
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem[cpu_id].lock);
  r->next = kmem[cpu_id].freelist;
  kmem[cpu_id].freelist = r;
  kmem[cpu_id].freenum++;
  release(&kmem[cpu_id].lock);
}

void freerange_cpu(void *pa_start, void *pa_end, int cpu_id)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree_cpu(p, cpu_id);
}

void freerange(void *pa_start, void *pa_end)
{
  freerange_cpu(pa_start, pa_end, cpuid());
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  kfree_cpu(pa, cpuid());
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc_cpu(int cpu_id)
{
  struct run *r;

  acquire(&kmem[cpu_id].lock);
  r = kmem[cpu_id].freelist;
  if (r)
  {
    kmem[cpu_id].freelist = r->next;
    kmem[cpu_id].freenum--;
  }
  release(&kmem[cpu_id].lock);

  if (!r)
  {
    // steal!
    int max_free = kmem[0].freenum, max_free_id = 0;
    for (int i = 1; i < NCPU; ++i)
      if (kmem[i].freenum > max_free)
        max_free = kmem[i].freenum, max_free_id = i;
    if (max_free_id == cpu_id || max_free == 0)
      return (void *)r;              // nothing to steal, return NULL
    int s = cpu_id, b = max_free_id; // small and big, to avoid deadlock
    if (cpu_id > max_free_id)
      s = max_free_id, b = cpu_id;
    acquire(&kmem[s].lock);
    acquire(&kmem[b].lock);
    int steal_num = kmem[max_free_id].freenum / 2;
    // printf("steal %d from %d to %d\n", steal_num, max_free_id, cpu_id);
    kmem[max_free_id].freenum -= steal_num;
    kmem[cpu_id].freenum += steal_num;
    for (int i = 0; i < steal_num; ++i)
    {
      struct run *p = kmem[max_free_id].freelist;

      kmem[max_free_id].freelist = p->next;

      p->next = kmem[cpu_id].freelist;
      kmem[cpu_id].freelist = p;
    }
    if (kmem[cpu_id].freelist)
    {
      r = kmem[cpu_id].freelist;
      kmem[cpu_id].freelist = r->next;
      kmem[cpu_id].freenum--;
    }
    release(&kmem[b].lock);
    release(&kmem[s].lock);
  }
  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}

void *kalloc(void)
{
  return kalloc_cpu(cpuid());
}