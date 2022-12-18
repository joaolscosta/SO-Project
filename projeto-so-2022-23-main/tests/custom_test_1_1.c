#include "fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BUFFER_LEN 600

int main() {

    FILE *fd = fopen("custom_input_1.txt", "r");

    char *path = "/createdfile";
    char buffer[BUFFER_LEN];

    assert(tfs_init(NULL) != -1);

    int f = tfs_open(path, TFS_O_CREAT);
    assert(f != -1);

    memset(buffer, 0, sizeof(buffer));
    size_t bytes_read = fread(buffer, sizeof(char), strlen(buffer) + 1, fd);

    ssize_t written_size;

    while (bytes_read > 0) {
        /* read the contents of the file */
        written_size = tfs_write(f, buffer, strlen(buffer));
        assert(written_size == strlen(buffer));

        memset(buffer, 0, sizeof(buffer));

        bytes_read = fread(buffer, sizeof(char), strlen(buffer) + 1, fd);
    }

    assert(tfs_close(f) != -1);

    f = tfs_open(path, 0);
    assert(f != -1);

    ssize_t read;
    while ((read = tfs_read(f, buffer, BUFFER_LEN)) > 0)
        fwrite(buffer, sizeof(char), (size_t)read, stdout);

    assert(tfs_close(f) != -1);

    printf("\nSuccessful test.\n");

    return 0;
}

//