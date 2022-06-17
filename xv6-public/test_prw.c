#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define NUM_TEST 2
#define BUFF_SIZE 100

uint src_string_len;
char* buff;

int preadtest(void);
int pwritetest(void);
//int threadsafe_preadtest(void);
//int threadsafe_pwritetest(void);

int (*testfunc[NUM_TEST]) (void) = {
  pwritetest,
  preadtest,
};

char* testname[NUM_TEST] = {
  "pwrite test",
  "pread test",
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
    } 
    else{
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
pwritetest(void)
{
  int fd, n, i, off, w;
  char* src;

  // Create original file
  if((fd = open("prw_testfile", O_CREATE | O_WRONLY)) < 0){
    printf(1, "pwrite test panic: failed to open file\n");
    return -1;
  }
  
  for(i=0; i < 100; i++){
    printf(fd, "%d", i%10);
  }

  src = "Good morning. I am DY.\n";
  n = strlen(src);
  src_string_len = n;
  printf(1, "src: %d\n", src_string_len);
  off = 10;
  
  if((w = pwrite(fd, src, n, off)) < 0){
    printf(1, "pwrite error\n");
    return -1;
  }

  close(fd);

  return 0;
}

int 
preadtest(void)
{
  int fd, r, off;
  buff = (char*)malloc(BUFF_SIZE);

  if((fd = open("prw_testfile", O_RDONLY)) < 0){
    printf(1, "pwrite test panic: failed to open file\n");
    return -1;
  }

  off = 10;
  printf(1, "src_string_len:%d\n", src_string_len);
  if((r = pread(fd, buff, 23, off)) < 0){
    printf(1, "pread error\n");
    return -1;
  }

  printf(1, "Retrieved string: %s", buff);

  close(fd);

  return 0;
}

