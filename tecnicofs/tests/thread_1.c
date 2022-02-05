#include "operations.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/*
 * This test uses multiple threads to create multiple file handlers for the same file (macro PATH)
 * A number of (N_THREADS = 8) will be used for CPU architecture reasons (theoretical reasons), but 
 * any other number could be chosen
 * The key objetive is to check if each created file handler is different from the others -> check()
 * function does this job
 * A mutex is used to read and write two global variables created to make the test easier to evaluate
 * This mutex DOESN't affect the tfs itself since when tfs_open() is called, any mutex is locked
 */

#define N_THREADS 8

#define PATH ("/f1")


int fhandlers[N_THREADS];

static pthread_mutex_t mutex;

static int count;

int check_array() {
    
    int i = 0;
    int j = 1;

    while(i < N_THREADS) {
        while (j < N_THREADS) {
            if (fhandlers[i] == fhandlers[j]) return -1;
            j++;
        }
        i++;
        j = i + 1;
    }
    return 0;
}

void *fn() {

    int fhandler = tfs_open(PATH, 0);

    pthread_mutex_lock(&mutex);

    fhandlers[count] = fhandler;
    count++;
    
    pthread_mutex_unlock(&mutex);

    return (void *)NULL;
}


int main() {

    pthread_mutex_init(&mutex, NULL);

    count = 0;

    pthread_t tids[N_THREADS];
    memset(tids, 0, sizeof(tids));

    assert(tfs_init() != -1);   

    int fx = tfs_open(PATH, TFS_O_CREAT);
    assert(fx != -1);
    assert(tfs_close(fx) != -1); 

    for (size_t i = 0; i < N_THREADS; i++) {
        
        assert(pthread_create(&tids[i], NULL, fn, NULL) == 0);
    }  

    for (int i = 0; i < N_THREADS; i++) {
        pthread_join(tids[i], NULL);
    }  

    int check = check_array();
    pthread_mutex_destroy(&mutex);

    assert(check == 0);

    printf("Sucessful test\n");

    return 0;
    
}