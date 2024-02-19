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

#define MOD 13
struct
{
  struct spinlock locks[MOD];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf heads[MOD];
  int freenum[MOD];
} bcache;

void push_front(struct buf *head, struct buf *node)
{
  node->prev = head, node->next = head->next;
  head->next->prev = node;
  head->next = node;
}

void push_back(struct buf *head, struct buf *node)
{
  node->prev = head->prev, node->next = head;
  head->prev->next = node, head->prev = node;
}

struct buf *pop(struct buf *head)
{
  struct buf *b;
  for (b = head->prev; b != head; b = b->prev)
  {
    if (b->refcnt == 0)
    {
      b->prev->next = b->next;
      b->next->prev = b->prev;
      return b;
    }
  }
  return 0;
}

void binit(void)
{
  struct buf *b;

  for (int i = 0; i < MOD; ++i)
    initlock(&bcache.locks[i], "bcache"), bcache.freenum[i] = 0;

  // Create linked list of buffers
  for (int i = 0; i < MOD; ++i)
    bcache.heads[i].prev = &bcache.heads[i], bcache.heads[i].next = &bcache.heads[i];
  for (int i = 0; i < NBUF; i++)
  {
    b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    push_front(&bcache.heads[i % MOD], b);
    ++bcache.freenum[i % MOD];
  }
  printf("init done");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;
  int p = blockno % MOD;
  // printf("try bget dev %d blockno %d, bget %d, free %d\n", dev,blockno, p, bcache.freenum[p]);
  acquire(&bcache.locks[p]);
  // printf("dev %d blockno %d, bget %d, free %d\n", dev,blockno, p, bcache.freenum[p]);

  // Is the block already cached?
  for (b = bcache.heads[p].next; b != &bcache.heads[p]; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      bcache.freenum[p] -= (b->refcnt == 0);
      // printf("hit cache, dev %d blockno %d, bget %d, free %d\n", dev,blockno, p, bcache.freenum[p]);
      b->refcnt++;
      release(&bcache.locks[p]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.

  if (bcache.freenum[p] == 0)
  {
    // printf("no free\n");
    // steal!
    int maxfree = 0, maxfreeid = 0;
    for (int i = 0; i < MOD; ++i)
    {
      if (bcache.freenum[i] > maxfree)
        maxfree = bcache.freenum[i], maxfreeid = i;
    }
    if (maxfree == 0)
    {
      panic("No buffer available");
    }
    acquire(&bcache.locks[maxfreeid]);
    int steal_num = bcache.freenum[maxfreeid] / 2;
    // printf("steal %d from %d to %d\n", steal_num, maxfreeid, p);
    bcache.freenum[p] += steal_num, bcache.freenum[maxfreeid] -= steal_num;
    for (int i = 0; i < steal_num; ++i)
    {
      push_back(&bcache.heads[p], pop(&bcache.heads[maxfreeid]));
    }
    release(&bcache.locks[maxfreeid]);
  }

  for (b = bcache.heads[p].prev; b != &bcache.heads[p]; b = b->prev)
  {
    if (b->refcnt == 0)
    {
      bcache.freenum[p]--;
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.locks[p]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid)
  {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int p = b->blockno % MOD;
  acquire(&bcache.locks[p]);
  b->refcnt--;
  if (b->refcnt == 0)
  {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.heads[p].next;
    b->prev = &bcache.heads[p];
    bcache.heads[p].next->prev = b;
    bcache.heads[p].next = b;
    bcache.freenum[p]++;
  }

  release(&bcache.locks[p]);
}

void bpin(struct buf *b)
{
  int p = b->blockno % MOD;
  acquire(&bcache.locks[p]);
  b->refcnt++;
  release(&bcache.locks[p]);
}

void bunpin(struct buf *b)
{
  int p = b->blockno % MOD;
  acquire(&bcache.locks[p]);
  b->refcnt--;
  release(&bcache.locks[p]);
}
