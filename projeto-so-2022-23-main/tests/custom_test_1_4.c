#include "fs/operations.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_LEN 50
#define THREAD_NUM 5
#define INPUT_FILE "./custom_input_1.txt"
#define TFS_FILE "/testing1"

void *read_fn(void *ignore);

int main() {
    assert(tfs_init(NULL) != -1);

    pthread_t tid[THREAD_NUM];

    tfs_copy_from_external_fs(INPUT_FILE, TFS_FILE);

    for (int i = 0; i < THREAD_NUM; i++) {
        // printf("Creating thread %d...\n", i);
        assert(pthread_create(&tid[i], NULL, read_fn, NULL) == 0);
    }

    for (int i = 0; i < THREAD_NUM; i++) {
        // printf("Joining thread %d...\n", i);
        assert(pthread_join(tid[i], NULL) == 0);
    }

    assert(tfs_destroy() == 0);
    printf("Successful test.\n");

    return 0;
}

void *read_fn(void *input) {
    (void)input; // ignore parameter

    int f = tfs_open(TFS_FILE, TFS_O_CREAT);
    assert(f != -1);

    char buffer[BUFFER_LEN];

    ssize_t bytes_read = tfs_read(f, buffer, BUFFER_LEN);

    ssize_t total_read = 0;
    while (bytes_read > 0) {
        printf("%s", buffer);
        total_read += bytes_read;
        memset(buffer, 0, BUFFER_LEN);
        bytes_read = tfs_read(f, buffer, BUFFER_LEN);
    }

    // check if both files reached the end
    assert(bytes_read == 0);

    assert(tfs_close(f) != -1);

    return NULL;
}