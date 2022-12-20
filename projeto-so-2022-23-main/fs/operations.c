#include "operations.h"
#include "config.h"
#include "locks.h"
#include "state.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betterassert.h"

#define BUFFER_SIZE 128

// mutex to protect the opened files table
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

tfs_params tfs_default_params() {
    tfs_params params = {
        .max_inode_count = 64,
        .max_block_count = 1024,
        .max_open_files_count = 16,
        .block_size = 1024,
    };
    return params;
}

int tfs_init(tfs_params const *params_ptr) {
    tfs_params params;
    if (params_ptr != NULL) {
        params = *params_ptr;
    } else {
        params = tfs_default_params();
    }

    if (state_init(params) != 0) {
        return -1;
    }

    if (pthread_mutex_init(&mutex, NULL) != 0) {
        perror("pthread_mutex_init");
        exit(EXIT_FAILURE);
    }

    // create root inode
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {

    if (pthread_mutex_destroy(&mutex) != 0) {
        perror("pthread_mutex_init");
        exit(EXIT_FAILURE);
    }

    if (state_destroy() != 0) {
        return -1;
    }
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}

/**
 * Looks for a file.
 *
 * Note: as a simplification, only a plain directory space (root directory only)
 * is supported.
 *
 * Input:
 *   - name: absolute path name
 *   - root_inode: the root directory inode
 * Returns the inumber of the file, -1 if unsuccessful.
 */
static int tfs_lookup(char const *name, inode_t const *root_inode) {
    // TODO: assert that root_inode is the root directory
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);

    if (memcmp(root_inode, root_dir_inode, MAX_FILE_NAME) != 0) {
        return -1;
    }

    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(root_inode, name);
}

int tfs_open(char const *name, tfs_file_mode_t mode) {
    // Checks if the path name is valid
    if (!valid_pathname(name)) {
        return -1;
    }

    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    ALWAYS_ASSERT(root_dir_inode != NULL,
                  "tfs_open: root dir inode must exist");

    // Locks the opened files table
    mutex_lock(&mutex);

    int inum = tfs_lookup(name, root_dir_inode);
    size_t offset;

    if (inum >= 0) {
        mutex_unlock(&mutex);
        // The file already exists
        inode_t *inode = inode_get(inum);
        ALWAYS_ASSERT(inode != NULL,
                      "tfs_open: directory files must have an inode");

        if (inode->i_node_type == T_LINK) {
            return tfs_open(data_block_get(inode->i_data_block), mode);
        }

        // Truncate (if requested)
        if (mode & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                data_block_free(inode->i_data_block);
                inode->i_size = 0;
            }
        }
        // Determine initial offset
        if (mode & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
    } else if (mode & TFS_O_CREAT) {
        // The file does not exist; the mode specified that it should be created
        // Create inode
        inum = inode_create(T_FILE);
        if (inum == -1) {
            mutex_unlock(&mutex);
            return -1; // no space in inode table
        }

        // Add entry in the root directory

        if (add_dir_entry(root_dir_inode, name + 1, inum) == -1) {
            mutex_unlock(&mutex);
            inode_delete(inum);
            return -1; // no space in directory
        }

        // Unlocks the opened files table
        mutex_unlock(&mutex);
        offset = 0;
    } else {

        //
        mutex_unlock(&mutex);
        return -1;
    }

    // Finally, add entry to the open file table and return the corresponding
    // handle
    return add_to_open_file_table(inum, offset);

    // Note: for simplification, if file was created with TFS_O_CREAT and there
    // is an error adding an entry to the open file table, the file is not
    // opened but it remains created
}

int tfs_sym_link(char const *target, char const *link_name) {

    // Checks if the path names are valid
    if (!valid_pathname(target) || !valid_pathname(link_name)) {
        return -1;
    }

    // get's the root directory inode
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);

    // gets the inumber for the new link
    int link_inum = tfs_lookup(link_name, root_dir_inode);
    if (link_inum != -1) {
        return -1;
    }

    // gets the inumber for the target file
    int target_inum = tfs_lookup(target, root_dir_inode);
    if (target_inum == -1) {
        return -1;
    }

    // Creates a new inode with type T_LINK (Symbolic Link)
    int link_inode_inum = inode_create(T_LINK);
    if (link_inode_inum == -1) {
        return -1;
    }

    // gets the inode for the new link
    inode_t *link_inode = inode_get(link_inode_inum);
    if (link_inode == NULL) {
        return -1;
    }

    // copies the target file path into the link's data block
    strcpy(data_block_get(link_inode->i_data_block), target);

    // adds the link to the directory
    if (add_dir_entry(root_dir_inode, link_name + 1, link_inode_inum) == -1) {
        return -1;
    }

    return 0;
}

int tfs_link(char const *target, char const *link_name) {

    // Checks if the pathnames are valid
    if (!valid_pathname(target) || !valid_pathname(link_name)) {
        return -1;
    }

    // get's the root directory inode
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);

    // gets the inumber for the new link
    int target_inum = tfs_lookup(target, root_dir_inode);
    if (target_inum == -1) {
        return -1;
    }

    // gets the inumber for the target file
    int link_inum = tfs_lookup(link_name, root_dir_inode);
    if (link_inum != -1) {
        return -1;
    }

    // Gets the inode for the target file
    inode_t *target_file_inode = inode_get(target_inum);
    if (target_file_inode == NULL) {
        return -1;
    }

    // Does not allow HardLinks to be created for symbolic links
    if (target_file_inode->i_node_type == T_LINK) {
        return -1;
    }

    // Adds the link to the directory
    int check = add_dir_entry(root_dir_inode, link_name + 1, target_inum);
    if (check == -1) {
        return -1;
    }

    // Increments the number of hard links for the target file
    target_file_inode->hard_links++;

    return 0;
}

int tfs_close(int fhandle) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1; // invalid fd
    }

    remove_from_open_file_table(fhandle);

    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {

    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&file->lock) != 0) {
        perror("pthread_mutex_lock");
        exit(EXIT_FAILURE);
    }

    //  From the open file table entry, we get the inode

    int inum = file->of_inumber;
    inode_t *inode = inode_get(inum);
    ALWAYS_ASSERT(inode != NULL, "tfs_write: inode of open file deleted");

    inode_lock(inum, 1); // locks inode for writing

    // Determine how many bytes to write
    size_t block_size = state_block_size();
    if (to_write + file->of_offset > block_size) {
        to_write = block_size - file->of_offset;
    }

    if (to_write > 0) {
        if (inode->i_size == 0) {
            // If empty file, allocate new block
            int bnum = data_block_alloc();
            if (bnum == -1) {
                inode_unlock(inum);
                if (pthread_mutex_unlock(&file->lock) != 0) {
                    perror("pthread_mutex_unlock");
                    exit(EXIT_FAILURE);
                }
                return -1; // no space
            }

            inode->i_data_block = bnum;
        }

        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_write: data block deleted mid-write");

        // Perform the actual write
        memcpy(block + file->of_offset, buffer, to_write);

        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_write;
        if (file->of_offset > inode->i_size) {
            // inode i_size is updated
            inode->i_size = file->of_offset;
        }
    }

    inode_unlock(inum);
    if (pthread_mutex_unlock(&file->lock) != 0) {
        perror("pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }
    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {

    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&file->lock) != 0) {
        perror("pthread_mutex_lock");
        exit(EXIT_FAILURE);
    }

    int inum = file->of_inumber;
    // From the open file table entry, we get the inode
    inode_t const *inode = inode_get(inum);
    ALWAYS_ASSERT(inode != NULL, "tfs_read: inode of open file deleted");

    inode_lock(inum, 0);

    // Determine how many bytes to read
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    if (to_read > 0) {
        void *block = data_block_get(inode->i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_read: data block deleted mid-read");

        // Perform the actual read
        memcpy(buffer, block + file->of_offset, to_read);
        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_read;
    }

    if (pthread_mutex_unlock(&file->lock) != 0) {
        perror("pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }

    inode_unlock(inum);
    return (ssize_t)to_read;
}

int tfs_unlink(char const *target) {

    // Checks if the given path is a valid pathname
    if (!valid_pathname(target)) {
        return -1;
    }

    // Gets the root directory inode
    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);

    // Gets the inumber of the target file
    int target_inum = tfs_lookup(target, root_dir_inode);
    if (target_inum == -1) {
        return -1;
    }

    // Gets the inode for the target file
    inode_t *target_file_inode = inode_get(target_inum);
    if (target_file_inode == NULL) {
        return -1;
    }

    // Checks if the target inode is of a Symbolic link
    if (target_file_inode->i_node_type != T_LINK) {

        // If it is a hard link, decrement the hard link count
        if (target_file_inode->hard_links > 1) {
            target_file_inode->hard_links--;
        } else { // If the hard link count is 1, delete the file
            inode_delete(target_inum);
        }
    }

    // Deletes target file from the given directory
    int res = clear_dir_entry(root_dir_inode, target + 1);
    if (res < 0) {
        return -1;
    }

    return 0;
}

int tfs_copy_from_external_fs(char const *source_path, char const *dest_path) {
    // (void)source_path;
    // (void)dest_path;

    FILE *f_to_read = fopen(source_path, "r");

    // Check if the input file was successfully opened
    if (f_to_read == NULL) {
        return -1;
    }

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    int f_to_write = tfs_open(dest_path, TFS_O_CREAT | TFS_O_TRUNC);

    // Check if the output file was successfully opened
    if (f_to_write < 0) {
        fclose(f_to_read);
        return -1;
    }

    size_t bytes_read;

    // Read data from the input file and store it in the buffer
    while ((bytes_read = fread(buffer, sizeof(char), strlen(buffer) + 1,
                               f_to_read)) != 0) {
        // Write the data from the buffer to the output file using tfs_write()
        tfs_write(f_to_write, buffer, strlen(buffer));
        memset(buffer, 0, sizeof(buffer));
    }

    // Close the input and output files
    fclose(f_to_read);
    tfs_close(f_to_write);

    return 0;
}
