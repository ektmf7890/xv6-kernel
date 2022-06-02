#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 10
#define NTEST 1

int binary_sem_test(void);

int (*testfunc[NTEST])(void) = {
  binary_sem_test,
};

char *testname[NTEST] = {
  "binary_sem_test",
};

volatile int gcnt;
int gpipe[2];
xem_t sem;

int main(int argc, char* argv[])
{
  int i;
  int ret;
  int pid;
  
  for(i = 0; i < NTEST; i++){
    printf(1, "%d. %s start\n", i, testname[i]);
    
    if(pipe(gpipe) < 0){
      printf(1, "pipe panic\n");
      exit();
    }

    ret = 0;

    if((pid = fork()) < 0){
      printf(1, "fork panic\n");
      exit();
    }

    if(pid == 0){
      close(gpipe[0]);
      testfunc[i]();
      write(gpipe[1], (char*)&ret, sizeof(ret));
      close(gpipe[1]);
      exit();
    }
    else{
      close(gpipe[1]);
      if(wait() == -1 || read(gpipe[0], (char*)&ret, sizeof(ret)) == -1 || ret != 0){
          printf(1, "%d. %s panic\n", i, testname[i]);
          exit();
      }
      close(gpipe[0]);
    }
    printf(1, "%d. %s finish\n", i, testname[i]);
    sleep(100);
  }
  exit();
}

void nop(){ }

void*
inc_thread_main(void* arg)
{
  int tid = (int) arg;
  int i, j;
  int tmp;
  
  for (i = 0; i < 10000; i++){
    xem_wait(&sem);
    for(j = 0; j < 1000; j++){
      tmp = gcnt;
      tmp++;
	    asm volatile("call %P0"::"i"(nop));
      gcnt = tmp;
    }
    xem_post(&sem);
  }
  printf(1, "gcnt: %d (thread%d exits)\n", gcnt, tid);
  thread_exit((void *)(tid+1));

  return 0;
}

int 
binary_sem_test(void)
{
  thread_t threads[NUM_THREAD];
  int i;
  void *retval;
  gcnt = 0;
 
  xem_init(&sem);
  //printf(1, "sem->lock->name:, sem->value:%d, &sem->chan:%d\n", sem.value, &sem.chan);

  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], inc_thread_main, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
    printf(1, "thread create %d\n", i);
  }
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_join(threads[i], &retval) != 0 || (int)retval != i+1){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  printf(1,"total: %d\n", gcnt);
  return 0;
}
