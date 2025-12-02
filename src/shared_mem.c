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
    // 1. Calculate total size: Struct + (Size of int * max_queue_size)
    size_t queue_data_size = sizeof(int) * max_queue_size;
    size_t total_size = sizeof(connection_queue_t) + queue_data_size;

    // 2. Use mmap to allocate "Anonymous Shared Memory"
    // PROT_READ | PROT_WRITE: We can read/write this memory
    // MAP_SHARED: Changes are visible to other processes mapping this region
    // MAP_ANONYMOUS: Not backed by a file (simpler than shm_open for this use case)
    void *mem_block = mmap(NULL, total_size, 
                           PROT_READ | PROT_WRITE, 
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (mem_block == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    // 3. Map the struct to the start of the block
    queue = (connection_queue_t *)mem_block;

    // 4. Point the 'connections' array to the memory immediately following the struct
    // This keeps everything in one contiguous shared block.
    queue->connections = (int *)(queue + 1);

    queue->head = 0;
    queue->tail = 0;
    queue->max_size = max_queue_size;

    // 5. Initialize Mutex with PTHREAD_PROCESS_SHARED attribute
    // This allows the mutex to be safe even if we move to a multi-process queue reader later.
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);

    if (pthread_mutex_init(&queue->mutex, &mutex_attr) != 0) {
        perror("mutex init");
        exit(1);
    }
    pthread_mutexattr_destroy(&mutex_attr);

    // 6. Initialize Semaphores with pshared=1 (Non-zero means shared between processes)
    if (sem_init(&queue->empty_slots, 1, max_queue_size) != 0 ||
        sem_init(&queue->filled_slots, 1, 0) != 0) {
        perror("sem init");
        exit(1);
    }
}

int enqueue(int client_socket) {
    // Logic remains identical, but now operates on Shared Memory
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