#include "types.h"
#include "defs.h"
#include "param.h"

int 
rwlock_init(rwlock_t* rwlock)
{
  rwlock->readers = 0;
  xem_init(&rwlock->lock);
  xem_init(&rwlock->writelock);
  return 0;
}

int 
rwlock_acquire_readlock(rwlock_t* rwlock)
{
  xem_wait(&rwlock->lock);
  rwlock->readers++;
  if(rwlock->readers == 1)
    xem_wait(&rwlock->writelock);
  xem_post(&rwlock->lock);
  return 0;
}

int 
rwlock_release_readlock(rwlock_t* rwlock)
{
  xem_wait(&rwlock->lock);
  rwlock->readers--;
  if(rwlock->readers == 0)
    xem_post(&rwlock->writelock);
  xem_post(&rwlock->lock);
  return 0;
}

int 
rwlock_acquire_writelock(rwlock_t* rwlock)
{
  xem_wait(&rwlock->writelock);
  return 0;
}

int rwlock_release_writelock(rwlock_t* rwlock)
{
  xem_post(&rwlock->writelock);
  return 0;
}
