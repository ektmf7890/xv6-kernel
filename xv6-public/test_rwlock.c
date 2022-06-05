#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 10
#define NUM_WRITERS 3
#define NTEST 3
#define BUFF_SZ 10000
#define SUM1 49995000
#define SUM2 50005000

int binary_sem_test(void);
int no_lock_test(void);
int rwlock_test(void);

int (*testfunc[NTEST])(void) = {
  binary_sem_test,
  no_lock_test,
  rwlock_test,
};

char *testname[NTEST] = {
  "binary_sem_test",
  "no_lock_test",
  "rwlock_test",
};

volatile int gcnt;
int gpipe[2];
xem_t sem;
rwlock_t rwlock;

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

volatile int buff[10000];

void*
read_thread_main(void* arg)
{
  int tid = (int)arg;
  int i, j, k;
  int race = 0;
  int sum = 0;
  int temp = 0;

  for(i = 0; i < 10000; i++){
    sum = 0;
    rwlock_acquire_readlock(&rwlock);
    for(j = 0; j < 10000; j++){
      sum += buff[j];
      for(k = 0; k < 10000000; k++){
        temp++;
        temp *= 2;
        temp /= 2;
        temp--;
      }
    }
    rwlock_release_readlock(&rwlock);
    if(sum != SUM1 && sum != SUM2){
      race = 1;
      break;
    }
  }

  xem_wait(&sem);
  printf(1, "reader %d exit with ", tid);
  if(race){
    printf(1, "race.\n");
  }
  else{
    printf(1, "no race.\n");
  }
  xem_post(&sem);
  thread_exit((void*)(tid+1));
  return 0;
}

void* 
write_thread_main(void*arg)
{
  int tid = (int) arg;
  int i, j, k;
  int temp = 0;
  
  for (i = 0; i < 10000; i++){
    if(i % 2 == 0){
      rwlock_acquire_writelock(&rwlock);
      for(j = 0; j < 10000; j++){
        buff[j] = j;
        for(k = 0; k < 10000000; k++){
          temp++;
          temp *= 2;
          temp /= 2;
          temp--;
        }
      }
      rwlock_release_writelock(&rwlock);
    }
    else{
      rwlock_acquire_writelock(&rwlock);
      for(j = 0; j < 10000; j++){
        buff[j] = j + 1;
        for(k = 0; k < 10000000; k++){
          temp++;
          temp *= 2;
          temp /= 2;
          temp--;
        }
      }
      rwlock_release_writelock(&rwlock);
    }
  }
  thread_exit((void *)(tid+1));

  return 0;
}

int 
rwlock_test(void)
{
  thread_t readers[NUM_THREAD];
  thread_t writers[NUM_WRITERS];
  void* retval;
  int i;
  

  xem_init(&sem);
  rwlock_init(&rwlock);

  for(i = 0; i < NUM_WRITERS; i++){
    if (thread_create(&writers[i], write_thread_main, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  for(i = 0; i < NUM_THREAD; i++){
    if (thread_create(&readers[i], read_thread_main, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  for(i = 0; i < NUM_WRITERS; i++){
    if (thread_join(writers[i], &retval) != 0 || (int)retval != i+1){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  for(i = 0; i < NUM_THREAD; i++){
    if (thread_join(readers[i], &retval) != 0 || (int)retval != i+1){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  return 0;
}

volatile int buff2[10000];

void* 
no_lock_read(void* arg)
{
  int tid = (int)arg;
  int i, j, k;
  int race = 0;
  int sum = 0;
  int temp = 0;

  for(i = 0; i < 10000; i++){
    sum = 0;
    for(j = 0; j < 10000; j++){
      sum += buff2[j];
      for(k = 0; k < 10000000; k++){
        temp++;
        temp *= 2;
        temp /= 2;
        temp--;
      }
    }
    if(sum != SUM1 && sum != SUM2){
      race = 1;
      break;
    }
  }

  xem_wait(&sem);
  printf(1, "reader %d exit with ", tid);
  if(race){
    printf(1, "race.\n");
  }
  else{
    printf(1, "no race.\n");
  }
  xem_post(&sem);
  thread_exit((void*)(tid+1));
  return 0;
}

void*
no_lock_write(void* arg)
{
  int tid = (int) arg;
  int i, j, k;
  int temp = 0;
  
  for (i = 0; i < 10000; i++){
    if(i % 2 == 0){
      for(j = 0; j < 10000; j++){
        buff2[j] = j;
        for(k = 0; k < 10000000; k++){
          temp++;
          temp *= 2;
          temp /= 2;
          temp--;
        }
      }
    }
    else{
      for(j = 0; j < 10000; j++){
        buff2[j] = j + 1;
        for(k = 0; k < 10000000; k++){
          temp++;
          temp *= 2;
          temp /= 2;
          temp--;
        }
      }
    }
  }
  thread_exit((void *)(tid+1));

  return 0;
}

int
no_lock_test(void)
{
  thread_t readers[NUM_THREAD];
  thread_t writers[NUM_WRITERS];
  void* retval;
  int i;

  xem_init(&sem);
  rwlock_init(&rwlock);

  for(i = 0; i < NUM_WRITERS; i++){
    if (thread_create(&writers[i], no_lock_write, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  for(i = 0; i < NUM_THREAD; i++){
    if (thread_create(&readers[i], no_lock_read, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return -1;
    }
  }
  for(i = 0; i < NUM_WRITERS; i++){
    if (thread_join(writers[i], &retval) != 0 || (int)retval != i+1){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  for(i = 0; i < NUM_THREAD; i++){
    if (thread_join(readers[i], &retval) != 0 || (int)retval != i+1){
      printf(1, "panic at thread_join\n");
      return -1;
    }
  }
  return 0;

}
