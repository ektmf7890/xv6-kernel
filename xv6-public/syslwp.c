#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_thread_create(void)
{
  thread_t* thread;
  void* (*start_routine) (void*);
  void* arg;

  if(argptr(0, (char**)&thread, sizeof(thread)) < 0)
    return -1;
  if(argptr(1, (char**)&start_routine, 4) < 0)
    return -1;
  if(argptr(2, (char**)&arg, 4) < 0)
    return -1;

  return thread_create(thread, start_routine, arg);
}

int sys_thread_exit(void)
{
  void* retval;
  if(argptr(0, (char**)&retval, 4) < 0)
    return -1;

  thread_exit(retval);

  return 0;
}

int sys_thread_join(void)
{
  thread_t thread;
  void** retval;

  struct proc* curproc = myproc();
  
  // Read read_sz bytes from the process's user stack at addr.
  uint addr = curproc->tf->esp + 4;
  uint read_sz = sizeof(thread);
  
  if(addr >= curproc->ustack || addr + read_sz > curproc->ustack)
    return -1;
  
  thread = *(thread_t*)(addr);

  addr += read_sz;
  read_sz = 4;
  
  if(addr >= curproc->ustack || addr + read_sz > curproc->ustack)
    return -1;

  retval = *(void***)(addr);

  return thread_join(thread, retval);
}

