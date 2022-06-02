#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"

int
xem_init(xem_t* semaphore)
{
  semaphore->value = 1;
  semaphore->chan = 0;
  initlock(&semaphore->lock, "semaphore");
  return 0;
}

int 
xem_wait(xem_t* semaphore)
{
  acquire(&semaphore->lock);
  while(semaphore->value <= 0){
    //cprintf("thread %d sleep\n", myproc()->thread_id);
    sleep(&semaphore->chan, &semaphore->lock);
  }
  //cprintf("thread %d acquired lock\n", myproc()->thread_id);
  semaphore->value--;
  release(&semaphore->lock);
  return 0;
}

int 
xem_post(xem_t* semaphore)
{
  //cprintf("sem post\n");
  acquire(&semaphore->lock);
  semaphore->value++;
  wakeup_one(&semaphore->chan);
  //cprintf("thread %d releasing lock\n", myproc()->thread_id);
  release(&semaphore->lock);
  return 0;
}
