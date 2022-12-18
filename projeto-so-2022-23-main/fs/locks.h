#ifndef LOCKS_H
#define LOCKS_H

#include <pthread.h>

void rw_init(pthread_rwlock_t *lock, pthread_rwlockattr_t *attr);

void rw_destroy(pthread_rwlock_t *lock);

void rw_read_lock(pthread_rwlock_t *lock);

void rw_write_lock(pthread_rwlock_t *lock);

void rw_unlock(pthread_rwlock_t *lock);

#endif // LOCKS_H