#include "defs.h"
#include "types.h"
#include "param.h"

int
xem_init(xem_t* semaphore)
{
  semaphore->value = 1;
  semaphore->chan = 0;
  initlock(&semaphore->lock, "semaphore");
}

int 
xem_wait(xem_t* semaphore)
{
  acquire(&semaphore->lock);
  if(semaphore->value <= 0){
    sleep(&semaphore->chan, &semaphore->lock);
  }
  semaphore->value--;
  release(&semaphore->lock);
}

int xem_post(xem_t* semaphore)
{
  acquire(&semaphore->lock);
  semaphore->value++;
  wakeup(&semaphore->chan);
  release(&semaphore->lock);
}
