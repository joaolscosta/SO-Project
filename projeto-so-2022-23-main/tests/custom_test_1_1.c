#include "fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BUFFER_LEN 600
#define CHAR_NUMBER_1 272
#define CHAR_NUMBER_2 1000

int main() {

    // TEST SMALL FILE:

    char *path = "/createdfile";
    char *file_to_read = "tests/custom_input_1.txt";
    char buffer[BUFFER_LEN];

    assert(tfs_init(NULL) != -1);

    int f = tfs_copy_from_external_fs(file_to_read, path);
    assert(f != -1);

    f = tfs_open(path, TFS_O_CREAT);
    assert(f != -1);

    ssize_t read;
    read = tfs_read(f, buffer, sizeof(buffer) - 1);
    assert(read == CHAR_NUMBER_1);

    assert(tfs_close(f) != -1);

    // TEST LARGE FILE:

    path = "/createdfile2";
    file_to_read = "tests/custom_input_2.txt";

    f = tfs_copy_from_external_fs(file_to_read, path);
    assert(f != -1);

    f = tfs_open(path, TFS_O_CREAT);
    assert(f != -1);

    read = 0;
    ssize_t current = tfs_read(f, buffer, sizeof(buffer) - 1);
    read += current;
    while (current != 0) {
        current = tfs_read(f, buffer, sizeof(buffer) - 1);
        read += current;
    }
    // assert(read == CHAR_NUMBER_2);
    assert(read == CHAR_NUMBER_2);

    assert(tfs_close(f) != -1);

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}

//