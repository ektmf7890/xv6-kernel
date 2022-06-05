#include "types.h"
#include "x86.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"

rwlock_t*
get_rwlockt(void)
{
  struct proc* curproc = myproc();
  uint addr = curproc->tf->esp + 4;
  uint read_sz = 4; // 4byte pointer rwlock_t*

  if(addr >= curproc->ustack || addr + read_sz > curproc->ustack){
    return 0;
  }

  return (rwlock_t*)(addr);
}

int
sys_rwlock_init(void)
{
  rwlock_t* rwlock;
  
  /*if((rwlock = get_rwlockt()) == 0){
    return -1;
  }*/
  if(argptr(0, (char**)&rwlock, sizeof(rwlock)) < 0)
    return -1;

  return rwlock_init(rwlock);
}

int
sys_rwlock_acquire_readlock(void)
{
  rwlock_t* rwlock;
  
  if(argptr(0, (char**)&rwlock, sizeof(rwlock)) < 0)
    return -1;

//  cprintf("rwlock->reader:%d, rwlock->lock->name:%s\n", rwlock->readers, rwlock->lock.lock.name);

  return rwlock_acquire_readlock(rwlock);
}


int
sys_rwlock_acquire_writelock(void)
{
  rwlock_t* rwlock;
  
  /*if((rwlock = get_rwlockt()) == 0){
    return -1;
  }*/
  if(argptr(0, (char**)&rwlock, sizeof(rwlock)) < 0)
    return -1;

  return rwlock_acquire_writelock(rwlock);
}

int
sys_rwlock_release_readlock(void)
{
  rwlock_t* rwlock;
  
  if(argptr(0, (char**)&rwlock, sizeof(rwlock)) < 0)
    return -1;

  return rwlock_release_readlock(rwlock);
}

int
sys_rwlock_release_writelock(void)
{
  rwlock_t* rwlock;
  
  if(argptr(0, (char**)&rwlock, sizeof(rwlock)) < 0)
    return -1;
  return rwlock_release_writelock(rwlock);
}
