#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define NUM_TEST 3
#define FILE_BSIZE (16)*(1024)*(1024)

int createtest(void);
int readtest(void);
int stresstest(void);

int (*testfunc[NUM_TEST]) (void) = {
  createtest,
  readtest,
  stresstest,
};

char* testname[NUM_TEST] = {
  "create test",
  "read test",
  "stress test",
};

int f_cnt;
int gpipe[2];

int main(int argc, char* argv[])
{
  int i;
  int ret;
  int pid;
  int start = 0;
  int end = NUM_TEST-1;
  
  if (argc >= 2)
    start = atoi(argv[1]);
  if (argc >= 3)
    end = atoi(argv[2]);

  for (i = start; i <= end; i++){
    printf(1,"%d. %s start\n", i, testname[i]);
    if (pipe(gpipe) < 0){
      printf(1,"pipe panic\n");
      exit();
    }
    ret = 0;

    if ((pid = fork()) < 0){
      printf(1,"fork panic\n");
      exit();
    }
    if (pid == 0){
      close(gpipe[0]);
      ret = testfunc[i]();
      write(gpipe[1], (char*)&ret, sizeof(ret));
      close(gpipe[1]);
      exit();
    } else{
      close(gpipe[1]);
      if (wait() == -1 || read(gpipe[0], (char*)&ret, sizeof(ret)) == -1 || ret != 0){
        printf(1,"%d. %s panic\n", i, testname[i]);
        exit();
      }
      close(gpipe[0]);
    }
    printf(1,"%d. %s finish\n", i, testname[i]);
    sleep(100);
  }
  exit();
}

int 
createtest(void)
{
  int fd;
  int n;
  char* src;

  if((fd = open("testfile", O_CREATE | O_WRONLY)) < 0){
    printf(1, "createtest panic: failed to open file\n");
    return -1;
  }

  n = FILE_BSIZE * sizeof(char);
  //printf(1, "n = %d\n", n);
  src = (char*)malloc(n);
  memset(src, 0, n); 

  if((write(fd, src, n) < 0)){
    printf(1, "createtest panic: failed to write data\n");
    return -1;
  }
  
  printf(1, "created file of size: %d\n", n);

  close(fd);

  return 0;
}

int 
readtest(void)
{
  int fd;
  uint n;
  char* dst;

  if((fd = open("testfile", O_RDONLY)) < 0){
    printf(1, "readtest panic: failed to open file\n");
    return -1;
  }

  n = FILE_BSIZE * sizeof(char);
  dst = (char*)malloc(n);

  if((read(fd, dst, n) < 0)){
    printf(1, "readtest panic: failed to write data\n");
    return -1;
  }

  printf(1, "removing file\n");
  unlink("testfile");

  close(fd);

  return 0;
}

int 
stresstest(void)
{
  int i;
  for(i=0; i<4; i++){
    if(createtest() < 0){
      printf(1, "stresstest panic: create test %d\n", i);
      return -1;
    }
    printf(1, "removing test file\n");
    unlink("testfile");
  }
  return 0;
}
