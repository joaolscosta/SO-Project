#include "fs/operations.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define BUFFER_LEN 50
#define THREAD_NUM 3
#define WRITE_THREAD_NUM 2
#define READ_THREAD_NUM 2
#define FILE_NUM 3
#define TFS_FILE "/testing1"

void *readfile(void *ignore);
void *writefile(void *ignore);

// This test creates a file in the tfs and then tries to read it from multiple
// threads

int main() {
    assert(tfs_init(NULL) != -1);

    pthread_t tid[THREAD_NUM];
    char *tfs_f[FILE_NUM] = {"/testing1", "/testing2", "/testing3"};

    assert(tfs_copy_from_external_fs("./custom_input_1.txt", tfs_f[0]) != -1);
    assert(tfs_copy_from_external_fs("./custom_input_3.txt", tfs_f[1]) != -1);
    assert(tfs_copy_from_external_fs("./custom_input_4.txt", tfs_f[2]) != -1);

    for (int i = 0; i < THREAD_NUM; i++) {
        // printf("Creating thread %d...\n", i);
        assert(pthread_create(&tid[i], NULL, readfile, NULL) == 0);
    }

    for (int i = 0; i < THREAD_NUM; i++) {
        // printf("Joining thread %d...\n", i);
        assert(pthread_join(tid[i], NULL) == 0);
    }

    assert(tfs_destroy() == 0);
    printf("Successful test.\n");

    return 0;
}

void *readfile(void *input) {
    (void)input; // ignore parameter

    int f = tfs_open(TFS_FILE, TFS_O_CREAT);
    assert(f != -1);

    char buffer[BUFFER_LEN];

    ssize_t bytes_read = tfs_read(f, buffer, BUFFER_LEN);

    ssize_t total_read = 0;
    while (bytes_read > 0) {
        total_read += bytes_read;
        memset(buffer, 0, BUFFER_LEN);
        bytes_read = tfs_read(f, buffer, BUFFER_LEN);
    }

    // check if both files reached the end
    assert(bytes_read == 0);

    assert(tfs_close(f) != -1);

    return NULL;
}