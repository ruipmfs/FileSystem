#include "state.h"

#define FIRST_INDIRECT_BLOCK (12)
#define REFERENCE_BLOCK_INDEX (11)

/* Persistent FS state  (in reality, it should be maintained in secondary
 * memory; for simplicity, this project maintains it in primary memory) */

/* I-node table */
typedef struct {
    inode_t inode_table[INODE_TABLE_SIZE];
    allocation_state_t freeinode_ts[INODE_TABLE_SIZE];
    pthread_mutex_t inode_table_mutex;
    pthread_rwlock_t inode_table_rwlock;
} inode_table_t;

static inode_table_t inode_table_s;

typedef struct {
    allocation_state_t fs_data[BLOCK_SIZE * DATA_BLOCKS];
    allocation_state_t free_blocks[DATA_BLOCKS];
    pthread_mutex_t data_blocks_mutex;
} data_blocks_t;

static data_blocks_t data_blocks_s;

/* Volatile FS state */

typedef struct {
    open_file_entry_t open_file_table[MAX_OPEN_FILES];
    char free_open_file_entries[MAX_OPEN_FILES]; 
    pthread_mutex_t fs_state_mutex; 
    pthread_rwlock_t fs_state_rwlock; 
} fs_state_t;

static fs_state_t fs_state_s;

static inline bool valid_inumber(int inumber) {
    return inumber >= 0 && inumber < INODE_TABLE_SIZE;
}

static inline bool valid_block_number(int block_number) {
    return block_number >= 0 && block_number < DATA_BLOCKS;
}

static inline bool valid_file_handle(int file_handle) {
    return file_handle >= 0 && file_handle < MAX_OPEN_FILES;
}

/**
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
static void touch_all_memory() { __asm volatile("" : : : "memory"); }

/*
 * Auxiliary function to insert a delay.
 * Used in accesses to persistent FS state as a way of emulating access
 * latencies as if such data structures were really stored in secondary memory.
 */
static void insert_delay() {
    for (int i = 0; i < DELAY; i++) {
        touch_all_memory();
    }
}

/*
 * Initializes FS state
 */
void state_init() {

    pthread_mutex_init(&(inode_table_s.inode_table_mutex), NULL);
    pthread_rwlock_init(&(inode_table_s.inode_table_rwlock), NULL);

    for (size_t i = 0; i < INODE_TABLE_SIZE; i++) {
        inode_table_s.freeinode_ts[i] = FREE;
        pthread_mutex_init(&(inode_table_s.inode_table[i].inode_mutex), NULL);
        pthread_rwlock_init(&(inode_table_s.inode_table[i].inode_rwlock), NULL);
    }

    pthread_mutex_init(&(data_blocks_s.data_blocks_mutex), NULL);

    for (size_t i = 0; i < DATA_BLOCKS; i++) {
        data_blocks_s.free_blocks[i] = FREE;
    }

    pthread_mutex_init(&(fs_state_s.fs_state_mutex), NULL);
    pthread_rwlock_init(&(fs_state_s.fs_state_rwlock), NULL);

    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        fs_state_s.free_open_file_entries[i] = FREE;
        pthread_mutex_init(&(fs_state_s.open_file_table[i].open_file_mutex), NULL);
        pthread_rwlock_init(&(fs_state_s.open_file_table[i].open_file_rwlock), NULL);
    }
}

void state_destroy() { 

    pthread_mutex_destroy(&(inode_table_s.inode_table_mutex));
    pthread_rwlock_destroy(&(inode_table_s.inode_table_rwlock));

    for (size_t i = 0; i < INODE_TABLE_SIZE; i++) {
        pthread_mutex_destroy(&(inode_table_s.inode_table[i].inode_mutex));
        pthread_rwlock_destroy(&(inode_table_s.inode_table[i].inode_rwlock));
    }

    pthread_mutex_destroy(&(fs_state_s.fs_state_mutex));
    pthread_rwlock_destroy(&(fs_state_s.fs_state_rwlock));

    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        pthread_mutex_destroy(&(fs_state_s.open_file_table[i].open_file_mutex));
        pthread_rwlock_destroy(&(fs_state_s.open_file_table[i].open_file_rwlock));

    }

    pthread_mutex_destroy(&(data_blocks_s.data_blocks_mutex));
}

/*
 * Creates a new i-node in the i-node table.
 * Input:
 *  - n_type: the type of the node (file or directory)
 * Returns:
 *  new i-node's number if successfully created, -1 otherwise
 */

int inode_create(inode_type n_type) {
    for (int inumber = 0; inumber < INODE_TABLE_SIZE; inumber++) {
        if ((inumber * (int) sizeof(allocation_state_t) % BLOCK_SIZE) == 0) {
            insert_delay(); // simulate storage access delay (to freeinode_ts)
        }

        pthread_mutex_lock(&inode_table_s.inode_table_mutex);

        // Finds first free entry in i-node table 
        if (inode_table_s.freeinode_ts[inumber] == FREE) {      
            // Found a free entry, so takes it for the new i-node
            inode_table_s.freeinode_ts[inumber] = TAKEN;

            insert_delay(); // simulate storage access delay (to i-node)

            inode_t *local_inode = &(inode_table_s.inode_table[inumber]);

            local_inode->i_node_type = n_type;

            if (n_type == T_DIRECTORY) {
                // Initializes directory (filling its block with empty
                // entries, labeled with inumber==-1) 
                int b = data_block_alloc();
                if (b == -1 && ((dir_entry_t *)data_block_get(b)) == NULL) {
                    inode_table_s.freeinode_ts[inumber] = FREE;
                    return -1;
                }

                local_inode->i_size = BLOCK_SIZE;
                local_inode->i_data_block = b;
                memset(local_inode->i_block, -1, sizeof(local_inode->i_block));

                dir_entry_t *dir_entry = (dir_entry_t *)data_block_get(b);

                for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {
                    dir_entry[i].d_inumber = -1;
                }
            } else {
                // In case of a new file, simply sets its size to 0 
                local_inode->i_size = 0;
                local_inode->i_data_block = -1;
                memset(local_inode->i_block, -1, sizeof(local_inode->i_block));

            }

            pthread_mutex_unlock(&inode_table_s.inode_table_mutex);

            return inumber;
        }

        pthread_mutex_unlock(&inode_table_s.inode_table_mutex);

    }
    return -1;
}


/*
 * Deletes the i-node.
 * Input:
 *  - inumber: i-node's number
 * Returns: 0 if successful, -1 if failed
 */
int inode_delete(int inumber) {
    // simulate storage access delay (to i-node and freeinode_ts)
    insert_delay();
    insert_delay();
    
    pthread_mutex_lock(&(inode_table_s.inode_table_mutex));

    if (!valid_inumber(inumber) || inode_table_s.freeinode_ts[inumber] == FREE) {
        pthread_mutex_unlock(&(inode_table_s.inode_table_mutex));
        return -1;
    }

    inode_table_s.freeinode_ts[inumber] = FREE;

    inode_t *local_inode = &inode_table_s.inode_table[inumber];

    if (local_inode->i_size > 0 && (data_block_free(local_inode->i_data_block) == -1)) {
        pthread_mutex_unlock(&(inode_table_s.inode_table_mutex));
        return -1;
    }

    pthread_mutex_unlock(&(inode_table_s.inode_table_mutex));

    return 0;
}

/*
 * Returns a pointer to an existing i-node.
 * Input:
 *  - inumber: identifier of the i-node
 * Returns: pointer if successful, NULL if failed
 */
inode_t *inode_get(int inumber) {
    if (!valid_inumber(inumber)) {
        return NULL;
    }
    
    insert_delay(); // simulate storage access delay to i-node

    return &(inode_table_s.inode_table[inumber]);
}

/*
 * Adds an entry to the i-node directory data.
 * Input:
 *  - inumber: identifier of the i-node
 *  - sub_inumber: identifier of the sub i-node entry
 *  - sub_name: name of the sub i-node entry
 * Returns: SUCCESS or FAIL
 */
int add_dir_entry(int inumber, int sub_inumber, char const *sub_name) {
    if (!valid_inumber(inumber) || !valid_inumber(sub_inumber)) {
        return -1;
    }

    pthread_rwlock_rdlock(&(inode_table_s.inode_table_rwlock));

    inode_t *local_inode = &(inode_table_s.inode_table[inumber]);

    insert_delay(); // simulate storage access delay to i-node with inumber
    if (local_inode->i_node_type != T_DIRECTORY) {
        pthread_rwlock_unlock(&(inode_table_s.inode_table_rwlock));
        return -1;
    }

    if (strlen(sub_name) == 0) {
        pthread_rwlock_unlock(&(inode_table_s.inode_table_rwlock));
        return -1;
    }

    /* Locates the block containing the directory's entries */
    dir_entry_t *dir_entry =
        (dir_entry_t *)data_block_get(local_inode->i_data_block);

    pthread_rwlock_unlock(&(inode_table_s.inode_table_rwlock));

    if (dir_entry == NULL) {
        return -1;
    }

    /* Finds and fills the first empty entry */
    pthread_mutex_lock(&(fs_state_s.fs_state_mutex))
        
    for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {

        if (dir_entry[i].d_inumber == -1) {

            dir_entry[i].d_inumber = sub_inumber;

            strncpy(dir_entry[i].d_name, sub_name, MAX_FILE_NAME - 1);

            dir_entry[i].d_name[MAX_FILE_NAME - 1] = 0;

            pthread_mutex_unlock(&(fs_state_s.fs_state_mutex));

            return 0;
        }
    }
    pthread_mutex_unlock(&(fs_state_s.fs_state_mutex));

    return -1;
}

/* Looks for a given name inside a directory
 * Input:
 * 	- parent directory's i-node number
 * 	- name to search
 * 	Returns i-number linked to the target name, -1 if not found
 */
int find_in_dir(int inumber, char const *sub_name) {
    insert_delay(); // simulate storage access delay to i-node with inumber

    pthread_rwlock_rdlock(&(inode_table_s.inode_table_rwlock));

    if (!valid_inumber(inumber) ||
        inode_table_s.inode_table[inumber].i_node_type != T_DIRECTORY) {
        pthread_rwlock_unlock(&(inode_table_s.inode_table_rwlock));
        return -1;
    }

    /* Locates the block containing the DIRECTORY's entries */
    dir_entry_t *dir_entry =
        (dir_entry_t *)data_block_get(inode_table_s.inode_table[inumber].i_data_block);

        pthread_rwlock_unlock(&(inode_table_s.inode_table_rwlock));

    if (dir_entry == NULL) {
        return -1;
    }

    /* Iterates over the directory entries looking for one that has the target
     * name */
    pthread_mutex_lock(&(fs_state_s.fs_state_mutex));

    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {

        if ((dir_entry[i].d_inumber != -1) &&
            (strncmp(dir_entry[i].d_name, sub_name, MAX_FILE_NAME) == 0)) {

            pthread_mutex_unlock(&(fs_state_s.fs_state_mutex));
            return dir_entry[i].d_inumber;
        }
    }
    pthread_mutex_unlock(&(fs_state_s.fs_state_mutex));

    return -1;
}

/*
 * Allocated a new data block
 * Returns: block index if successful, -1 otherwise
 */
// ignorar os locks?
int data_block_alloc() {
        
    for (int i = 0; i < DATA_BLOCKS; i++) {
        if (i * (int) sizeof(allocation_state_t) % BLOCK_SIZE == 0) {
            insert_delay(); // simulate storage access delay to free_blocks
        }

        pthread_mutex_lock(&(data_blocks_s.data_blocks_mutex));

        if (data_blocks_s.free_blocks[i] == FREE) {
            data_blocks_s.free_blocks[i] = TAKEN;
       
            pthread_mutex_unlock(&(data_blocks_s.data_blocks_mutex));
            return i;
        }

        pthread_mutex_unlock(&(data_blocks_s.data_blocks_mutex));
 
    }
    return -1;
}

/* Frees a data block
 * Input
 * 	- the block index
 * Returns: 0 if success, -1 otherwise
 */
int data_block_free(int block_number) {

    if (!valid_block_number(block_number)) {
        return -1;
    }

    insert_delay(); // simulate storage access delay to free_blocks

    pthread_mutex_lock(&(data_blocks_s.data_blocks_mutex));

    data_blocks_s.free_blocks[block_number] = FREE;

    pthread_mutex_unlock(&(data_blocks_s.data_blocks_mutex));

    return 0;
}

/* Returns a pointer to the contents of a given block
 * Input:
 * 	- Block's index
 * Returns: pointer to the first byte of the block, NULL otherwise
 */
void *data_block_get(int block_number) {
    if (!valid_block_number(block_number)) {
        return NULL;
    }

    insert_delay(); // simulate storage access delay to block

    return &(data_blocks_s.fs_data[block_number * BLOCK_SIZE]);
}

/* Add new entry to the open file table
 * Inputs:
 * 	- I-node number of the file to open
 * 	- Initial offset
 * Returns: file handle if successful, -1 otherwise
 */
int add_to_open_file_table(int inumber, size_t offset) {

    for (int i = 0; i < MAX_OPEN_FILES; i++) {

        if (fs_state_s.free_open_file_entries[i] == FREE) {
            fs_state_s.free_open_file_entries[i] = TAKEN;
            fs_state_s.open_file_table[i].of_inumber = inumber;
            fs_state_s.open_file_table[i].of_offset = offset;

            return i;
        }       

    }

    return -1;
}

/* Frees an entry from the open file table
 * Inputs:
 * 	- file handle to free/close
 * Returns 0 is success, -1 otherwise
 */
int remove_from_open_file_table(int fhandle) {

    pthread_mutex_lock(&(fs_state_s.fs_state_mutex));

    if (!valid_file_handle(fhandle) ||
        fs_state_s.free_open_file_entries[fhandle] != TAKEN) {

        pthread_mutex_unlock(&(fs_state_s.fs_state_mutex));

        return -1;
    }

    fs_state_s.free_open_file_entries[fhandle] = FREE;

    pthread_mutex_unlock(&(fs_state_s.fs_state_mutex));

    return 0;
}

/* Returns pointer to a given entry in the open file table
 * Inputs:
 * 	 - file handle
 * Returns: pointer to the entry if sucessful, NULL otherwise
 */
open_file_entry_t *get_open_file_entry(int fhandle) {
    if (!valid_file_handle(fhandle)) {
        return NULL;
    }

    return &(fs_state_s.open_file_table[fhandle]);
}



// ------------------------------- AUX FUNCTIONS ---------------------------------------------

/* Writes in the direct region
 * Inputs:
 * 	 - inode
 *   - pointer to the file entry
 *   - buffer
 *   - n of bytes to write
 * Returns: total of written bytes if sucessful, -1 otherwise
 */
ssize_t tfs_write_direct_region(inode_t *inode, open_file_entry_t *file, void const *buffer, size_t write_size) {

    size_t bytes_written = 0;
    size_t to_write_block = 0;

    for (int i = 0; write_size > 0 && i < REFERENCE_BLOCK_INDEX; i++) {

        if (inode->i_size % BLOCK_SIZE == 0) {                                                           
            int insert_status = direct_block_insert(inode);     
            if (insert_status == -1) {
                printf("[ tfs_write_direct_region ] Error writing in direct region: %s\n", strerror(errno));
                return -1;
            }
        }

        void *block = data_block_get(inode->i_data_block);

        if (block == NULL) {
            return -1;
        }
        
        if (write_size >= BLOCK_SIZE || BLOCK_SIZE - (file->of_offset % BLOCK_SIZE) < write_size) {
            to_write_block = BLOCK_SIZE - (file->of_offset % BLOCK_SIZE);
            write_size -= to_write_block;

        } else  {   
            to_write_block = write_size;
            write_size = 0;
        }

        memcpy(block + (file->of_offset % BLOCK_SIZE), buffer + bytes_written, to_write_block);

        file->of_offset += to_write_block;
        inode->i_size += to_write_block;
        bytes_written += to_write_block;

    }

    return (ssize_t)bytes_written;
}

/* 
 *  INSERTS
 */
int direct_block_insert(inode_t *inode) {

    inode->i_data_block = data_block_alloc();

    void *block = data_block_get(inode->i_data_block);

    if (inode->i_data_block == -1) {
        printf("[ direct_block_insert ] Error : alloc block failed\n");
        return -1;
    }

    memset(block, -1, BLOCK_SIZE);

    inode->i_block[inode->i_data_block - 1] = inode->i_data_block;

    return 0;
}

/* Writes in the indirect region
 * Inputs:
 * 	 - inode
 *   - pointer to the file entry
 *   - buffer
 *   - n of bytes to write
 * Returns: total of written bytes if sucessful, -1 otherwise
 */
ssize_t tfs_write_indirect_region(inode_t *inode, open_file_entry_t *file, void const *buffer, size_t write_size) {

    size_t bytes_written = 0;
    size_t to_write_block = 0;
    int insert_status = 0;

    for (int i = 0; write_size > 0; i++) {

        if (inode->i_size + write_size > MAX_BYTES) {
            write_size = MAX_BYTES - inode->i_size;
        }

        if (inode->i_size % BLOCK_SIZE == 0) { 

            insert_status = indirect_block_insert(inode);  

            if (insert_status == -1) {
                printf("[ tfs_write_indirect_region ] Error writing in indirect region: %s\n", strerror(errno));
                return -1;
            }
        }

        void *block = data_block_get(inode->i_data_block);

        if (block == NULL) {
            printf("[ tfs_write_indirect_region ] Error : NULL block\n");
            return -1;
        }
        
        if (write_size >= BLOCK_SIZE || BLOCK_SIZE - (file->of_offset % BLOCK_SIZE) < write_size) {
            to_write_block = BLOCK_SIZE - (file->of_offset % BLOCK_SIZE);           
            write_size -= to_write_block;
        }

        else  {
            to_write_block = write_size;
            write_size = 0;
        }


        memcpy(block + (file->of_offset % BLOCK_SIZE), buffer + bytes_written, to_write_block);


        file->of_offset += to_write_block;
        inode->i_size += to_write_block;
        bytes_written += to_write_block;

    
    }
  
    return (ssize_t)bytes_written;
}

int indirect_block_insert(inode_t *inode) {

    int *last_i_block = (int *)data_block_get(inode->i_block[MAX_DIRECT_BLOCKS]);

    int block_number = data_block_alloc();

    if (block_number == -1) {
        printf(" Error : Invalid block insertion\n");
        return -1;
    }

    inode->i_data_block = block_number;

    memset(data_block_get(block_number), -1, BLOCK_SIZE / sizeof(int));

    last_i_block[block_number - FIRST_INDIRECT_BLOCK] = block_number;    

    return 0;

}

int tfs_handle_indirect_block(inode_t *inode) {

    int block_number = data_block_alloc();

    if (block_number == -1) {
        return -1;
    }

    inode->i_block[MAX_DIRECT_BLOCKS] = block_number;
    inode->i_data_block = block_number;

    memset(data_block_get(inode->i_data_block), -1, sizeof(data_block_get(inode->i_data_block)));

    return 0;
}

/* Reads from the direct region a certain amount of bytes to a buffer
 * Inputs:
 *   - pointer to the file entry
 *   - n bytes to read
 *   - buffer
 * Returns: total of read bytes if sucessful, -1 otherwise
 */
ssize_t tfs_read_direct_region(open_file_entry_t *file, size_t to_read, void *buffer) {

    size_t current_block = (file->of_offset / BLOCK_SIZE) + 1;
    size_t block_offset = file->of_offset % BLOCK_SIZE;
    size_t to_read_block = 0;
    size_t total_read = 0;
    
    if (file->of_offset + to_read <= MAX_BYTES_DIRECT_DATA) {

        while (to_read > 0 && current_block <= MAX_DIRECT_BLOCKS) {        

            void *block = data_block_get((int) current_block);

            if (block == NULL) {
                return -1;
            }

            if (to_read + block_offset > BLOCK_SIZE ) { 
                to_read_block = BLOCK_SIZE - block_offset;               
                to_read -= to_read_block;

            } else {
                to_read_block = to_read;
                to_read = 0;
            }

            memcpy(buffer + total_read, block + block_offset, to_read_block);

            file->of_offset += to_read_block;
            total_read += to_read_block;

            current_block = (file->of_offset / BLOCK_SIZE) + 1;
            block_offset = file->of_offset % BLOCK_SIZE;

        }
    }

    return (ssize_t) total_read;
}

/* Reads from the indirect region a certain amount of bytes to a buffer
 * Inputs:
 *   - pointer to the file entry
 *   - n bytes to read
 *   - buffer
 * Returns: total of read bytes if sucessful, -1 otherwise
 */
ssize_t tfs_read_indirect_region(open_file_entry_t *file, size_t to_read, void *buffer) {

    size_t to_read_block = 0;
    size_t total_read = 0;
    size_t current_block = (file->of_offset / BLOCK_SIZE) + 2;
    size_t block_offset = file->of_offset % BLOCK_SIZE;

    while (to_read > 0) {      

        void *block = data_block_get((int) current_block);
        if (block == NULL) {
            return -1;
        }

        if (to_read + block_offset > BLOCK_SIZE ) {
            to_read_block = BLOCK_SIZE - block_offset;              
            to_read -= to_read_block;

        } else {
            to_read_block = to_read;
            to_read = 0;
        }

        memcpy(buffer + total_read, block + block_offset, to_read_block);

        file->of_offset += to_read_block;
        total_read += to_read_block;

        current_block = (file->of_offset / BLOCK_SIZE) + 2;
        block_offset = file->of_offset % BLOCK_SIZE;
    }

    file->of_offset = file->of_offset;

    return (ssize_t)total_read;
}

/* Locks an inode mutex or a rwlock, specified by the flag lock_state
 * Inputs:
 *   - inode
 *   - lock_state - READ, WRITE, MUTEX
 * Returns: 0 if sucessful, -1 otherwise
 */
int inode_lock(inode_t *inode, lock_state_t lock_state) {

    // READ
    if (lock_state == READ) {
        if (pthread_rwlock_rdlock(&inode->inode_rwlock) != 0) {
            printf("[ inode_lock ] Error locking memory region\n");
            return -1;
        }
    }
    // WRITE
    else if (lock_state == WRITE) {
        if (pthread_rwlock_wrlock(&inode->inode_rwlock) != 0) {
            printf("[ inode_lock ] Error locking memory region\n");
            return -1;
        }
    }
    // MUTEX
    else if (lock_state == MUTEX){
        if (pthread_mutex_lock(&inode->inode_mutex) != 0) {
            printf("[ inode_lock ] Error locking memory region\n");
            return -1;
        }
    }
    else {
        printf("[ inode_lock ] Unrecognized lock state\n");
        return -1;
    }
    return 0;
}

/* Unlocks an inode mutex or a rwlock, specified by the flag lock_state
 * Inputs:
 *   - inode
 *   - lock_state - READ, WRITE, MUTEX
 * Returns: 0 if sucessful, -1 otherwise
 */
int inode_unlock(inode_t *inode, lock_state_t lock_state) {

    // RWLOCK
    if (lock_state == READ || lock_state == WRITE) {
        if (pthread_rwlock_unlock(&inode->inode_rwlock) != 0) {
            printf("[ inode_unlock ] Error unlocking memory region\n");
            return -1;
        }
    }
    // MUTEX
    else if (lock_state == MUTEX){
        if (pthread_mutex_unlock(&inode->inode_mutex) != 0) {
            printf("[ inode_unlock ] Error unlocking memory region\n");
            return -1;
        }
    }
    else {
        printf("[ inode_unlock ] Unrecognized lock state\n");
        return -1;
    }
    return 0;
}

/* Locks an open_file_entry mutex or a rwlock, specified by the flag lock_state
 * Inputs:
 *   - open_file_entry
 *   - lock_state - READ, WRITE, MUTEX
 * Returns: 0 if sucessful, -1 otherwise
 */
int open_file_lock(open_file_entry_t * open_file_entry, lock_state_t lock_state) {

    // READ
    if (lock_state == READ) {
        if (pthread_rwlock_rdlock(&open_file_entry->open_file_rwlock) != 0) {
            printf("[ open_file_lock ] Error locking memory region\n");
            return -1;
        }
    }
    // WRITE
    else if (lock_state == WRITE) {
        if (pthread_rwlock_wrlock(&open_file_entry->open_file_rwlock) != 0) {
            printf("[ open_file_lock ] Error locking memory region\n");
            return -1; 
        }
    }
    // MUTEX
    else if(lock_state == MUTEX) {
        if (pthread_mutex_lock(&open_file_entry->open_file_mutex) != 0) {
            printf("[ open_file_lock ] Error locking memory region\n");
            return -1;
        }
    }
    else {
        printf("[ open_file_lock ] Unrecognized lock state\n");
        return -1;
    }
    return 0;
}

/* Unlocks an open_file_entry mutex or a rwlock, specified by the flag lock_state
 * Inputs:
 *   - open_file_entry
 *   - lock_state - READ, WRITE, MUTEX
 * Returns: 0 if sucessful, -1 otherwise
 */
int open_file_unlock(open_file_entry_t *open_file_entry, lock_state_t lock_state) {

    // RWLOCK
    if (lock_state == READ || lock_state == WRITE) {
        if (pthread_rwlock_unlock(&open_file_entry->open_file_rwlock) !=0) {
            printf("[ open_file_unlock ] Error unlocking memory region\n");
            return -1;
        }
    }
    // MUTEX
    else if (lock_state == MUTEX) {
        if (pthread_mutex_unlock(&open_file_entry->open_file_mutex) != 0) {
            printf("[ open_file_unlock ] Error unlocking memory region\n");
            return -1;
        }
    }
    else {
        printf("[ open_file_unlock ] Unrecognized lock state\n");
        return -1;
    }
    return 0;
}

int inode_allocation_map_lock(lock_state_t lock_state) {

    // RWLOCK
    if (lock_state == READ) {
        if (pthread_rwlock_rdlock(&inode_table_s.inode_table_rwlock) != 0) {
            printf("[ inode_allocation_map_lock ] Error unlocking memory region\n");
            return -1;
        }
    }
    else if (lock_state == WRITE) {
        if (pthread_rwlock_wrlock(&inode_table_s.inode_table_rwlock) != 0) {
            printf("[ inode_allocation_map_lock ] Error unlocking memory region\n");
            return -1;
        }
    }
    // MUTEX
    else if (lock_state == MUTEX){
        if (pthread_mutex_lock(&inode_table_s.inode_table_mutex) != 0) {
            printf("[ inode_allocation_map_lock ] Error unlocking memory region\n");
            return -1;
        }
    }
    else {
        printf("[ inode_allocation_map_lock ] Unrecognized lock state\n");
        return -1;
    }
    return 0;
}

int inode_allocation_map_unlock(lock_state_t lock_state) {

    // RWLOCK
    if (lock_state == READ || lock_state == WRITE) {
        if (pthread_rwlock_unlock(&inode_table_s.inode_table_rwlock) != 0) {
            printf("[ inode_allocation_map_unlock ] Error unlocking memory region\n");
            return -1;
        }
    }
    // MUTEX
    else if (lock_state == MUTEX) {
        if (pthread_mutex_unlock(&inode_table_s.inode_table_mutex) != 0) {
            printf("[ inode_allocation_map_unlock ] Error unlocking memory region\n");
            return -1;
        }
    }
    else {
        printf("[ inode_allocation_map_unlock ] Unrecognized lock state\n");
        return -1;
    }
    return 0;
}


int file_allocation_map_lock(lock_state_t lock_state) {

    // RWLOCK
    if (lock_state == READ) {
        if (pthread_rwlock_rdlock(&fs_state_s.fs_state_rwlock) != 0) {
            printf("[ file_allocation_map_lock ] Error locking memory region\n");
            return -1;
        }
    }
    else if (lock_state == WRITE) {
        if (pthread_rwlock_wrlock(&fs_state_s.fs_state_rwlock) != 0) {
            printf("[ file_allocation_map_lock ] Error locking memory region\n");
            return -1;
        }
    }
    // MUTEX
    else if (lock_state == MUTEX){
        if (pthread_mutex_lock(&fs_state_s.fs_state_mutex) != 0) {
            printf("[ file_allocation_map_lock ] Error locking memory region\n");
            return -1;
        }
    }
    else {
        printf("[ inode_allocation_map ] Unrecognized lock state\n");
        return -1;
    }
    return 0;
}

int file_allocation_map_unlock(lock_state_t lock_state) {

    // RWLOCK
    if (lock_state == READ || lock_state == WRITE) {
        if (pthread_rwlock_unlock(&fs_state_s.fs_state_rwlock) != 0) {
            printf("[ file_allocation_map_unlock ] Error unlocking memory region\n");
            return -1;
        }
    }
    // MUTEX
    else if (lock_state == MUTEX) {
        if (pthread_mutex_unlock(&fs_state_s.fs_state_mutex) != 0) {
            printf("[ file_allocation_map_unlock ] Error unlocking memory region\n");
            return -1;
        }
    }
    else {
        printf("[ file_allocation_map_unlock ] Unrecognized lock state\n");
        return -1;
    }
    return 0;
}
