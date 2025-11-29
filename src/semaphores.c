
#include "semaphores.h"
#include <stdio.h>
#include <stdlib.h>

sem_t empty_slots;
sem_t filled_slots;
pthread_mutex_t mutex;

void init_semaphores(int max_queue_size)
{
    if (sem_init(&empty_slots, 1, max_queue_size) != 0 ||
        sem_init(&filled_slots, 1, 0) != 0 ||
        pthread_mutex_init(&mutex, NULL) != 0)
    {
        perror("Semaphore or mutex init failed");
        exit(1);
    }
}
