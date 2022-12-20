#include "fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BUFFER_LEN 600

char buffer[BUFFER_LEN];

int main() {

    char *path = "/createdfile";
    char *path2 = "/createdfile2";

    assert(tfs_init(NULL) != -1);

    // Copies custom_input_1.txt to the tfs
    int f = tfs_copy_from_external_fs("tests/custom_input_1.txt", path);
    assert(f != -1);

    // Copies custom_input_2.txt to the tfs
    f = tfs_copy_from_external_fs("tests/custom_input_2.txt", path2);
    assert(f != -1);

    // Opens the file created as a copy of custom_input_1.txt
    f = tfs_open(path, TFS_O_CREAT);
    assert(f != -1);

    ssize_t read_bytes = tfs_read(f, buffer, BUFFER_LEN);
    assert(read_bytes != -1);

    while (read_bytes > 0) {
        // printf("%s", buffer);
        memset(buffer, 0, BUFFER_LEN);
        read_bytes = tfs_read(f, buffer, BUFFER_LEN);
        assert(read_bytes != -1);
    }

    memset(buffer, 0, BUFFER_LEN);

    assert(tfs_close(f) != -1);

    f = tfs_open(path2, TFS_O_CREAT);
    assert(f != -1);

    read_bytes = tfs_read(f, buffer, BUFFER_LEN);
    ssize_t total = 0;

    while (read_bytes > 0) {
        // printf("%s", buffer);
        total += read_bytes;
        memset(buffer, 0, BUFFER_LEN);
        read_bytes = tfs_read(f, buffer, BUFFER_LEN);
        assert(read_bytes != -1);
    }

    // Checks if the file has the correct size (1024 bytes) and if the file was
    // read correctly
    assert(total == 1024);

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}

//
//