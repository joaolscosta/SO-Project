#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "state.h"

static pthread_mutex_t *counter_lock;
static pthread_mutex_t *write_lock;

void lock_file_for_writing(int fhandle, open_file_entry_t *file) {
    pthread_mutex_lock(&write_lock);
}

void unlock_file_for_writing(int fhandle, open_file_entry_t *file) {
    pthread_mutex_unlock(&write_lock);
}

void lock_file_for_reading(int fhandle, open_file_entry_t *file) {
    pthread_mutex_lock(&counter_lock);
    file->readcnt++;
    if (file->readcnt == 1) {
        // If this is the first reader, lock the write lock so that no one can
        // write while we are reading
        pthread_mutex_lock(&write_lock);
    }
    pthread_mutex_unlock(&counter_lock);
}

void unlock_file_for_reading(int fhandle, open_file_entry_t *file) {
    pthread_mutex_lock(&counter_lock);
    file->readcnt--;
    if (file->readcnt == 0) {
        // If this is the last reader, unlock the write lock so that someone can
        // write
        pthread_mutex_unlock(&write_lock);
    }
    pthread_mutex_unlock(&counter_lock);
}

void rw_init(pthread_rwlock_t * lock, pthread_rwlockattr_t * attr) {
    if (pthread_rwlock_init(lock, attr) != 0) {
        perror("pthread_rwlock_init");
        exit(EXIT_FAILURE);
    }
}