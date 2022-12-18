#include "locks.h"
#include "state.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rw_init(pthread_rwlock_t *lock, pthread_rwlockattr_t *attr) {
    if (pthread_rwlock_init(lock, attr) != 0) {
        perror("pthread_rwlock_init");
        exit(EXIT_FAILURE);
    }
}

void rw_destroy(pthread_rwlock_t *lock) {
    if (pthread_rwlock_destroy(lock) != 0) {
        perror("pthread_rwlock_destroy");
        exit(EXIT_FAILURE);
    }
}

void rw_read_lock(pthread_rwlock_t *lock) {
    if (pthread_rwlock_rdlock(lock) != 0) {
        perror("pthread_rwlock_rdlock");
        exit(EXIT_FAILURE);
    }
}

void rw_write_lock(pthread_rwlock_t *lock) {
    if (pthread_rwlock_wrlock(lock) != 0) {
        perror("pthread_rwlock_wrlock");
        exit(EXIT_FAILURE);
    }
}

void rw_unlock(pthread_rwlock_t *lock) {
    if (pthread_rwlock_unlock(lock) != 0) {
        perror("pthread_rwlock_unlock");
        exit(EXIT_FAILURE);
    }
}