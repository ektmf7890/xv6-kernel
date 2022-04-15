#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

void procdump(void);

// protected data for scheduling tasks
struct{
  uint priboosttime;         // Counts num of ticks passed since last pri boost. Checked at process yield.
  int mlfq_pass;             // Pass value of mlfq processes
  int mlfq_stride;           // Stride value of mlfq processes -> changes whenever set_cpu_share called
  int stride_share;          // Sum of CPU share non-lmfq processes are occupying.
  struct proc* stride_head;  // Linked list of processes in stride queue.
  int qlevels[3];            // Number of processes in each mlfq level.
//  struct proc* mlfq_head;    // Linked list of processes in mlfq.
  struct spinlock lock;
}mlfqstr;

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// remove a process from mlfq when its state changes from RUNNABLE/RUNNING to different state
// ptable lock should be acquired in caller
void 
mlfqrm(struct proc* p){
  acquire(&mlfqstr.lock);
  if(p->level >= 0 && p->level <= 2)
    mlfqstr.qlevels[p->level]--;

  // remove from the linked list, mlfq_procs
  /*if(mlfqstr.mlfq_head->pid == p->pid) // head itself is the process to remove
    mlfqstr.mlfq_head = mlfqstr.mlfq_head->next;
  else{
    struct proc* pptr = mlfqstr.mlfq_head;
    while(pptr){
      if(pptr->next->pid == p->pid){
        pptr->next = p->next;
        break;
      }
      pptr = pptr->next;
    }
  }
  p->next = NULL;
  */
  //cprintf("level2: %d, level1: %d, level0: %d\n", mlfqstr.qlevels[2], mlfqstr.qlevels[1], mlfqstr.qlevels[0]);
  release(&mlfqstr.lock);
}

// add a process to mlfq when its status is changed to RUNNABLE
// ptable lock should be acquired in caller
void 
mlfqadd(struct proc* p)
{
  acquire(&mlfqstr.lock);
  if(p->level >= 0 && p->level <= 2)
    mlfqstr.qlevels[p->level]++;
  
  // add process to front of linked list, mlfq_procs
  /*
  p->next = mlfqstr.mlfq_head;
  mlfqstr.mlfq_head = p;
  */
  //cprintf("level2: %d, level1: %d, level0: %d\n", mlfqstr.qlevels[2], mlfqstr.qlevels[1], mlfqstr.qlevels[0]);
  release(&mlfqstr.lock);
}
  

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&mlfqstr.lock, "mlfqstr");
  
  // initialize values in mlfqstr
  acquire(&mlfqstr.lock);
  mlfqstr.priboosttime = 0;
  mlfqstr.qlevels[0] = mlfqstr.qlevels[1] = mlfqstr.qlevels[2] = 0;
  //mlfqstr.mlfq_head = NULL;
  mlfqstr.stride_head = NULL;
  mlfqstr.stride_share = 0;
  mlfqstr.mlfq_pass = 0;
  mlfqstr.mlfq_stride = 0;
  release(&mlfqstr.lock);
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli(); // disable interrupt
  c = mycpu();
  p = c->proc;
  popcli(); // enable interrupt
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

   //cprintf("allocproc\n");
  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  //cprintf("allocproc");
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // Assign to level 2
  p->level = 2;
  p->timequant = 1;
  p->timeallot = 5;
  p->next = NULL; 
 // cprintf("\nEmbryo process made(pid: %d)\n", p->pid);

  //cprintf("allorproc2");
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}


//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
   //cprintf("userinit\n");
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  mlfqadd(p);

  //cprintf("userinit");
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)

{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

   //cprintf("fork`\n");
  acquire(&ptable.lock);

  np->state = RUNNABLE;
  if(np->level != -1)
    mlfqadd(np);

 // cprintf("fork\n");
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

   //cprintf("exit\n");
  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  if(curproc->level != -1)
    mlfqrm(curproc);
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
   //cprintf("wait\n");
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        //cprintf("wait\n");
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      //cprintf("wait\n");
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void 
scheduler(void)
{
   //cprintf("First enter scheduler\n");
  struct cpu * c = mycpu();
  struct proc * searchidx = ptable.proc;
  c->proc = 0;

  for(;;){
    // Enable interrupts on this processor
    sti();

     //cprintf("scheduler\n");
    acquire(&ptable.lock);

    struct proc * newproc = NULL;

    if(mlfqstr.stride_head){
      int min_pass = mlfqstr.mlfq_pass;

      struct proc * sp = mlfqstr.stride_head;
      while(sp){
        if(sp->pass < min_pass){
          min_pass = sp->pass;
          newproc = sp;
        }
        sp = sp->next;
      }

      // if mlfq has the smallest pass value
      if(newproc) goto contextswitch;      
    }
    
    acquire(&mlfqstr.lock);
    // find the level to select from
    int level;
    if (mlfqstr.qlevels[2] > 0) level = 2;
    else if (mlfqstr.qlevels[1] > 0) level = 1;
    else level = 0;
    release(&mlfqstr.lock);

    // search the ptable for a runnable procee, from the mlfq, with the correct level.
    // a process in the stride queue wont be selected because their level values are -1
     //cprintf("entering process search loop\n");
    
    // we search at most 64 processes (loop through the entire ptable)
    // before giving up and ending the search.
    int cnt = 0;
    struct proc* p;
    while((p = searchidx)){
      if(p->state == RUNNABLE && searchidx->level == level)
        newproc = searchidx;

      if(searchidx < &ptable.proc[NPROC])
        searchidx ++;
      else
        searchidx = ptable.proc;

      if(p->state == RUNNABLE && p->level == level){
        newproc = p;
        break;
      }

      if(cnt >= 63) break;
    }
     //cprintf("broke out of process search loop\n");

contextswitch:
    c->proc = newproc;
    switchuvm(newproc);
    newproc->state = RUNNING;
    
    // cprintf("switching to process pid %d\n", newproc->pid);
    swtch(&(c->scheduler), newproc->context);
    
    switchkvm();
    c->proc = 0;

    release(&ptable.lock);
  }
}

/*void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Running processes in ptable in Round-Robin fashion.
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      acquire(&mlfqstr.lock);
      int level;
      if (mlfqstr.qlevels[2] > 0) level = 2;
      else if (mlfqstr.qlevels[1] > 0) level = 1;
      else level = 0;
      release(&mlfqstr.lock);
      
      if(!(p->state == RUNNABLE &&  p->level == level))
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      setprocstate(p, RUNNING);

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}*/

// Enter scheduler.  Must hold only pable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

void
priboost(){
  struct proc* p;
  for(p=ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNABLE){
      mlfqstr.qlevels[p->level]--;
      p->level = 2;
      p->timequant = 1;
      p->timeallot = 5;
      mlfqstr.qlevels[2]++;
    }
  }
}

void lowerlevel(struct proc* p){
  if (p->level < 1 || p->level > 2){
    return;
  }

  // no need to acquire mlfqstr's lock. (done in priboost)
  mlfqstr.qlevels[p->level]--;
  
  p->level--;
  switch(p->level){
    case 1:
      p->timequant = 2;
      p->timeallot = 10;
      break;
    case 0:
      p->timequant = 4;
      break;
  }
  
  mlfqstr.qlevels[p->level]++;
  
  p->tickcount = 0;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
   //cprintf("yield\n");
  acquire(&ptable.lock);  //DOC: yieldlock

  struct proc* curproc = myproc();
  
  uint tickcount = curproc->tickcount++;
  uint level = curproc->level;

  // Check if this process has used up its time allotment
  // We skip this part for stride processes (their level values are -1)
  int lowered = 0;
  if(level > 0 && (tickcount >= curproc->timeallot)){
    // used up its time allot
    //cprintf("\nqlevels before lowerlevel(): {%d, %d, %d}\n", mlfq.qlevels[0], mlfq.qlevels[1], mlfq.qlevels[2]);
    lowerlevel(curproc);
    lowered = 1;
   // cprintf("\n\nUsed Time Allot(pid: %d, level:%d)\ncurrent qlevels: {%d, %d, %d}\n\n", myproc()->pid, myproc()->level, mlfqstr.qlevels[0], mlfqstr.qlevels[1], mlfqstr.qlevels[2]);
  }
  
  // Priority Boost
  acquire(&tickslock);
  if(ticks - mlfqstr.priboosttime >= 100){
    mlfqstr.priboosttime = ticks;
    priboost();
   // cprintf("\nPriboost\n");
  }
  release(&tickslock);

  acquire(&mlfqstr.lock);
  // If the stride queue is not empty, we have to increment pass values every time a process yields
  if(mlfqstr.stride_head){
    // if the yielding process is in the stride queue
    if(level == -1) curproc->pass += curproc->stride;

    // if the yielding process is in the mlfq
    else mlfqstr.mlfq_pass += mlfqstr.mlfq_stride;
  }
  release(&mlfqstr.lock);
  
  // Do not call scheduler if it hasn't used up its time quantum
  // Processes in stride queue will always call the scheduler because their time quantum is 1.
  if(lowered || tickcount % curproc->timequant == 0){
    curproc->state = RUNNABLE;
    sched();
  }

  //cprintf("yield");
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
   //cprintf("forkret\n");
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
     //cprintf("sleep (pid:%d)\n", p->pid);
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  if(p->level != -1)
    mlfqrm(p);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    //cprintf("sleep\n");
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
     // cprintf("wakeup(pid: %d)\n", p->pid);
      if(p->level != -1)
        mlfqadd(p);
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
   //cprintf("wakeup\n");
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

   //cprintf("kill\n");
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        if(p->level != -1)
          mlfqadd(p);
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s level%d", p->pid, state, p->name, p->level);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int 
getlev(void){
  int level = myproc()->level;
  if (level < 0 || level > 2)
    return -1;
  else
    return level;
}

int 
set_cpu_share(int share)
{
   //cprintf("setcpushare\n");
  acquire(&ptable.lock);
  acquire(&mlfqstr.lock);

  if(mlfqstr.stride_share + share > 80)
    return -1;
  mlfqstr.stride_share += share;

  struct proc * p = myproc();
  
  // if stride queue is empty
  if(!mlfqstr.stride_head){
    // remove the process from mlfq
    mlfqrm(p);

    // add the process to stride queue
    mlfqstr.stride_head = p;
  }
  else{
    struct proc* pptr = mlfqstr.stride_head;
    while(pptr){
      if(pptr->pid == p->pid)
        break;
      pptr = pptr->next;
    }

    // this process was not already in stride queue -> currently in mlfq
    if(!pptr){
      // remove from mlfq
      mlfqrm(p);
      // add to the front of the stride queue
      p->next = mlfqstr.stride_head;
      mlfqstr.stride_head = p->next;
    }
  }

  int stride = (int)(STRIDE_DIVIDEND/share + 0.5); // round up
  p->stride = stride;
  p->pass = 0;
  p->level = -1;
  p->timequant = 1;
  
  mlfqstr.mlfq_stride = (int)(STRIDE_DIVIDEND/(100-mlfqstr.stride_share) + 0.5);

  //cprintf("setcpushare");
  release(&ptable.lock);
  release(&mlfqstr.lock);
  return 0; 
}
