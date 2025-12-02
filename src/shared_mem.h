#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <semaphore.h>
#include <pthread.h>

typedef struct
{
    int *connections;
    int head;
    int tail;
    int max_size;
    
    sem_t empty_slots;
    sem_t filled_slots;
    pthread_mutex_t mutex;
} connection_queue_t;

void init_shared_queue(int max_queue_size);
int enqueue(int client_socket);
int dequeue();

#endif