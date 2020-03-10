#ifndef QUEUE_H
#define QUEUE_H 1

#include <pthread.h>

#define BUF_SIZE 64
typedef struct {
    char buf[BUF_SIZE];
    int wr_pos, rd_pos;
    int empty;
    int full;
    pthread_mutex_t mutex;
    pthread_cond_t can_prod;
    pthread_cond_t can_cons;
    int num_producers;
} queue;


#define QUEUE_INITIALIZER {\
    .wr_pos = 0,\
    .rd_pos = 0,\
    .empty = 1,\
    .full = 0,\
    .mutex = PTHREAD_MUTEX_INITIALIZER,\
    .can_prod = PTHREAD_COND_INITIALIZER,\
    .can_cons = PTHREAD_COND_INITIALIZER,\
    .num_producers = 0\
}

//Adds a char to queue q in a thread-safe way. Returns 0 on successful write,
//negative on error. This function can sleep; do not call while holding _any_
//mutexes, not even the one in the struct! 
int enqueue_single(queue *q, char c);

//Reads a char from queue q in a thread-safe way. Returns 0 on successful read,
//-1 if no producers. This function can sleep; do not call while holding _any_
//mutexes, not even the one in the struct! 
int dequeue_single(queue *q, char *c);

#endif
