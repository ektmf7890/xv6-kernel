struct stat;
struct rtcdate;

struct spinlock {
  uint locked;       // Is the lock held?
  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
  uint pcs[10];      // The call stack (an array of program counters)
                     // that locked the lock.
};

typedef struct _thread_t{
  int thread_id;
  int group_id;
}thread_t;

typedef struct __my_sem_t{
  int value;
  int chan;
  struct spinlock lock;
}xem_t;

typedef struct __my_rwlock_t{
  xem_t lock;
  xem_t writelock;
  int readers;
}rwlock_t;

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int my_syscall(char*);
int getppid(void);
int  yield(void);
int getlev(void);
int set_cpu_share(int);
int thread_create(thread_t* thread, void* (*start_rotine) (void*), void* arg);
void thread_exit(void* retval);
int thread_join(thread_t thread, void** retval);
int pwrite(int, void*, int, int);
int pread(int, void*, int, int);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);

// semaphore.c
int xem_init(xem_t*);
int xem_wait(xem_t*);
int xem_post(xem_t*);

// rwlock.c
int rwlock_init(rwlock_t*);
int rwlock_acquire_readlock(rwlock_t*);
int rwlock_acquire_writelock(rwlock_t*);
int rwlock_release_readlock(rwlock_t*);
int rwlock_release_writelock(rwlock_t*);
