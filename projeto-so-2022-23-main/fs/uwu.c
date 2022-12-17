#include "betterassert.h"
#include "state.h"

#include "config.h"
#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 128

#include <assert.h>
#include <stdint.h>

#define PATH_FORMAT "/f%ld_%ld"
#define FILE_COUNT 5
#define MAX_PATH_SIZE 32

uint8_t const file_contents[] = "AAA!";

/*
 * Persistent FS state
 * (in reality, it should be maintained in secondary memory;
 * for simplicity, this project maintains it in primary memory).
 */
static tfs_params fs_params;

tfs_params tfs_default_params() {
    tfs_params params = {
        .max_inode_count = 64,
        .max_block_count = 1024,
        .max_open_files_count = 16,
        .block_size = 1024,
    };
    return params;
}

// Inode table
static inode_t *inode_table;
static allocation_state_t *freeinode_ts;

// Data blocks
static char *fs_data; // # blocks * block size
static allocation_state_t *free_blocks;

/*
 * Volatile FS state
 */
static open_file_entry_t *open_file_table;
static allocation_state_t *free_open_file_entries;

// Convenience macros
#define INODE_TABLE_SIZE (fs_params.max_inode_count)
#define DATA_BLOCKS (fs_params.max_block_count)
#define MAX_OPEN_FILES (fs_params.max_open_files_count)
#define BLOCK_SIZE (fs_params.block_size)
#define MAX_DIR_ENTRIES (BLOCK_SIZE / sizeof(dir_entry_t))

static inline bool valid_inumber(int inumber) {
    return inumber >= 0 && inumber < INODE_TABLE_SIZE;
}

static inline bool valid_block_number(int block_number) {
    return block_number >= 0 && block_number < DATA_BLOCKS;
}

static inline bool valid_file_handle(int file_handle) {
    return file_handle >= 0 && file_handle < MAX_OPEN_FILES;
}

size_t state_block_size(void) { return BLOCK_SIZE; }

/**
 * Do nothing, while preventing the compiler from performing any optimizations.
 *
 * We need to defeat the optimizer for the insert_delay() function.
 * Under optimization, the empty loop would be completely optimized away.
 * This function tells the compiler that the assembly code being run (which is
 * none) might potentially change *all memory in the process*.
 *
 * This prevents the optimizer from optimizing this code away, because it does
 * not know what it does and it may have side effects.
 *
 * Reference with more information: https://youtu.be/nXaxk27zwlk?t=2775
 *
 * Exercise: try removing this function and look at the assembly generated to
 * compare.
 */
static void touch_all_memory(void) { __asm volatile("" : : : "memory"); }

/**
 * Artifically delay execution (busy loop).
 *
 * Auxiliary function to insert a delay.
 * Used in accesses to persistent FS state as a way of emulating access
 * latencies as if such data structures were really stored in secondary memory.
 */
static void insert_delay(void) {
    for (int i = 0; i < DELAY; i++) {
        touch_all_memory();
    }
}

/**
 * Initialize FS state.
 *
 * Input:
 *   - params: TécnicoFS parameters
 *
 * Returns 0 if successful, -1 otherwise.
 *
 * Possible errors:
 *   - TFS already initialized.
 *   - malloc failure when allocating TFS structures.
 */
int state_init(tfs_params params) {
    fs_params = params;

    if (inode_table != NULL) {
        return -1; // already initialized
    }

    inode_table = malloc(INODE_TABLE_SIZE * sizeof(inode_t));
    freeinode_ts = malloc(INODE_TABLE_SIZE * sizeof(allocation_state_t));
    fs_data = malloc(DATA_BLOCKS * BLOCK_SIZE);
    free_blocks = malloc(DATA_BLOCKS * sizeof(allocation_state_t));
    open_file_table = malloc(MAX_OPEN_FILES * sizeof(open_file_entry_t));
    free_open_file_entries =
        malloc(MAX_OPEN_FILES * sizeof(allocation_state_t));

    if (!inode_table || !freeinode_ts || !fs_data || !free_blocks ||
        !open_file_table || !free_open_file_entries) {
        return -1; // allocation failed
    }

    for (size_t i = 0; i < INODE_TABLE_SIZE; i++) {
        freeinode_ts[i] = FREE;
    }

    for (size_t i = 0; i < DATA_BLOCKS; i++) {
        free_blocks[i] = FREE;
    }

    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        free_open_file_entries[i] = FREE;
    }

    return 0;
}

/**
 * Destroy FS state.
 *
 * Returns 0 if succesful, -1 otherwise.
 */
int state_destroy(void) {
    free(inode_table);
    free(freeinode_ts);
    free(fs_data);
    free(free_blocks);
    free(open_file_table);
    free(free_open_file_entries);

    inode_table = NULL;
    freeinode_ts = NULL;
    fs_data = NULL;
    free_blocks = NULL;
    open_file_table = NULL;
    free_open_file_entries = NULL;

    return 0;
}

/**
 * (Try to) Allocate a new inode in the inode table, without initializing its
 * data.
 *
 * Returns the inumber of the newly allocated inode, or -1 in the case of error.
 *
 * Possible errors:
 *   - No free slots in inode table.
 */
static int inode_alloc(void) {
    for (size_t inumber = 0; inumber < INODE_TABLE_SIZE; inumber++) {
        if ((inumber * sizeof(allocation_state_t) % BLOCK_SIZE) == 0) {
            insert_delay(); // simulate storage access delay (to freeinode_ts)
        }

        // Finds first free entry in inode table
        if (freeinode_ts[inumber] == FREE) {
            //  Found a free entry, so takes it for the new inode
            freeinode_ts[inumber] = TAKEN;

            return (int)inumber;
        }
    }

    // no free inodes
    return -1;
}

/**
 * Create a new inode in the inode table.
 *
 * Allocates and initializes a new inode.
 * Directories will have their data block allocated and initialized, with i_size
 * set to BLOCK_SIZE. Regular files will not have their data block allocated
 * (i_size will be set to 0, i_data_block to -1).
 *
 * Input:
 *   - i_type: the type of the node (file or directory)
 *
 * Returns inumber of the new inode, or -1 in the case of error.
 *
 * Possible errors:
 *   - No free slots in inode table.
 *   - (if creating a directory) No free data blocks.
 */
int inode_create(inode_type i_type) {
    int inumber = inode_alloc();
    if (inumber == -1) {
        return -1; // no free slots in inode table
    }

    inode_t *inode = &inode_table[inumber];
    insert_delay(); // simulate storage access delay (to inode)

    inode->i_node_type = i_type;
    switch (i_type) {
    case T_DIRECTORY: {
        // Initializes directory (filling its block with empty entries, labeled
        // with inumber==-1)
        int b = data_block_alloc();
        if (b == -1) {
            // ensure fields are initialized
            inode->i_size = 0;
            inode->i_data_block = -1;

            // run regular deletion process
            inode_delete(inumber);
            return -1;
        }

        inode_table[inumber].i_size = BLOCK_SIZE;
        inode_table[inumber].i_data_block = b;
        inode_table[inumber].hard_links = 1;

        dir_entry_t *dir_entry = (dir_entry_t *)data_block_get(b);
        ALWAYS_ASSERT(dir_entry != NULL,
                      "inode_create: data block freed while in use");

        for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {
            dir_entry[i].d_inumber = -1;
        }
    } break;
    case T_FILE:
        // In case of a new file, simply sets its size to 0
        inode_table[inumber].i_size = 0;
        inode_table[inumber].i_data_block = -1;
        inode_table[inumber].hard_links = 1;
        break;
    case T_LINK:
        // In case of a new file, simply sets its size to 0
        break;
    default:
        PANIC("inode_create: unknown file type");
    }

    return inumber;
}

/**
 * Delete an inode.
 *
 * Input:
 *   - inumber: inode's number
 */
void inode_delete(int inumber) {
    // simulate storage access delay (to inode and freeinode_ts)
    insert_delay();
    insert_delay();

    ALWAYS_ASSERT(valid_inumber(inumber), "inode_delete: invalid inumber");

    ALWAYS_ASSERT(freeinode_ts[inumber] == TAKEN,
                  "inode_delete: inode already freed");

    if (inode_table[inumber].i_size > 0) {
        data_block_free(inode_table[inumber].i_data_block);
    }

    freeinode_ts[inumber] = FREE;
}

/**
 * Obtain a pointer to an inode from its inumber.
 *
 * Input:
 *   - inumber: inode's number
 *
 * Returns pointer to inode.
 */
inode_t *inode_get(int inumber) {
    ALWAYS_ASSERT(valid_inumber(inumber), "inode_get: invalid inumber");

    insert_delay(); // simulate storage access delay to inode
    return &inode_table[inumber];
}

/**
 * Clear the directory entry associated with a sub file.
 *
 * Input:
 *   - inode: directory inode
 *   - sub_name: sub file name
 *
 * Returns 0 if successful, -1 otherwise.
 *
 * Possible errors:
 *   - inode is not a directory inode.
 *   - Directory does not contain an entry for sub_name.
 */
int clear_dir_entry(inode_t *inode, char const *sub_name) {
    insert_delay();
    if (inode->i_node_type != T_DIRECTORY) {
        return -1; // not a directory
    }

    // Locates the block containing the entries of the directory
    dir_entry_t *dir_entry = (dir_entry_t *)data_block_get(inode->i_data_block);
    ALWAYS_ASSERT(dir_entry != NULL,
                  "clear_dir_entry: directory must have a data block");

    for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (!strcmp(dir_entry[i].d_name, sub_name)) {
            dir_entry[i].d_inumber = -1;
            memset(dir_entry[i].d_name, 0, MAX_FILE_NAME);
            return 0;
        }
    }
    return -1; // sub_name not found
}

/**
 * Store the inumber for a sub file in a directory.
 *
 * Input:
 *   - inode: directory inode
 *   - sub_name: sub file name
 *   - sub_inumber: inumber of the sub inode
 *
 * Returns 0 if successful, -1 otherwise.
 *
 * Possible errors:
 *   - inode is not a directory inode.
 *   - sub_name is not a valid file name (length 0 or > MAX_FILE_NAME - 1).
 *   - Directory is already full of entries.
 */
int add_dir_entry(inode_t *inode, char const *sub_name, int sub_inumber) {
    if (strlen(sub_name) == 0 || strlen(sub_name) > MAX_FILE_NAME - 1) {
        return -1; // invalid sub_name
    }

    insert_delay(); // simulate storage access delay to inode with inumber
    if (inode->i_node_type != T_DIRECTORY) {
        return -1; // not a directory
    }

    // Locates the block containing the entries of the directory
    dir_entry_t *dir_entry = (dir_entry_t *)data_block_get(inode->i_data_block);
    ALWAYS_ASSERT(dir_entry != NULL,
                  "add_dir_entry: directory must have a data block");

    // Finds and fills the first empty entry
    for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (dir_entry[i].d_inumber == -1) {
            dir_entry[i].d_inumber = sub_inumber;
            strncpy(dir_entry[i].d_name, sub_name, MAX_FILE_NAME - 1);
            dir_entry[i].d_name[MAX_FILE_NAME - 1] = '\0';

            return 0;
        }
    }

    return -1; // no space for entry
}

/**
 * Obtain the inumber for a sub file inside a directory.
 *
 * Input:
 *   - inode: directory inode
 *   - sub_name: sub file name
 *
 * Returns inumber linked to the target name, -1 if errors occur.
 *
 * Possible errors:
 *   - inode is not a directory inode.
 *   - Directory does not contain a file named sub_name.
 */
int find_in_dir(inode_t const *inode, char const *sub_name) {
    ALWAYS_ASSERT(inode != NULL, "find_in_dir: inode must be non-NULL");
    ALWAYS_ASSERT(sub_name != NULL, "find_in_dir: sub_name must be non-NULL");

    insert_delay(); // simulate storage access delay to inode with inumber
    if (inode->i_node_type != T_DIRECTORY) {
        return -1; // not a directory
    }

    // Locates the block containing the entries of the directory
    dir_entry_t *dir_entry = (dir_entry_t *)data_block_get(inode->i_data_block);
    ALWAYS_ASSERT(dir_entry != NULL,
                  "find_in_dir: directory inode must have a data block");

    // Iterates over the directory entries looking for one that has the target
    // name
    for (int i = 0; i < MAX_DIR_ENTRIES; i++)
        if ((dir_entry[i].d_inumber != -1) &&
            (strncmp(dir_entry[i].d_name, sub_name, MAX_FILE_NAME) == 0)) {

            int sub_inumber = dir_entry[i].d_inumber;
            return sub_inumber;
        }

    return -1; // entry not found
}

/**
 * Allocate a new data block.
 *
 * Returns block number/index if successful, -1 otherwise.
 *
 * Possible errors:
 *   - No free data blocks.
 */
int data_block_alloc(void) {
    for (size_t i = 0; i < DATA_BLOCKS; i++) {
        if (i * sizeof(allocation_state_t) % BLOCK_SIZE == 0) {
            insert_delay(); // simulate storage access delay to free_blocks
        }

        if (free_blocks[i] == FREE) {
            free_blocks[i] = TAKEN;

            return (int)i;
        }
    }
    return -1;
}

/**
 * Free a data block.
 *
 * Input:
 *   - block_number: the block number/index
 */
void data_block_free(int block_number) {
    ALWAYS_ASSERT(valid_block_number(block_number),
                  "data_block_free: invalid block number");

    insert_delay(); // simulate storage access delay to free_blocks

    free_blocks[block_number] = FREE;
}

/**
 * Obtain a pointer to the contents of a given block.
 *
 * Input:
 *   - block_number: the block number/index
 *
 * Returns a pointer to the first byte of the block.
 */
void *data_block_get(int block_number) {
    ALWAYS_ASSERT(valid_block_number(block_number),
                  "data_block_get: invalid block number");

    insert_delay(); // simulate storage access delay to block
    return &fs_data[(size_t)block_number * BLOCK_SIZE];
}

/**
 * Add a new entry to the open file table.
 *
 * Input:
 *   - inumber: inode number of the file to open
 *   - offset: initial offset
 *
 * Returns file handle if successful, -1 otherwise.
 *
 * Possible errors:
 *   - No space in open file table for a new open file.
 */
int add_to_open_file_table(int inumber, size_t offset) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (free_open_file_entries[i] == FREE) {
            free_open_file_entries[i] = TAKEN;
            open_file_table[i].of_inumber = inumber;
            open_file_table[i].of_offset = offset;

            return i;
        }
    }

    return -1;
}

/**
 * Free an entry from the open file table.
 *
 * Input:
 *   - fhandle: file handle to free/close
 */
void remove_from_open_file_table(int fhandle) {
    ALWAYS_ASSERT(valid_file_handle(fhandle),
                  "remove_from_open_file_table: file handle must be valid");

    ALWAYS_ASSERT(free_open_file_entries[fhandle] == TAKEN,
                  "remove_from_open_file_table: file handle must be taken");

    free_open_file_entries[fhandle] = FREE;
}

/**
 * Obtain pointer to a given entry in the open file table.
 *
 * Input:
 *   - fhandle: file handle
 *
 * Returns pointer to the entry, or NULL if the fhandle is invalid/closed/never
 * opened.
 */
open_file_entry_t *get_open_file_entry(int fhandle) {
    if (!valid_file_handle(fhandle)) {
        return NULL;
    }

    if (free_open_file_entries[fhandle] != TAKEN) {
        return NULL;
    }

    return &open_file_table[fhandle];
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

    // create root inode
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
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
    int inum = tfs_lookup(name, root_dir_inode);
    size_t offset;

    if (inum >= 0) {
        // The file already exists
        inode_t *inode = inode_get(inum);
        ALWAYS_ASSERT(inode != NULL,
                      "tfs_open: directory files must have an inode");

        if (inode->i_node_type == T_LINK) {
            return tfs_open(inode->target_name, mode);
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
            return -1; // no space in inode table
        }

        // Add entry in the root directory
        if (add_dir_entry(root_dir_inode, name + 1, inum) == -1) {
            inode_delete(inum);
            return -1; // no space in directory
        }

        offset = 0;
    } else {
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
    //(void)target;
    //(void)link_name;
    // ^ this is a trick to keep the compiler from complaining about unused
    // variables. TODO: remove

    if (!valid_pathname(target) || !valid_pathname(link_name)) {
        return -1;
    }

    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);

    int link_inum = tfs_lookup(link_name, root_dir_inode);
    if (link_inum != -1) {
        return -1;
    }

    int target_inum = tfs_lookup(target, root_dir_inode);
    if (target_inum == -1) {
        return -1;
    }

    int link_inode_inum = inode_create(T_LINK);
    if (link_inode_inum == -1) {
        return -1;
    }

    inode_t *link_inode = inode_get(link_inode_inum);
    if (link_inode == NULL) {
        return -1;
    }

    strcpy(link_inode->target_name, target);

    if (add_dir_entry(root_dir_inode, link_name + 1, link_inode_inum) == -1) {
        return -1;
    }

    return 0;
}

int tfs_link(char const *target, char const *link_name) {
    //(void)target;
    //(void)link_name;
    // ^ this is a trick to keep the compiler from complaining about unused
    // variables. TODO: remove

    if (!valid_pathname(target) || !valid_pathname(link_name)) {
        return -1;
    }

    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);

    int target_inum = tfs_lookup(target, root_dir_inode);
    if (target_inum == -1) {
        return -1;
    }

    int link_inum = tfs_lookup(link_name, root_dir_inode);
    if (link_inum != -1) {
        return -1;
    }

    inode_t *target_file_inode = inode_get(target_inum);
    if (target_file_inode == NULL) {
        return -1;
    }

    if (target_file_inode->i_node_type == T_LINK) {
        return -1;
    }

    int check = add_dir_entry(root_dir_inode, link_name + 1, target_inum);
    if (check == -1) {
        return -1;
    }

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

    //  From the open file table entry, we get the inode
    inode_t *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_write: inode of open file deleted");

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
            inode->i_size = file->of_offset;
        }
    }

    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    // From the open file table entry, we get the inode
    inode_t const *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_read: inode of open file deleted");

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

    return (ssize_t)to_read;
}

int tfs_unlink(char const *target) {
    //(void)target;
    // ^ this is a trick to keep the compiler from complaining about unused
    // variables. TODO: remove

    if (!valid_pathname(target)) {
        return -1;
    }

    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);

    int target_inum = tfs_lookup(target, root_dir_inode);
    if (target_inum == -1) {
        return -1;
    }

    inode_t *target_file_inode = inode_get(target_inum);
    if (target_file_inode == NULL) {
        return -1;
    }

    if (target_file_inode->i_node_type != T_LINK) {
        if (target_file_inode->hard_links > 1) {
            target_file_inode->hard_links--;
        } else {
            inode_delete(target_inum);
        }
    }

    int res = clear_dir_entry(root_dir_inode, target + 1);
    if (res < 0) {
        return -1;
    }

    return 0;
}

// PANIC("TODO: tfs_unlink");

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
    while ((bytes_read = fread(buffer, sizeof(char), strlen(buffer) + 2,
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

void _format_path(char *dest, size_t max_size, size_t rep, size_t file_idx) {
    int ret = snprintf(dest, max_size, PATH_FORMAT, rep, file_idx);
    assert(ret > 0);
    assert(ret <= max_size);
}

int _open(size_t rep, size_t file_idx, tfs_file_mode_t mode) {
    char path[MAX_PATH_SIZE];
    _format_path(path, MAX_PATH_SIZE, rep, file_idx);
    return tfs_open(path, mode);
}

int _link(size_t rep_target, size_t file_idx_target, size_t rep_name,
          size_t file_idx_name) {
    char target[MAX_PATH_SIZE];
    _format_path(target, MAX_PATH_SIZE, rep_target, file_idx_target);

    char name[MAX_PATH_SIZE];
    _format_path(name, MAX_PATH_SIZE, rep_name, file_idx_name);

    return tfs_link(target, name);
}

int _unlink(size_t rep, size_t file_idx) {
    char path[MAX_PATH_SIZE];
    _format_path(path, MAX_PATH_SIZE, rep, file_idx);
    return tfs_unlink(path);
}

void assert_contents_ok(size_t rep, size_t file_idx) {
    int f = _open(rep, file_idx, 0);
    assert(f != -1);

    uint8_t buffer[sizeof(file_contents)];
    assert(tfs_read(f, buffer, sizeof(buffer)) == sizeof(buffer));
    assert(memcmp(buffer, file_contents, sizeof(buffer)) == 0);

    assert(tfs_close(f) != -1);
}

void assert_empty_file(size_t rep, size_t file_idx) {
    int f = _open(rep, file_idx, 0);
    assert(f != -1);

    uint8_t buffer[sizeof(file_contents)];
    assert(tfs_read(f, buffer, sizeof(buffer)) == 0);

    assert(tfs_close(f) != -1);
}

void write_contents(size_t rep, size_t file_idx) {
    int f = _open(rep, file_idx, 0);
    assert(f != -1);

    assert(tfs_write(f, file_contents, sizeof(file_contents)) ==
           sizeof(file_contents));

    assert(tfs_close(f) != -1);
}

void run_test(size_t rep) {
    // _open(rep, <number from 0 to FILE_COUNT>, mode) opens one of the
    // alternative names to the same file

    // confirm that all are empty
    for (size_t i = 0; i < FILE_COUNT; i++) {
        assert_empty_file(rep, i);
    }

    // delete half the links
    for (size_t i = FILE_COUNT / 2; i < FILE_COUNT; i++) {
        assert(_unlink(rep, i) != -1);
    }

    // write in one of the links
    write_contents(rep, 0);

    // confirm that the others see the write
    for (size_t i = 0; i < FILE_COUNT / 2; i++) {
        assert_contents_ok(rep, i);
    }

    // finish removing links
    for (size_t i = 0; i < FILE_COUNT / 2; i++) {
        assert(_unlink(rep, i) != -1);
    }
}

int main() {
    assert(tfs_init(NULL) != -1);

    for (size_t rep = 0; rep < FILE_COUNT; rep++) {
        // create original file
        {
            int fd = _open(rep, rep, TFS_O_CREAT);
            assert(fd != -1);
            assert(tfs_close(fd) != -1);
        }

        // create links
        for (size_t i = 0; i < FILE_COUNT; i++) {
            if (i == rep) {
                continue;
            }

            // instead of linking to the original always, let's create a link to
            // the previously linked name
            if (i == 0) {
                // no links exist yet, link to the original
                assert(_link(rep, rep, rep, i) != -1);
            } else {
                // link to the link created in the previous iteration
                assert(_link(rep, i - 1, rep, i) != -1);
            }
        }

        run_test(rep);
    }

    printf("Successful test.\n");

    return 0;
}