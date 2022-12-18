#include "fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BUFFER_LEN 600

char buffer[BUFFER_LEN];

int main() {

    char *path = "/createdfile";

    assert(tfs_init(NULL) != -1);

    int f = tfs_copy_from_external_fs("tests/custom_input_1.txt", path);
    assert(f != -1);

    f = tfs_open(path, TFS_O_CREAT);
    assert(f != -1);

    ssize_t read_bytes = tfs_read(f, buffer, BUFFER_LEN);
    assert(read_bytes != -1);

    while (read_bytes > 0) {
        printf("%s", buffer);
        memset(buffer, 0, BUFFER_LEN);
        read_bytes = tfs_read(f, buffer, BUFFER_LEN);
        assert(read_bytes != -1);
    }

    assert(tfs_close(f) != -1);

    assert(tfs_destroy() != -1);

    printf("\nSuccessful test.\n");

    return 0;
}

//