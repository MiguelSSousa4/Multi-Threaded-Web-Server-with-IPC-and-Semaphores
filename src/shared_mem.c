#include "shared_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>        
#include <fcntl.h>           

connection_queue_t *queue = NULL;

void init_shared_queue(int max_queue_size)
{

    size_t queue_data_size = sizeof(int) * max_queue_size;
    size_t total_size = sizeof(connection_queue_t) + queue_data_size;

    void *mem_block = mmap(NULL, total_size, 
                           PROT_READ | PROT_WRITE, 
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (mem_block == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    queue = (connection_queue_t *)mem_block;

    queue->connections = (int *)(queue + 1);

    queue->head = 0;
    queue->tail = 0;
    queue->max_size = max_queue_size;

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);

    if (pthread_mutex_init(&queue->mutex, &mutex_attr) != 0) {
        perror("mutex init");
        exit(1);
    }
    pthread_mutexattr_destroy(&mutex_attr);

    if (sem_init(&queue->empty_slots, 1, max_queue_size) != 0 ||
        sem_init(&queue->filled_slots, 1, 0) != 0) {
        perror("sem init");
        exit(1);
    }
}

int enqueue(int client_socket) {

    if (sem_trywait(&queue->empty_slots) != 0) {
        if (errno == EAGAIN) {
            return -1; 
        }
        perror("sem_trywait");
        return -1; 
    }

    pthread_mutex_lock(&queue->mutex);

    queue->connections[queue->tail] = client_socket;
    queue->tail = (queue->tail + 1) % queue->max_size;

    pthread_mutex_unlock(&queue->mutex);
    sem_post(&queue->filled_slots);
    
    return 0; 
}

int dequeue() {
    sem_wait(&queue->filled_slots);
    pthread_mutex_lock(&queue->mutex);

    int client_socket = queue->connections[queue->head];
    queue->head = (queue->head + 1) % queue->max_size;

    pthread_mutex_unlock(&queue->mutex);
    sem_post(&queue->empty_slots);
    
    return client_socket;
}