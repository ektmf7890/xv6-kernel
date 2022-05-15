#include "types.h"
#include "stat.h"
#include "user.h"
#include "lwp.h"

void* routine(void* arg)
{
  int m = (int)arg;
  printf(1, "arg: %d\n", m);
  thread_exit((void*)(arg + 1));
  return (void*)(arg+1);
}

int main(int argc, char* argv[])
{
 // struct thread_t t0, t1, t2;
 // int cnt0, cnt1, cnt2;

  struct thread_t t0;
  int *cnt0;
  int arg0 = 100;
  //int arg1 = 150;

//  printf(1, "routine: %x\n", routine);
  thread_create(&t0, routine, &arg0);
  //thread_create(&t1, routine, &arg1);
 // printf(1, "new thread t0: tid %d, gid %d\n", t0.thread_id, t0.group_id);

  thread_join(t0, (void**) &cnt0);
  //thread_join(t1, (void**) &cnt1);
  
  printf(1, "thread 0 return: %d\n", *cnt0);
  //printf(1, "thread 1 return: %d\n", cnt1);
  //printf(1, "thread 2 return%d\n", cnt2);

  exit();
}
