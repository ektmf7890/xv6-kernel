#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"

xem_t*
get_xemt(void)
{
  struct proc* curproc = myproc();
  uint addr = curproc->tf->esp + 4;
  uint read_sz = 4; // 4byte pointer xem_t*

  if(addr >= curproc->ustack || addr + read_sz > curproc->ustack){
    return 0;
  }

  return (xem_t*)(addr);
}

int 
sys_xem_init(void)
{
  int semaphore;
  
  /*if((semaphore = get_xemt()) == 0){
    return -1;
  }*/
  if(argint(0, &semaphore) < 0)
    return -1;

  return xem_init((xem_t*)semaphore);
}


int 
sys_xem_wait(void)
{
  int semaphore;
  
  /*if((semaphore = get_xemt()) == 0){
    return -1;
  }*/
  if(argint(0, &semaphore) < 0)
    return -1;

  return xem_wait((xem_t*)semaphore);
}


int 
sys_xem_post(void)
{
  int semaphore;
  
  /*if((semaphore = get_xemt()) == 0){
    return -1;
  }*/
  if(argint(0, &semaphore) < 0){
    cprintf("failed to call xem post\n");
    return -1;
  }

  return xem_post((xem_t*)semaphore);
}
