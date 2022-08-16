# First Milestone
## Process / Thread
- **Process**  
  A process is a running program with its own address space. Different processes have seperate address spaces that can not be accessed from another process.
- **Thread**  
  Threads created within one process shares the address space of that process, but each has their own register context(shared in its TCB) and stack. Sharing an address space is equivalent to using the same page table. 
- **Context Switching in both sides**  
  When context switching between threads, we save the register context of the old thread in its TCB(thread control block) and restore the register context of the new process from its TCB. The difference from context switch between processes is that the address space remains same. This means that we do not change the page table when context switching between threads from the same process. 

## POSIX Thread (pthread library)
- int **pthread_create** (pthread_t* thread, const pthread_attr_t* attr, void*  (* start_ routine)(void* ), void* arg)
  - Interface to create a new thread withing a program.
  - **thread**: A pointer to a structure of type pthread_t. We use this structure to interact with the thread from the main thread, such as waiting for it to complete with join and exiting from it. This structure will be initialized in pthread_create.
  - **attr**: Used to specify any sttributes the thread may have, such as the stack size, information about the scheduling priority, etc.
  - **start_routine**: A function pointer to the function the thread should execute. 
  - **arg**: The argument to the function that the thread executes (start_routine). arg is a void pointer, but can be casted to whatever type the programmer wants within the thread routine. Also, if we want to pass mutiple arguments, we define a structure containing the multiple arguments and pass that as arg.  
  
- int **pthread_join**(pthread_t thread, void** value_ptr)
  - Interface to make the main thread wait for a created thread to complete. When pthread_join is called in the main thread, the main thread waits inside the pthread_join routine and returns once the thread finishes running. The return values of the thread can be accessed using the value_ptr.
  - **thread**: Used to specify which thread to wait for. The pthread_t structure, unique to each thread, is initialized by the pthread_create routine. 
  - **value_ptr**: A pointer to the return value you expect to get back from the thread. 

- void **pthread_exit**(void* ret_val)
  - When a thread exits within its thread routine, the entire process exits too. To make only the thread exit, and not the enitre process, we either reture a value or use pthread_exit. 
  - **ret_val** is catched by the pthread_join, waiting for the exiting thread. 

# Second milestone: Basic LWP operations 
## thread_t
- **thread_id**  
  Id unique within lwp group.
- **group_id**  
  pid of the main thread (thread that called thread create)

## proc struct added features
- **uint ustack**  
  Base address of the stack page allocated for a thread in the main thread's address space

  ![image](uploads/fecaf6b8596e9f1eda2a670eed2fa12e/image.png)

- **int waiting_tid**  
  When the process is a main thread waiting for a certain thread to exit, it will set its waiting_tid field to the thread's thread id. When a thread exits, it will check if its main thread's waiting_tid is set to itself and reset it to -1.
  
- **int thread_count**  
  The number of threads a main thread has. It includes the main thread, so a normal process will have thread_count == 1.
  
- **int next_tid**   
  The number of active threads always changes. Therefore allocating thread_ids using thread_count is problematic. (we might end up with threads with the same thread_id). So we use a next_tid which is a counter for the total number of threads a process has ever created and use it to allocate thread ids.

- **int pid**  
  The main thread's pid will be set normally. However, a thread will have pid == -1 to indicate that it is a lwp, not a normal process. 

- **int thread_id**  
  Id unique within an lwp group. 

- **struct proc\* lwpgroup**  
  Pointer to the main thread. The main thread's lwpgroup points to itself.

- **void\* retval**   
  Memory space to save the return value of the thread routine.

- **struct proc\* t_link**  
  Threads in the same lwp group are connected by a linked list. t_link is used to link their proc structures. When a new thread is created, we add is to the thread list using add_thread(), and remove it with rm_thread() in thread_exit. If process pid3 creates 10 threads, the thread list will look like the below figure. We can find the entry point of a thread's thread list by referencing the t_link field of the main thread.

## mlfqstr struct added features 

  ![image](uploads/dcb183ad9fd2af4e18eb52009e6f8069/image.png)

- **struct proc\* next_t[NPROC]**  
  If process pid3 created 10 threads, we have to alternate between the 10 threads. Each thread should get to run for 1 tick at a time. To do this, we keep an array of struct proc pointers. The threads within the lwp group created by process pid3, will use next_t[3] as a pointer that indicates which thread gets to run in the next round. next_t[3] will be updated when a threads switch between each other in thread_swtch. Thread switch occurs in either yield, scheduler or thread_exit. 

- **init_next_t()**
  In thread_create, if the main thread's thread count is 1, this means the thread list only contains the main thread itself (there are no lwps). Therefore, we have to initialize the next_t pointer. If process 'p' creates a new lwp thread for the first time, we execute
  ```  
  next_t[p->[pid] = p;
  ```

- **add_thread()**
  When a new thread is created, we add the thread to the thread list right behind the thread that will be executed next. This way, we can run the newly created thread sooner. 

  ```
  struct proc* next_t = mlfqstr.next_t[p->lwpgroup->pid];
  p->t_link = next_t->t_link;
  next_t->t_link = p;
  ```

- **rm_thread()**  
  When a thread exits, we deleted it from the thread list data structure.
  
- **update_next_t()**  
  When we switch between threads, we need to update the thread to be executed in the next scheduling round. We loop through the thread list to find a process that is RUNNABLE and also is not waiting for a thread to exit in thread join. We check the waiting_tid field, and if it is not -1, this means it is waiting for a thread, and we do not schedule it. The old_t is the thread that is about to be scheduled now, and new_t is the thread that will be scheduled in the next round. We loop the thread list, updating new_t, until we loop a whole cycle and new_t reached old_t. This means there is no other thread to run other than the currently selected thread old_t, so we check its state and waiting_tid and schedule it in. However even the to-be-scheduled old_t may no meet the selecting criteria, and in this case update_next_t returns -1. The caller of update_next_t() will behave accordingly. Callers of update_next_t() are yield(), scheduler() and thread_exit().
  ```
  struct proc* old_t = mlfqstr.next_t[main_t->pid];
  struct proc* new_t;
  
  if(!old_t){
    old_t = main_t;
  }
  
  if(!old_t->t_link){
    new_t = main_t;
  }
  else{
    new_t = old_t->t_link;
  }
  
  while(new_t != old_t){
    if(new_t->state == RUNNABLE && new_t->waiting_tid == -1){
      mlfqstr.next_t[main_t->pid] = new_t;
      return 0;
    }
    new_t = new_t->t_link;
    if(!new_t){
      new_t = main_t;
    }
  }
  
  if(new_t->state == RUNNABLE && new_t->waiting_tid == -1){
    mlfqstr.next_t[main_t->pid] = new_t;
    return 0;
  }
  else{
    mlfqstr.next_t[main_t->pid] = 0;
    return -1;
  }
  ```

## Scheduling / Context switching between threads
If process pid3 created 10 threads, we have to alternate between the 10 threads, each running it for 1tick. If the threads collectively use the time quantum, we schedule in a different process, and if the threads collectively use up a time allotment, we lower the level of the threads. 

- **int thread_swtch(struct context\*\* old_context, struct proc\* main_thread)**  
  Switching between threads do not occur via the scheduler. In yield(), if the yielding process is a lwp thread, we call thread_swtch() to do thread specific context switching jobs.  
  
  1. Find the next thread to run by referencing the next_t array. We use the main thread's pid as an index.
  2. Call update_next_t() to find the thread to run in the next scheduling round and update the next_t array.
  3. The next_t array may have been null, if there were no eligible threads to run in the thread list. In this case we return -1, and the caller function will behave accordingly. 
  4. If next_t is not null, we set the cpu's proc to next_t, set the process's state to RUNNING, and call the normal swtch function. We do not need switchuvm since the page directory is shared between threads (no need to update CR3 control register).  
  
  ```
  struct proc* next_t;
  acquire(&mlfqstr.lock);
  next_t = mlfqstr.next_t[main_thread->pid];
  update_next_t(main_thread);
  release(&mlfqstr.lock);
  if(!next_t){
    return -1;
  }

  mycpu()->proc = next_t;
  next_t->state = RUNNING;
  swtch(old_context, next_t->context);
  return 0;
  ```

- **int yield(void)**  
  In yield, we find the main thread using the yielding process's lwpgroup field, and use the main thread's tickcount, time quantum and time allotment fields. If the threads collectively use up the time allotment, we call lower level, which lowers the level of all the threads in the lwp group. Before, we called the scheduler whenever a process used up its time quantum. However, now we also check if the yielding process is a thread. If the thread hasn't used up its time quantum yet, we call thread_swtch insetad of entering the scheduler. This way we directly switch between threads. If the return value of thread_swtch is -1, this means therea are no eligible threads to run in the thread list. We call the scheduler in this case. 

  ```
  if(main_thread->thread_count > 1 && tickcount % main_thread->timequant != 0){
    if(thread_swtch(&curproc->context, main_thread) == -1){
      sched();
    }
  }  
  else if(lowered || tickcount % main_thread->timequant == 0){
    curproc->state = RUNNABLE;
    sched();
  }

  ```

- **void scheduler(void)**  
  In the scheduler, when we loop the process table, we only select main threads, not lwp threads. We do this by filtering out processes with pid == -1. This is to ensure the scheduling sequence of threads adheres to the mlfq and stride policies. 
  
  Also, when a process that has been selected (the 'newproc' variable in the below code) is a main thread with more than 1 lwp threads, we call thread_swtch. In thread_swtch we perfrom appropriate actions such as selecting the next thread to run and updating the next_t array. Thus we context switch from the scheduler to thread to run. 
  
  ```
  contextswitch:
    if(newproc->thread_count > 1){
      thread_swtch(&(c->scheduler), newproc);
    
      switchkvm();
      c->proc = 0;
      goto norunnable;
    }

    c->proc = newproc;
    switchuvm(newproc);
    newproc->state = RUNNING;
   
    swtch(&(c->scheduler), newproc->context);
    
    switchkvm();
    c->proc = 0;

  norunnable:
    release(&ptable.lock);
  ```


## thread_create
- Find an UNUSED process p.
- Set pid to -1 to indicate that this thread is not the main thread of the lwp group.
- Set lwpgroup, which is a pointer to the main thread, to the current process calling thread_create.
- Allocate a kernel stack for this thread.
  - p->kstack = kalloc()
  - Set up p->tf, and p->context.
  - p->context->eip = (uint)forkret;
  - push address of trapret onto kstack.
  - p->tf->eip = (uint)start_routine;
  - p->tf->esp = top of newly allocated ustack (reference next step).
  
  ![image](uploads/467f5cabb058d5c2fd5c487c010ec8a9/image.png)  

- Find empty page in main thread's address space
  Starting from the main thread's ustack, we start searching page by page until we find one that is not present (*pte & PTE_P is 0). Once we find an empty page, we set the new thread's ustack to the address of this empty page.  

- Allocate user stack
  - Using allocuvm(), allocate a page at the address pointed to by p->ustack.
  - Push arg and a fake return address into the allocated user stack. 

 ```
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
```

- Share the main thread's page directory with the new thread.
- Set the process's thread id and initialize the thread_t values. (thread_id, and the group_id which is the pid of the main_thread.
- Increase the thread_count of the main_thread.
- Add the thread to the thread list using add_thread().

## thread_exit
- Set the calling thread to ZOMIBE state.
- Write the return value to this process's retval field.
- Decrease the thread_count of the main_thread.
- Remove this thread from the thread list using rm_thread().
- Deallocate the thread's user stack using the deallocuvm routine and the ustack field of the thread.
- Check the main thread's waiting_tid field. If it is same as this thread's thread id, this means the main thread is waiting for this thread to exit in thread_join. Therefore, we set the waiting_tid value back to -1 so it can be scheduled in again. (We dont select thread's with waiting_tid values that are not -1. See update_next_t().)
- Context switch to a different thread by calling thread_swtch(). If thread_swtch() returns -1, we need jump into the scheduler and never return. 

## thread_join
  - From the thread_t, read thread_id and group_id to find the corresponding proc structure from the ptable list. 
  - If the thread's proc structure is not in ZOMBIE state, this means the process has not exited yet. Therefore, we set waiting_tid field to the thread_id we need to wait for and yield.
  ```
  if(p->state != ZOMBIE){
    curproc->waiting_tid = tid;
    curproc->state = RUNNABLE;
    release_ptable();
    yield();
    acquire_ptable();
  }
  ```
  - Once the thread exits and sets the main thread's waiting_tid back to 0, the main_thread will be scheduled again and will resume executing thread_join.
  - Read the retval from the thread's retval field.
  - Free the thread's kernel stack.
  - Clean up proc structure values. 

# Third milestone: Interaction with other services in xv6
## exit  
  If a process calling exit is a LWP thread, than find all the process from the ptable that are in the same LWP group and clean up(deallocate page table, deallocate proc structures from ptable) all of them. 

## fork  
  If mutiple LWPs from the same group call fork(), we normally proceed with the fork system call by copying each threads address space and allocating a process for it. 

## exec
  Even if exec is called by a LWP thread, we behave the same, cleaning up the threads memory and allocating a new address space for this process with new eip values pointing to the entry point of the code to execute.

## sbrk
  When mutiple LWPs request memory expansion, we make sure that these requests are processes atomically. Also, we make sure the memory request does not end up overlapping with other parts of the address space.

## kill
  If a process calling the kill system call is a LWP thread, we search the ptable for all other LWP threads within the same group and clean up there page table and proc strcuture.

## pipe  
  LWPs in the same group share the ofile field to share the pipe, and we secure critical sections of fiel read and write using locking mechanisms.

## sleep  
  When a LWP goes to sleep and during that time a different LWP from the same group is terminatd, we reap all ZOMBIE and SLEEPING processes. 

## Interaction with MLFQ and Stride
- When thread_create is called, we check if the calling process is in mlfq or stride and copy all the scheduling policies.
- When a process yields, and it is a thread.
  - If it has used up its time quantum, call the scheduler and run a different process. (make sure not to schedule a thread from this lwp group.)
  - If it has not used up its time quantum yet, search if there is another thread within the same lwp group. If there is, context switch to that thread. If not, simply return from the yield function and run the same thread again.
  - When running a thread from one lwp group, we make sure to update the tick_count value of all the other threads in the same lwp group. This is to ensure that all LWPs within the same group share their time quantum and time allotment.

## Example Running Scenario
- p1 with 4 threads(t0~t3) including its main thread -> allocated 25% share
- p2 -> 25% share
- p3, p4: mlfq level 2

Assume 'stride = 100/share'.

| process | stride |
|----|----|
| p1 | 4 |
| p2 | 4 | 
| mlfq(p3, p4) | 2 |

Our scheduler only picks from the ptable, processes that are not a LWP thread. We only choose processes that either have one single thread or is the main thread. We do this by selecting processes that are pid!=-1. This is important to make the cpu share allocation correct. 

In our example, since p1 and p2 are stride processes and p3 and p4 are in the mlfq, all processes have a time quantum of 5ticks.

*p1t0 refers to the main thread of p1, p1t1 refers to the first thread of p1, etc.*
 
| Time | Selected process | Running sequence |
|----|----|----|
| Tick 0 | p1 | p1t0-p1t1-p1t2-p1t3-p1t0 |
| Tick 5 | p2 | p2-p2-p2-p2-p2 |
| Tick 10 | mlfq(p3) | p3-p3-p3-p3-p3 |
| Tick 15 | mlfq(p4) | p4-p4-p4-p4-p4 |

**Result**
- p1: 5/20(25%)
- p2: 5/20(25%)
- mlfq: 10/20(50%) 
