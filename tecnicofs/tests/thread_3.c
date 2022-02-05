#include "operations.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/*
 * This test uses multiple threads to read on the same file using the same file handler.
 * The objective is to read only the amount requested, which could only be possible when using
 * thread-safe applications/ programms.
 * The number of threads chosen is 2, but any other number could be chosen.
 * READ and WRITE are such huge numbers because for small "counts" there are no differences 
 * between thread-safe and not thread-safe applications/ programms. 
 */

#define READ 100000
#define WRITE 100000

static int counter;
static pthread_mutex_t mutex;

void *fn(void *args) {

    char buffer[READ];

    int fh = *((int *)args);

    ssize_t total_read = tfs_read(fh, buffer, READ);

    assert(total_read != -1);

    pthread_mutex_lock(&mutex);

    counter += (int) total_read;
    
    pthread_mutex_unlock(&mutex);

    return (void *)NULL;

}



int main() {

    char *path = "/f5";

    pthread_mutex_init(&mutex, NULL);

    counter = 0;

    char buffer[WRITE];

    memset(buffer, 'V', sizeof(buffer));

    assert(tfs_init() != -1);

    int fh = tfs_open(path, TFS_O_CREAT);
    assert(fh != -1);

    assert(tfs_write(fh, buffer, WRITE));

    assert(tfs_close(fh) != -1);

    fh = tfs_open(path, 0);
    assert(fh != -1);

    pthread_t tid1;
    pthread_t tid2;

    pthread_create(&tid1, NULL, fn, (void *)&fh);
    pthread_create(&tid2, NULL, fn, (void *)&fh);
    
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    assert(counter == READ);

    pthread_mutex_destroy(&mutex);

    assert(tfs_close(fh) != -1);

    printf("Successfull test\n");

    return 0;

}