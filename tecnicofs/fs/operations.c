#include "operations.h"

int tfs_init() {
    state_init();
    /* create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}

int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(ROOT_DIR_INUM, name);
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);

    if (inum >= 0) {

        inode_allocation_map_lock(READ);

        inode_t *inode = inode_get(inum);

        inode_allocation_map_unlock(READ);

        /* The file already exists */
        if (inode == NULL) {
            return -1;
        }
        
        if (inode_lock(inode, MUTEX) != 0) {
            return -1;
        }

        
        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {

            if (inode->i_size > 0) {
                if (data_block_free(inode->i_data_block) == -1) {

                    if (inode_unlock(inode, MUTEX) != 0) {
                        return -1;
                    }
                    return -1;
                }

                inode->i_size = 0;
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }

        if (inode_unlock(inode, MUTEX) != 0) {
            return -1;
        }

    } 
    else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be created*/
        /* Create inode */
        inum = inode_create(T_FILE);

        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        offset = 0;
    } else {
        return -1;
    }

    /* Finally, add entry to the open file table and
     * return the corresponding handle */

    file_allocation_map_lock(MUTEX);

    int fhandle = add_to_open_file_table(inum, offset);

    file_allocation_map_unlock(MUTEX);
    
    return fhandle; 

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}

int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {

    ssize_t direct_bytes = 0;
    ssize_t indirect_bytes = 0;
    size_t direct_size = 0;
    size_t indirect_size = 0;

    if (to_write == 0) {
        printf("[ tfs_write ] %s", NOTHING_TO_WRITE);
        return -1;
    }


    if (file_allocation_map_lock(READ) != 0) return -1;

    open_file_entry_t *file = get_open_file_entry(fhandle);

    if (file_allocation_map_unlock(READ) != 0) return -1;

    if (file == NULL) {
        return -1;
    }


    if (open_file_lock(file, MUTEX) != 0) {
        return -1;
    }

    inode_t *inode = inode_get(file->of_inumber);

    if (inode == NULL) {
        if (open_file_unlock(file, MUTEX) != 0) {
            return -1;
        }
        return -1;
    }   

    if (inode_lock(inode, READ) != 0) {
        if (open_file_unlock(file, MUTEX) != 0) {
            return -1;
        }
        return -1;
    }

    if (inode->i_size + to_write <= MAX_BYTES_DIRECT_DATA) {

        direct_bytes = tfs_write_direct_region(inode, file, buffer, to_write);

        if (inode_unlock(inode, READ) != 0) {
            if (open_file_unlock(file, MUTEX) != 0) {
                return -1;
            }
            return -1;
        }    

        if (open_file_unlock(file, MUTEX) != 0) {
            return -1;
        }

        if (direct_bytes == -1) {
            return -1;
        }

        to_write = (size_t)direct_bytes;
    }

    else if (inode->i_size >= MAX_BYTES_DIRECT_DATA) {

        if (inode->i_block[MAX_DIRECT_BLOCKS] == -1) {
            tfs_handle_indirect_block(inode);
        }
        
        indirect_bytes = tfs_write_indirect_region(inode, file, buffer, to_write);

        if (inode_unlock(inode, READ) != 0) {
            if (open_file_unlock(file, MUTEX) != 0) {
                return -1;
            }
            return -1;
        }    

        if (open_file_unlock(file, MUTEX) != 0) {
            return -1;
        }

        if (indirect_bytes == -1) {
            return -1;
        }

        to_write = (size_t) indirect_bytes;

    }

    else {

        direct_size = MAX_BYTES_DIRECT_DATA - inode->i_size;;
        indirect_size = to_write - direct_size;

        direct_bytes = tfs_write_direct_region(inode, file, buffer, direct_size);
        
        if (inode->i_block[MAX_DIRECT_BLOCKS] == -1) {
            tfs_handle_indirect_block(inode);
        }

        if (direct_bytes == -1) {
            printf("[ tfs_write ] %s", WRITE_ERROR);

            if (inode_unlock(inode, READ) != 0) {
                if (open_file_unlock(file, MUTEX) != 0) {
                    return -1;
                }
                return -1;
            }    

            if (open_file_unlock(file, MUTEX) != 0) {
                return -1;
            }
            return -1;
        }

       indirect_bytes = tfs_write_indirect_region(inode, file, buffer + direct_size, indirect_size);

       if (inode_unlock(inode, READ) != 0) {
            if (open_file_unlock(file, MUTEX) != 0) {
                return -1;
            }
            return -1;
        }    

        if (open_file_unlock(file, MUTEX) != 0) {
            return -1;
        }
    
        if (indirect_bytes == -1) {
            printf("[ tfs_write ] %s", WRITE_ERROR);
            return -1;
        }

        to_write = (size_t)(direct_bytes + indirect_bytes);
    }
    
    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {

    size_t to_read = 0;
    size_t total_read = 0;
    ssize_t direct_read = 0;
    ssize_t indirect_read = 0;

    if (len == 0) {
        printf("[ tfs_read ] %s", NOTHING_TO_READ);
        return -1;
    } 


    if (file_allocation_map_lock(READ) != 0) return -1;

    open_file_entry_t *file = get_open_file_entry(fhandle);

    if (file_allocation_map_unlock(READ) != 0) return -1;

    if (file == NULL) {
        return -1;
    }

    if (open_file_lock(file, MUTEX) != 0) {
        return -1;    
    }

    inode_t *inode = inode_get(file->of_inumber);

    if (inode == NULL) {
        if (open_file_unlock(file, MUTEX) != 0) {
            return -1;    
        }
        return -1;
    }

    if (inode_lock(inode, READ) != 0) {
        if (open_file_unlock(file, MUTEX) != 0) {
            return -1;    
        }
        return -1;    
    }


    to_read = inode->i_size - file->of_offset; 

    if (to_read > len) {
        to_read = len;
    } 


    if (file->of_offset + to_read <= MAX_BYTES_DIRECT_DATA) {

        direct_read = tfs_read_direct_region(file, to_read, buffer);  

        if (inode_unlock(inode, READ) != 0) {
            if (open_file_unlock(file, MUTEX) != 0) {
                return -1;
            }
            return -1;
        }    

        if (open_file_unlock(file, MUTEX) != 0) {
            return -1;
        }

        if (direct_read == -1) {
            printf("[ tfs_read ] %s", READ_ERROR);
            return -1;
        }

        total_read = (size_t) direct_read;

    }

    else if (file->of_offset >= MAX_BYTES_DIRECT_DATA) {

        indirect_read = tfs_read_indirect_region(file, to_read, buffer);

        if (inode_unlock(inode, READ) != 0) {
            if (open_file_unlock(file, MUTEX) != 0) {
                return -1;
            }
            return -1;
        }    

        if (open_file_unlock(file, MUTEX) != 0) {
            return -1;
        }

        if (indirect_read == -1) {
            printf("[ tfs_read ] %s", READ_ERROR);
            return -1;
        } 

        total_read = (size_t) indirect_read;
    }

    else {

        size_t bytes_to_read_in_direct_region = 0;

        if (to_read + file->of_offset > MAX_BYTES_DIRECT_DATA) {
            bytes_to_read_in_direct_region = MAX_BYTES_DIRECT_DATA - file->of_offset;
        }

        to_read -= bytes_to_read_in_direct_region;

        direct_read = tfs_read_direct_region(file, bytes_to_read_in_direct_region, buffer);

        if (direct_read == -1) {

            if (inode_unlock(inode, READ) != 0) {
                if (open_file_unlock(file, MUTEX) != 0) {
                    return -1;
                }
                return -1;
            }    

            if (open_file_unlock(file, MUTEX) != 0) {
                return -1;
            }
            return -1;
        }

        total_read = (size_t) (direct_read);
      
        indirect_read = tfs_read_indirect_region(file, to_read, buffer + total_read);

        if (inode_unlock(inode, READ) != 0) {
            if (open_file_unlock(file, MUTEX) != 0) {
                return -1;
            }
            return -1;
        }    

        if (open_file_unlock(file, MUTEX) != 0) {
            return -1;
        }

        if (indirect_read == -1){
            printf("[ tfs_read ] %s", READ_ERROR);
            return -1;
        }

        total_read += ((size_t)indirect_read);
    }
    return (ssize_t)total_read;
}


int tfs_copy_to_external_fs(char const *source_path, char const *dest_path) {

    char buffer[BUFFER_SIZE];
    int source_file;
    FILE *dest_file;
    ssize_t read_bytes = 0;
    ssize_t buffer_read_bytes = 0;
    size_t to_write_bytes = 0;
    size_t written_bytes = 0;
    ssize_t total_size_to_read = 0;
    size_t to_read = 0;
    int close_status_source = 0;
    int close_status_dest = 0;

    memset(buffer, '\0', sizeof(buffer));

    if (tfs_lookup(source_path) == -1) {
        printf("[ tfs_copy_to_external_fs ] %s", FILE_NOT_FOUND);
        return -1;
    }
    else {
        source_file = tfs_open(source_path, TFS_O_APPEND);
    }    

    if (source_file < 0) {
        printf("[ tfs_copy_to_external_fs ] (Source : %s) %s", source_path, OPEN_ERROR);
		return -1;
    }

    dest_file = fopen(dest_path, "w");

    if (dest_file == NULL) {
        printf("[ tfs_copy_to_external_fs ] (Dest : %s) %s", dest_path, OPEN_ERROR);
		return -1;
    }  

    open_file_entry_t *file = get_open_file_entry(source_file);

    pthread_mutex_lock(&file->open_file_mutex);    

    inode_t *inode = inode_get(file->of_inumber);

    file->of_offset = 0;

    pthread_mutex_unlock(&file->open_file_mutex);    

    pthread_rwlock_rdlock(&inode->inode_rwlock);

    total_size_to_read = (ssize_t) inode->i_size;

    pthread_rwlock_unlock(&inode->inode_rwlock);


    do {

        if (total_size_to_read - read_bytes >= sizeof(buffer)) {
            buffer_read_bytes = tfs_read(source_file, buffer, sizeof(buffer));
        }

        else {
            to_read = (size_t) (total_size_to_read - read_bytes);
            buffer_read_bytes = tfs_read(source_file, buffer, to_read);
        }

        if (buffer_read_bytes == -1) {
            printf("[ tfs_copy_to_external_fs ] %s", READ_ERROR);
		    return -1;
        }

        read_bytes += buffer_read_bytes;

        to_write_bytes = (size_t) buffer_read_bytes;

        written_bytes = fwrite(buffer, sizeof(char), to_write_bytes, dest_file);

        if (written_bytes != buffer_read_bytes) {
            printf("[ tfs_copy_to_external_fs ] %s", WRITE_ERROR);
		    return -1;
        }

        memset(buffer, '\0', sizeof(buffer)); 

    } while (total_size_to_read > read_bytes);

    close_status_source =  tfs_close(source_file);
    close_status_dest = fclose(dest_file);

    if (close_status_dest < 0 || close_status_source < 0) {
        printf("[ tfs_copy_to_external_fs ] %s", CLOSE_ERROR);
		return -1;
    }

    return 0;
}
