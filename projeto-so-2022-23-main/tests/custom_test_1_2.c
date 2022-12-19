#include "fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BUFFER_LEN 600

// This test creates a file, and tests some possible errors related to linking a
// file

int main() {

    assert(tfs_init(NULL) != -1);

    assert(tfs_unlink("/") == -1);

    int f1 = tfs_open("/f1", TFS_O_CREAT);
    assert(f1 != -1);

    assert(tfs_close(f1) != -1);

    assert(tfs_link("/f1", "/h1") != -1);

    assert(tfs_link("/h1", "/h2") != -1);

    assert(tfs_sym_link("/f1", "/s1") != -1);

    assert(tfs_sym_link("/h1", "/s2") != -1);

    assert(tfs_sym_link("/h2", "/s3") != -1);

    assert(tfs_open("/h2", 0) != -1);

    assert(tfs_open("/s1", 0) != -1);

    assert(tfs_open("/s2", 0) != -1);

    assert(tfs_unlink("/h1") != -1);

    assert(tfs_open("/s2", 0) == -1);

    assert(tfs_open("/h1", 0) == -1);

    assert(tfs_open("/h2", 0) != -1);

    assert(tfs_open("/s3", 0) != -1);

    assert(tfs_unlink("/f1") != -1);

    assert(tfs_open("/h1", 0) == -1);

    assert(tfs_open("/s1", 0) == -1);

    assert(tfs_open("/s3", 0) != -1);

    assert(tfs_open("/f1", 0) == -1);

    assert(tfs_open("/h2", 0) != -1);

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}