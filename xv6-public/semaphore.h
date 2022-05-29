#ifndef SPINLOCK_H
#define SPINLOCK_H
#include "spinlock.h"
#endif

typedef struct __my_sem_t{
  int value;
  int chan;
  struct spinlock lock;
}xem_t;
