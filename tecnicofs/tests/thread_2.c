#include "operations.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <time.h>


/*
 * This test uses multiple threads to write on the same file (and with the same fh), targeting only 
 * the first block.
 * The objective is to evaluate the final buffer which should contain 1024 characters, all equals
 * between them, since the tfs_write() and the write itself would be concurrent.
 * A number of (N_THREADS = 8) will be used for CPU architecture reasons (theoretical reasons), but 
 * any other number could be chosen
 * WRITE = 20 represents he total number of bytes written, but only BLOCK_SIZE = 1024 will actual be 
 * written when the test is over 
 */

#define WRITE 20480
#define N_THREADS 20

static char buffer[WRITE];

typedef struct {
    int fh;
    int offset;
} myargs_t;

static char read_info[BLOCK_SIZE];

void *fn(void *arg) {
    
    myargs_t args = *((myargs_t *)arg);

    ssize_t total_written = tfs_write(args.fh, buffer + args.offset, BLOCK_SIZE);

    assert(total_written != -1);

    return (void *)NULL;
}

int check() {
    
    char control = read_info[0];

    for (int i = 1; i < BLOCK_SIZE; i++) {
        if (read_info[i] != control) {
            return -1;
        }
    }
    return 0;
}



int main() {

    char *path = "/f5";

    int fhs[N_THREADS];

    pthread_t tids[N_THREADS];

    myargs_t *s[N_THREADS]; 

    memset(buffer, '\0', sizeof(buffer));

    memset(buffer, 'O', sizeof(buffer) / 3);    
    memset(buffer + (BLOCK_SIZE), 'L', sizeof(buffer) / 3);
    memset(buffer + (2 * BLOCK_SIZE), 'A', sizeof(buffer) / 3);

    memset(read_info, '\0', sizeof(read_info));

    assert(tfs_init() != -1);

    for (int i = 0; i < N_THREADS; i++) {
       fhs[i] = tfs_open(path, TFS_O_CREAT); 
    } 

    for (int i = 0; i < N_THREADS; i++)  {
        s[i] = (myargs_t *)malloc(sizeof(myargs_t));
        s[i]->fh = fhs[i];
        s[i]->offset = BLOCK_SIZE * i;
        assert(pthread_create(&tids[i], NULL, fn, (void *)s[i]) == 0);

    }

    for (int i = 0; i < N_THREADS; i++) {
        pthread_join(tids[i], NULL);
    }

    for (int i = 0; i < N_THREADS; i++) {
        assert(tfs_close(fhs[i]) != -1);
        free(s[i]);
    }

    memset(buffer, '\0', sizeof(buffer));


    int fh = tfs_open(path, 0);

    ssize_t read = tfs_read(fh, read_info, BLOCK_SIZE);

    assert(check() == 0);
    assert(read = BLOCK_SIZE);

    printf("Successfull test\n");

    return 0;

}