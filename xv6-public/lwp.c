#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

extern void trapret(void);

int thread_create(thread_t* thread, void* (*start_routine) (void*), void* arg)
{
  struct proc *p;
  struct proc *curproc = myproc();
  uint sz;
  char* sp;

  p = find_unused();
  
  // Could not find an available proc structure
  if(!p){
    return 1;
  }

  p->state = EMBRYO;
  p->pid = -1;         // pid=-1 indicates that this is a LWP, not a normal process.
  p->lwpgroup = curproc;

  // Assign scheduling related fields.
  p->timequant = 1;
  p->level = curproc->level;

  // Allocate kernel stack (1page)
  if((p->kstack = kalloc())==0){
    return 1;
  }
  sp = p->kstack + KSTACKSIZE; // point sp to the top of the kernel stack
  
  // Leave room for trap frame 
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Build context
  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  
  // Allocate a user stack (2pages -> 1 for stack) 
  //cprintf("curproc->sz: 0x%x\n", PGROUNDUP(curproc->sz));
  sz = PGROUNDUP(curproc->sz);
  if((sz = allocuvm(curproc->pgdir, sz, sz + PGSIZE))==0){
    return 1; // not enough memory to allocate user stack for thread.
  }
  curproc->sz = sz;
  switchuvm(curproc);
  //cprintf("increased curproc->sz: 0x%x\n", curproc->sz);
  
  //clearpteu(curproc->pgdir, (char*)(sz-2*PGSIZE)); // Mark the guard page as inaccessible.
  sp = (char*)sz; // Make sp point to the newly allocated user stack.
  //cprintf("bottom of new user stack: 0x%x\n", (int)sp);

  // Make the new thread use the page table of the main thread.
  p->pgdir = curproc->pgdir;
  p->sz = curproc->sz;

  // Push argument value on the new thread's user stack.
  sp = (char*)((uint)(sp - sizeof(arg)) & ~3);
  *(int*)sp = (int)arg;
  
  // Push fake return address
  sp -= 4;
  //cprintf("top after pushing arg: %x\n", (int)sp);
  int fake_pc = 0xFFFFFFFF;
  if(copyout(curproc->pgdir, (uint)sp, (void*)(&fake_pc), 4) < 0){
    cprintf("failed to copyout fake return address\n");
    return 1; // Failed to write fake return address.
  }

  // The new thread will return to trapret, restoring these esp and eip values.
  // eip: start_routine, esp: user stack specific to this thread.
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)trapret;

  // Values restored by iret
  memset(p->tf, 0, sizeof *p->tf);
  // ss will be left as 0 (not sure if this is ok) -> turns out not ok (read about general protection exception)
  p->tf->ss = curproc->tf->ss;
  p->tf->esp = (uint)sp;
  // eflags will be all zero.
  p->tf->cs = curproc->tf->cs;
  p->tf->eip = (uint)start_routine;
  cprintf("tf->eip: %x\n", p->tf->eip);

  // Values restored in trapret
  p->tf->ds = curproc->tf->ds;
  p->tf->es = curproc->tf->es;
  p->tf->fs = curproc->tf->fs;
  p->tf->gs = curproc->tf->gs;
  // eax, ecx, edx, ebx, oesp, esi, edi will all be zero.
  p->tf->ebp = (uint)sz;
  
  if(curproc->thread_count == 1){
    init_next_t(p, curproc->pid);
    curproc->t_link = p;
    p->t_link = NULL;
  }
  else{
    add_thread(p);
  }
   
  p->thread_id = curproc->next_tid++;
  // Initialize thread_t
  thread->group_id = curproc->pid;
  thread->thread_id = p->thread_id;
  
  curproc->thread_count++;
  p->state = RUNNABLE;

  cprintf("created thread %d\n", p->thread_id);
  return 0;
}

void
thread_exit(void* retval)
{
  cprintf("entered exit\n");
  struct proc* p = myproc();
  struct proc* main_thread = p->lwpgroup;
  
  // Set to ZOMBIE status and deallocate in main thread with thread_join.
  p->state = ZOMBIE;
  p->tf->eax = (uint)retval;

  main_thread->thread_count--;
  rm_thread(p);
  
  // If main thread is waiting in thread_join, wake it up.
  if(main_thread->dont_sched){
    main_thread->dont_sched = 0;
  }

  main_thread->caller_isnt_yield = 1;
  acquire_ptable();
  //cprintf("main_thread-> state: %d, level: %d, pid: %d, dont_sched: %d\n", main_thread->state, main_thread->level, main_thread->pid, main_thread->dont_sched);
  mycpu()->proc = main_thread;
  swtch(&(p->context), main_thread->context);
}

int 
thread_join(thread_t thread, void** retval)
{
  struct proc* curproc = myproc();

  int tid = thread.thread_id;
  int gid = thread.group_id;
  struct proc* p;

  p = find_thread(tid, gid);
  
  // Could not find proc structure for this thread.
  if(!p){
    return 1;
  }

  // Fall into sleep if the thread has not exited yet.
  if(p->state != ZOMBIE){
    //cprintf("p->state!=zombit\n");

    // This process will not be selected in the scheduler
    // We dont set the status to sleeping because no other thread can be scheduled either if we do that.
    // This main thread will be scheduled back in when the thread exits.
    curproc->dont_sched = 1;
    curproc->state = RUNNABLE;
    curproc->caller_isnt_yield = 1;
    acquire_ptable();
    sched();
    if(is_holding_ptable())
      release_ptable();
  }

  // If thread has already exited, we save return value and deallocate the process.
  // save ret value
  *retval = (void*)p->tf->eax;
//  cprintf("retval: %d\n", *(int*)*retval);

  // deallocate kernel stack of this thread
  kfree(p->kstack);
  p->kstack = 0;

  //deallocate user stack of this thread
  uint sz = curproc->sz;
  if((sz = deallocuvm(curproc->pgdir, sz, sz - 2*PGSIZE)) == 0)
    return 1;
  curproc->sz = sz;
  switchuvm(curproc);
  
  p->pid = 0;
  p->lwpgroup = NULL;
  p->thread_id = 0;
  p->killed = 0;
  p->state = UNUSED;
  p->name[0] = 0;

  cprintf("reached end of trap_join\n");
  //cprintf("curproc->context: %x\n", &curproc->context);
  return 0;
}
