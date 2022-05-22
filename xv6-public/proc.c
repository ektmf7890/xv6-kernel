#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

// protected data for scheduling tasks
struct{
  uint priboosttime;         // Counts num of ticks passed since last pri boost. Checked at process yield.
  uint mlfq_pass;            // Pass value of mlfq processes
  uint mlfq_stride;          // Stride value of mlfq processes -> changes whenever set_cpu_share called.
  int mlfq_proc_cnt;         // Number of processes in the mlfq. Used when finding min pass value in stride scheduler.
  int stride_share;          // Sum of CPU share non-mlfq processes are occupying.
  struct proc* stride_head;  // Linked list of processes in stride queue.
  int qlevels[3];            // Number of processes in each mlfq level.
  struct spinlock lock;
  struct proc * next_t [NPROC];
}mlfqstr;


static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// remove a process from mlfq when its state changes from RUNNABLE to different state
// ptable lock should be acquired in caller
void 
mlfqrm(struct proc* p){
  if(p->level >= 0 && p->level <= 2)
    mlfqstr.qlevels[p->level]--;
  mlfqstr.mlfq_proc_cnt--;
}

// add a process to mlfq when its status is changed to RUNNABLE
// ptable lock should be acquired in caller
void 
mlfqadd(struct proc* p)
{
  if(p->level >= 0 && p->level <= 2)
    mlfqstr.qlevels[p->level]++;
  mlfqstr.mlfq_proc_cnt++;
}
  
void 
striderm(struct proc * p)
{
  struct proc * pptr = mlfqstr.stride_head;

  // If p is at the head of the stride queue.
  if(pptr == p)
    mlfqstr.stride_head = pptr->s_link; 

  else{
    while(pptr){
      if(pptr->s_link == p){
        pptr->s_link = p->s_link;
        break;
      }
      pptr = pptr->s_link;
    } 
  }
  
  p->s_link = NULL;

  // reset the mlfq share and mlfq stride
  mlfqstr.stride_share -= p->share;
  mlfqstr.mlfq_stride = (int)(STRIDE_DIVIDEND/(100-mlfqstr.stride_share) + 0.5);

  // If the stride queue becomes empty, we reset the mlfq's pass value to 0
  if(!mlfqstr.stride_head)
    mlfqstr.mlfq_pass = 0;
  //cprintf("mlfq share: %d, mlfq stride: %d, mlfq pass: %d\n", 100-mlfqstr.stride_share, mlfqstr.mlfq_stride, mlfqstr.mlfq_pass);
}

void 
strideadd(struct proc * p)
{
  // Add to the front of the stride queue.
  p->s_link = mlfqstr.stride_head;
  mlfqstr.stride_head = p;
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

//  cprintf("allocproc\n");
  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // Assign to level 2
  p->level = 2;
  p->timequant = 5;
  p->timeallot = 20;
  p->tickcount = 0;
  
  // Initialize values needed when added to stride queue.
  p->s_link = NULL; 
  p->share = 0;
  p->stride = -1;
  p->pass = 0;

  // Treat the process as the number 0 thread of itself.
  p->thread_count = 1;
  p->thread_id = 0;
  p->next_tid = 1;
  p->lwpgroup = p;
  p->dont_sched = 0;
  //p->caller_isnt_yield = 0;
  p->waiting_tid = -1;

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
  cprintf("userinit\n");
  acquire(&ptable.lock);
  p->state = RUNNABLE;
  release(&ptable.lock);
  
  acquire(&mlfqstr.lock);
  mlfqadd(p);
  release(&mlfqstr.lock);
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
  np->ustack = np->sz;
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

//  cprintf("fork\n");
  acquire(&ptable.lock);

  np->state = RUNNABLE;
  if(np->level != -1){
    acquire(&mlfqstr.lock);
    mlfqadd(np);
    release(&mlfqstr.lock);
  }

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
  // Remove from mflq
  if(curproc->level != -1){
    acquire(&mlfqstr.lock);
    mlfqrm(curproc);
    release(&mlfqstr.lock);
  }
  // Remove from stride queue
  else{
    acquire(&mlfqstr.lock);
    striderm(curproc);
    release(&mlfqstr.lock);
  }
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
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
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
  struct cpu * c = mycpu();
  struct proc * searchidx = ptable.proc;
  c->proc = 0;
  //int caller_isnt_yield;

  for(;;){
    // Enable interrupts on this processor
    sti();


    acquire(&ptable.lock);

    struct proc * newproc = NULL;

    acquire(&mlfqstr.lock);
    // If the stride queue is not empty
    if(mlfqstr.stride_head){
      // Find the process with the least pass value
      uint min_pass;
      struct proc* sp = mlfqstr.stride_head;

      if(mlfqstr.mlfq_proc_cnt > 0){
        min_pass = mlfqstr.mlfq_pass;
      }
      else{
        min_pass = mlfqstr.stride_head->pass;
      }

      while(sp){
        if(sp->pass <= min_pass && sp->state == RUNNABLE){
          min_pass = sp->pass;
          newproc = sp;
        }
        sp = sp->s_link;
      }

      // if a stride process has the smallest pass value
      if(newproc){
        release(&mlfqstr.lock);
        goto contextswitch;
      }
    }

    // Search process to run from mlfq.
    // First, find the level to select from.
    int level;
    if (mlfqstr.qlevels[2] > 0) level = 2;
    else if (mlfqstr.qlevels[1] > 0) level = 1;
    else level = 0;
    release(&mlfqstr.lock);

    // Search the ptable for a runnable process, from the mlfq, with the correct level.
    // A process in the stride queue wont be selected because their level values are -1
    
    // We search at most 64 processes (loop through the entire ptable)
    // before giving up and ending the search.
    int cnt = 0;
    struct proc* p;
    while((p = searchidx)){
      if(searchidx < &ptable.proc[NPROC-1])
        searchidx ++;
      else
        searchidx = ptable.proc;

      if((p->state == RUNNABLE) && (p->level == level) && (p->pid!=-1)){
        newproc = p;
        goto contextswitch;
      }

      if(cnt >= 63) 
        break;
      cnt++;
    }

    if(!newproc){
      goto norunnable;
    }
    
contextswitch:
    // If this process is a main thread of a lwp group with mutiple threads 
    if(newproc->thread_count > 1){
      newproc = mlfqstr.next_t[newproc->pid];
      update_next_t(newproc->lwpgroup);
      
      // if the scheduled process is a main_thread that is waiting in thread_join,
      // we schedule in a different thread from the lwp group.
      if(newproc->waiting_tid != -1 || newproc->state != RUNNABLE){
        newproc = mlfqstr.next_t[newproc->pid];
        update_next_t(newproc);
      }

      //cprintf("about to run thread tid:%d\n", newproc->thread_id);
    }
    //caller_isnt_yield = newproc->caller_isnt_yield;

    // Switch to chosen process. It is the process's job 
    // to release ptable.lock (in yield) and then reacquire it(when the switched in process yields)
    // before jumping back to us. 
    c->proc = newproc;
    switchuvm(newproc);
    newproc->state = RUNNING;
   
  // if(caller_isnt_yield){
    //  release(&ptable.lock);
   // }

    swtch(&(c->scheduler), newproc->context);
    
    switchkvm();
    c->proc = 0;

norunnable:
    release(&ptable.lock);
  }
}

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

  if(p->pid==-1){
    //cprintf("a thread called sched\n");
    //if(readeflags()&FL_IF)
   //   cprintf("interrupt enabled\n");
   // cprintf("p->context: 0x%x\n", &p->context);
  }

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
    if(p->state == RUNNABLE && p->level != -1 && p->pid!=-1){ //pid==-1 means this is a thread.
      mlfqstr.qlevels[p->level]--;
      p->level = 2;
      p->timequant = 5;
      p->timeallot = 20;
      p->tickcount = 0;
      mlfqstr.qlevels[2]++;
    }
  }
}

void lowerlevel(struct proc* p){
  if (p->level < 1 || p->level > 2){
    return;
  }

  // no need to acquire mlfqstr's lock. (done in yield)
  mlfqstr.qlevels[p->level]--;
  
  p->level--;
  p->tickcount = 0;
  switch(p->level){
    case 1:
      p->timequant = 10;
      p->timeallot = 40;
      break;
    case 0:
      p->timequant = 20;
      break;
  }
  
  mlfqstr.qlevels[p->level]++;
}

void
thread_swtch(struct proc* curproc, struct proc* main_thread)
{
  struct proc* next_t;
  acquire(&mlfqstr.lock);
  next_t = mlfqstr.next_t[main_thread->pid];
  update_next_t(main_thread);

  if(next_t->waiting_tid != -1 || next_t->state != RUNNABLE){
    next_t = mlfqstr.next_t[main_thread->pid];
    update_next_t(main_thread);
  }
  release(&mlfqstr.lock);

  mycpu()->proc = next_t;
  switchuvm(next_t);
  next_t->state = RUNNING;
  swtch(&curproc->context, next_t->context);
}

// Give up the CPU for one scheduling round.
int
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock

  struct proc* curproc = myproc();
  struct proc* main_thread = curproc->lwpgroup;
  int intena = mycpu()->intena;
  
  uint tickcount = main_thread->tickcount++;
  uint level = main_thread->level;
  uint timeallot = main_thread->timeallot;

  // Check if this process has used up its time allotment
  // We skip this part for stride processes (their level values are -1)
  int lowered = 0;
  if(level > 0 && (tickcount >= timeallot)){
    // used up its time allot
    lowerlevel(main_thread);
    lowered = 1;
  }
  
  // Priority Boost
  acquire(&tickslock);
  acquire(&mlfqstr.lock);
  if(ticks - mlfqstr.priboosttime >= 200){
    mlfqstr.priboosttime = ticks;
    priboost();
  }
  release(&tickslock);

  // If the stride queue is not empty, we have to increment pass values every time a process yields
  if(mlfqstr.stride_head){
    // if the yielding process is in the stride queue
    if(level == -1)
      main_thread->pass += main_thread->stride;
        
    // if the yielding process is in the mlfq
    else 
      mlfqstr.mlfq_pass += mlfqstr.mlfq_stride;
  }
  release(&mlfqstr.lock);

  // If the process has mutiple threads,
  // we choose the next thread to run here and context switch.
  // We do not context switch through the scheduler.
  if(main_thread->thread_count > 1){
    //struct proc* next_t = mlfqstr.next_t[main_thread->pid];
    //update_next_t(main_thread);
    
    curproc->state = RUNNABLE;
    intena = mycpu()->intena;
    thread_swtch(curproc, main_thread);
    mycpu()->intena = intena;

   // cprintf("switching to thread- pid:%d, tid:%d\n", main_thread->pid, next_t->thread_id);

    //mycpu()->proc = next_t;
    //switchuvm(next_t);
    //next_t->state = RUNNING;

   // if(next_t->caller_isnt_yield){
     // release(&ptable.lock);
   // }

    //swtch(&(curproc->context), next_t->context);
    //mycpu()->intena = intena;

   // cprintf("switching back to thread - pid:%d, tid: %d\n", main_thread->pid, curproc->thread_id);
  }
  
  // Do not call scheduler if it hasn't used up its time quantum
  // Processes in stride queue will always call the scheduler because their time quantum is 1.
  else if(lowered || tickcount % main_thread->timequant == 0){
    curproc->state = RUNNABLE;
    sched();
  }

  release(&ptable.lock);

  return 0;
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
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
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
  if(p->level != -1){
    acquire(&mlfqstr.lock);
    mlfqrm(p);
    release(&mlfqstr.lock);
  }

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
      if(p->level != -1){
        acquire(&mlfqstr.lock);
        mlfqadd(p);
        release(&mlfqstr.lock);
      }
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
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

  cprintf("kill\n");
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        if(p->level != -1){
          acquire(&mlfqstr.lock);
          mlfqadd(p);
          release(&mlfqstr.lock);
        }
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
  acquire(&ptable.lock);
  acquire(&mlfqstr.lock);

  // 0 -> wrong input error
  if(share <= 0)
    return -1;

  // Total requeste CPU share > 80 -> error
  if(mlfqstr.stride_share + share > 80)
    return -1;


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
      pptr = pptr->s_link;
    }

    // This process was not already in stride queue -> currently in mlfq
    if(!pptr){
      // Remove from mlfq
      mlfqrm(p);

      // Add to the stride queue
      strideadd(p);
    }

    // If this process is already in the stride queue, we don't have to worry about it.
    // Since the stride_head contains pointers to the proc structs, we simply change the values of myproc().
  }

  mlfqstr.stride_share += share;
  p->share = share;

  int stride = (int)(STRIDE_DIVIDEND/share + 0.5); // round up
  p->stride = stride; 
  
  p->level = -1;
  p->timequant = 5;
  
  // Pass value is already set to 0 in allocproc.
  // If the process is already in the stride queue, we don't update pass to 0
  // because it would monopolize the cpu for the first few runs.
  
  mlfqstr.mlfq_stride = (int)(STRIDE_DIVIDEND/(100-mlfqstr.stride_share) + 0.5);

  release(&ptable.lock);
  release(&mlfqstr.lock);
  return 0; 
}

struct proc* 
find_unused(void){
  struct proc* p;

  acquire(&ptable.lock);
  
  for(p=ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED){
      release(&ptable.lock);
      return p;
    }
  }
 
  release(&ptable.lock);
  return NULL;
}

struct proc* 
find_thread(int tid, int gid)
{
  struct proc* p;

  acquire(&ptable.lock);
  
  for(p=ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->thread_id == tid && p->lwpgroup->pid==gid){
      release(&ptable.lock);
      return p;
    }
  }
 
  release(&ptable.lock);
  return NULL;

}

void init_next_t(struct proc* p, int pid)
{
  //cprintf("init_next_t\n");
  acquire(&mlfqstr.lock);
  mlfqstr.next_t[pid] = p;
  release(&mlfqstr.lock);
}

// mlfqstr.lock should be acquire before calling this function.
int
update_next_t(struct proc* main_t)
{
  struct proc* old_t = mlfqstr.next_t[main_t->pid];
  struct proc* new_t;

  if(!old_t)
    cprintf("old_t is null\n");

  if(!old_t->t_link){
    new_t = main_t;
  }
  else{
    new_t = old_t->t_link;
  }

//  cprintf("old_t: pid %d\n", old_t->pid);
 // cprintf("new_t: pid %d\n", new_t->pid);

  // when we search one full cycle and still couldnt find a reunnable thread, we return -1;
  while(new_t != old_t){
    if(new_t->state == RUNNABLE && new_t->waiting_tid == -1){
      mlfqstr.next_t[main_t->pid] = new_t;
      return 0;
    }
    new_t = new_t->t_link;
  }
  return -1;
}

void
add_thread(struct proc* p)
{
  acquire(&mlfqstr.lock);
  struct proc* next_t = mlfqstr.next_t[p->lwpgroup->pid];
  p->t_link = next_t->t_link;
  next_t->t_link = p;
  release(&mlfqstr.lock);
}

void rm_thread(struct proc* p)
{
  // if thread to remove is not next_t, we dont do anything
  // if it is, we call update_next_t()

  struct proc* main_t = p->lwpgroup;
  struct proc* pptr = main_t;
  while(pptr){
    if(pptr->t_link == p){
      acquire(&mlfqstr.lock);
      if(mlfqstr.next_t[main_t->pid] == p){ // if the next thread pointer was pointing to this thread
        update_next_t(main_t);
      }
      release(&mlfqstr.lock);
      pptr->t_link = p->t_link;
      p->t_link = NULL;
      break;
    }
    pptr = pptr->t_link;
  }
}

void
acquire_ptable()
{
  if(holding(&ptable.lock)){
    //cprintf("already holding\n");
    return;
  }
  acquire(&ptable.lock);
}

void
release_ptable()
{
  if(!holding(&ptable.lock))
      return;
  release(&ptable.lock);
}

int is_holding_ptable(){
  if(holding(&ptable.lock)){
//    cprintf("holding ptable.lock\n");
    return 1;
  }
  else return 0;
}
