#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

extern void trapret(void);
extern void forkret(void);
extern pte_t* walkpgdir(pde_t*, const void*, int);

int thread_create(thread_t* thread, void* (*start_routine) (void*), void* arg)
{
  struct proc *p;
  struct proc *curproc = myproc();
  //uint sz;
  char* sp;

  p = find_unused();
  
  // Could not find an available proc structure
  if(!p){
    return -1;
  }

  acquire_ptable();

  p->state = EMBRYO;
  p->pid = -1;         // pid=-1 indicates that this is a LWP, not a normal process.
  p->lwpgroup = curproc;
  p->waiting_tid = -1;
  p->parent = 0;

  p->timequant = 1;
  p->s_link = NULL;
  p->share = 0;
  p->stride = -1;
  p->pass = 0;

  // Allocate kernel stack (1page)
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return -1;
  }
  sp = p->kstack + KSTACKSIZE; // point sp to the top of the kernel stack
  
  // Leave room for trap frame 
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  sp -= 4;
  *(uint*)sp = (uint)trapret;

  // Build context
  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  
  // Allocate a page in address space for this thread's ustack
  struct proc* pptr = curproc;
  pte_t* pte;
  uint sz;
  while(pptr){
    pte = walkpgdir(curproc->pgdir, (char*)pptr->ustack, 0);
    if(!(*pte & PTE_P)){
      p->ustack = pptr->ustack + PGSIZE;
      if((sz = allocuvm(curproc->pgdir, p->ustack - PGSIZE, p->ustack)) == 0){
        kfree(p->kstack);
        p->kstack = 0;
        p->state = UNUSED;
        return -1;
      }
      break;
    }
    pptr = pptr->t_link;
  }
  if(!pptr){
    cprintf("could not find space for stack\n");
    return -1;
  }
  /*uint sz;
  p->ustack = curproc->ustack + (curproc->next_tid * PGSIZE);
  if((sz = allocuvm(curproc->pgdir, p->ustack - PGSIZE, p->ustack)) == 0){
    kfree(p->kstack);
    p->kstack = 0;
    p->state = UNUSED;
  }*/
  //cprintf("allocated ustack for thread %d at %d\n", curproc->thread_count, p->ustack);
  curproc->sz = sz;
  switchuvm(curproc);
  
  /*if((sz = allocuvm(curproc->pgdir, sz, sz + PGSIZE))==0){
    kfree(p->kstack);
    p->kstack = 0;
    p->state = UNUSED;
    return -1; // not enough memory to allocate user stack for thread.
  }*/

  int i;
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      p->ofile[i] = filedup(curproc->ofile[i]);
  p->cwd = curproc->cwd;

  safestrcpy(p->name, curproc->name, sizeof(curproc->name));
  
  sp = (char*)(p->ustack); // Make sp point to the newly allocated user stack.
  // Push argument value on the new thread's user stack.
  sp = (char*)((uint)(sp - sizeof(arg)) & ~3);
  *(int*)sp = (int)arg;
  
  // Push fake return address
  sp -= 4;
  int fake_pc = 0xFFFFFFFF;
  *(int*)sp = fake_pc;

  p->pgdir = curproc->pgdir;
  p->sz = curproc->sz;

  // The new thread will return to trapret, restoring these esp and eip values.
  // eip: start_routine, esp: user stack specific to this thread.
  
  // Values restored by iret
  *p->tf = *curproc->tf;
  p->tf->esp = (uint)sp;
  p->tf->eip = (uint)start_routine;
  p->tf->ebp = (uint)p->ustack;

  //cprintf("thread's esp value: %d\n", p->tf->esp);
  //cprintf("return address: %x, arg: %x\n", *(int*)(p->tf->esp), *(int*)(p->tf->esp + 4));
  
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
  
  release_ptable();
  return 0;
}

void
thread_exit(void* retval)
{
  struct proc* p = myproc();
  struct proc* main_thread = p->lwpgroup;
  
  // Set to ZOMBIE status and deallocate in main thread with thread_join.
  acquire_ptable(); 
  p->state = ZOMBIE;
  p->retval = (int)retval;
 
  main_thread->thread_count--;
  rm_thread(p);
  
  // If main thread is waiting in thread_join, wake it up.
  if(main_thread->waiting_tid == p->thread_id){
    main_thread->waiting_tid = -1;
  }
  
  uint sz;
  //deallocate user stack of this thread
  if((sz = deallocuvm(main_thread->pgdir, p->ustack, p->ustack-PGSIZE)) == 0){
    cprintf("failed to deallocate ustack at thread exit\n");
  }
  main_thread->sz = sz;

  //cprintf("thread %d exit with retval: %d\n", p->thread_id, (int)p->retval);
  if(thread_swtch(&p->context, main_thread) == -1){
    sched();
  }
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
    return -1;
  }

  acquire_ptable();
  // Fall into sleep if the thread has not exited yet.
  if(p->state != ZOMBIE){
    curproc->waiting_tid = tid;
    curproc->state = RUNNABLE;
    release_ptable();
    //cprintf("calling yield in thread_join tid%d\n", tid);
    yield();
    acquire_ptable();
  }

  // save ret value
  *retval = (void*)p->retval;

  // deallocate kernel stack of this thread
  kfree(p->kstack);
  p->kstack = 0;

  
 /* int fd;
  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      fileclose(p->ofile[fd]);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;
  */
  p->pid = 0;
  p->lwpgroup = NULL;
  p->thread_id = 0;
  p->killed = 0;
  p->state = UNUSED;
  p->name[0] = 0;
  p->t_link = 0;
  p->s_link = 0;
  p->pgdir = 0;

  release_ptable();

  return 0;
}
