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

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
  struct buf hashbuf[NBUCKET];
  struct spinlock bucketlock[NBUCKET];
} bcache;

char *bucketlockname[] = {"bcache00", "bcache01","bcache02","bcache03","bcache04","bcache05",
 "bcache06","bcache07","bcache08","bcache09","bcache10","bcache11","bcache12","bcache13"};

int hash(uint blockno) {
    return blockno % NBUCKET;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "eviction");

  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
  for (int i = 0; i < NBUCKET; i++) {
    bcache.hashbuf[i].prev = &bcache.hashbuf[i];
    bcache.hashbuf[i].next = &bcache.hashbuf[i];
  }
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->next = bcache.hashbuf[0].next;
    b->prev = &bcache.hashbuf[0];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbuf[0].next->prev = b;
    bcache.hashbuf[0].next = b;
  }

}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int bucketno = hash(blockno);

  acquire(&bcache.bucketlock[bucketno]);

  // Is the block already cached?
  for(b = bcache.hashbuf[bucketno].next; b != &bcache.hashbuf[bucketno]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucketlock[bucketno]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucketlock[bucketno]);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  acquire(&bcache.lock);
  

  struct buf *LRUblock = 0;
  int ebucketno = -1;
  for (int i = 0; i < NBUCKET; i++) {
        int flag = 0;
        int newno = -1;
        acquire(&bcache.bucketlock[i]);
        for(b = bcache.hashbuf[i].next; b != &bcache.hashbuf[i]; b = b->next) {
            if (b->refcnt == 0 && (LRUblock == 0 || (b->ticks < LRUblock->ticks))) {
                LRUblock = b;
                flag = 1;
                newno = i;
            }
        }
        if (flag == 0) {
            release(&bcache.bucketlock[i]);
        } else {
            if (newno != ebucketno && ebucketno != -1) {
                release(&bcache.bucketlock[ebucketno]);
            }
            ebucketno = newno;
        }
  }
  if (LRUblock == 0) {
      panic("bget: no buffers");
  }

  //eviction
  if (ebucketno != bucketno) {
    LRUblock->next->prev = LRUblock->prev;
    LRUblock->prev->next = LRUblock->next;
    release(&bcache.bucketlock[ebucketno]);
    acquire(&bcache.bucketlock[bucketno]);
    LRUblock->next = bcache.hashbuf[bucketno].next;
    LRUblock->prev = &bcache.hashbuf[bucketno];
    bcache.hashbuf[bucketno].next->prev = LRUblock;
    bcache.hashbuf[bucketno].next = LRUblock;
  }
  LRUblock->dev = dev;
  LRUblock->blockno = blockno;
  LRUblock->valid = 0;
  LRUblock->refcnt = 1;
  release(&bcache.bucketlock[bucketno]);
  release(&bcache.lock);
  acquiresleep(&LRUblock->lock);
  return LRUblock;
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

  int bucketno = hash(b->blockno);
  acquire(&bcache.bucketlock[bucketno]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
    b->ticks = ticks;
  }
  
  release(&bcache.bucketlock[bucketno]);
}

void
bpin(struct buf *b) {
  int bucketno = hash(b->blockno);

  acquire(&bcache.bucketlock[bucketno]);
  b->refcnt++;
  release(&bcache.bucketlock[bucketno]);
}

void
bunpin(struct buf *b) {
  int bucketno = hash(b->blockno);

  acquire(&bcache.bucketlock[bucketno]);
  b->refcnt--;
  release(&bcache.bucketlock[bucketno]);
}


