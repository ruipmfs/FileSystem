#ifndef STATE_H
#define STATE_H

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

/*
 * Directory entry
 */
typedef struct {
    char d_name[MAX_FILE_NAME];
    int d_inumber;
    pthread_mutex_t dir_entry_mutex;
    pthread_rwlock_t dir_entry_rwlock;
} dir_entry_t;

typedef enum { T_FILE, T_DIRECTORY } inode_type;

/*
 * I-node
 */
typedef struct {
    inode_type i_node_type;
    size_t i_size;
    int i_data_block; //current block in use to write
    int i_block[11];   // 10 primeiras entradas sao diretas
    pthread_mutex_t inode_mutex;
    pthread_rwlock_t inode_rwlock;
    /* in a real FS, more fields would exist here */
} inode_t;

typedef enum { FREE = 0, TAKEN = 1 } allocation_state_t;

/*
 * Open file entry (in open file table)
 * of_inumber : entry number
 * of_offset : current offset position
 */
typedef struct {
    int of_inumber;
    size_t of_offset;
    pthread_mutex_t open_file_mutex;
    pthread_rwlock_t open_file_rwlock;
} open_file_entry_t;

typedef enum { READ = 1, WRITE = 2, MUTEX = 3 } lock_state_t;


#define MAX_DIR_ENTRIES (BLOCK_SIZE / sizeof(dir_entry_t))


void state_init();
void state_destroy();

int inode_create(inode_type n_type);
int inode_delete(int inumber);
inode_t *inode_get(int inumber);

int clear_dir_entry(int inumber, int sub_inumber);
int add_dir_entry(int inumber, int sub_inumber, char const *sub_name);
int find_in_dir(int inumber, char const *sub_name);

int data_block_alloc();
int data_block_free(int block_number);
void *data_block_get(int block_number);
int data_block_insert(int i_block[], int block_number);
int index_block_insert(int index_block[], int block_number);

int add_to_open_file_table(int inumber, size_t offset);
int remove_from_open_file_table(int fhandle);
open_file_entry_t *get_open_file_entry(int fhandle);


ssize_t tfs_write_direct_region(inode_t *inode, open_file_entry_t *file, void const *buffer, size_t write_size);
int direct_block_insert(inode_t *inode);
ssize_t tfs_write_indirect_region(inode_t *inode, open_file_entry_t *file, void const *buffer, size_t write_size);
int indirect_block_insert(inode_t *inode);
int tfs_handle_indirect_block(inode_t *inode);
ssize_t tfs_read_direct_region(open_file_entry_t *file, size_t to_read, void *buffer);
ssize_t tfs_read_indirect_region(open_file_entry_t *file, size_t to_read, void *buffer);

int inode_lock(inode_t *inode, lock_state_t lock_state);
int inode_unlock(inode_t *inode, lock_state_t lock_state);
int open_file_lock(open_file_entry_t *open_file_entry, lock_state_t lock_state);
int open_file_unlock(open_file_entry_t *open_file_entry, lock_state_t lock_state);
int inode_allocation_map_lock(lock_state_t lock_state);
int inode_allocation_map_unlock(lock_state_t lock_state);
int file_allocation_map_lock(lock_state_t lock_state);
int file_allocation_map_unlock(lock_state_t lock_state);

#endif // STATE_H
