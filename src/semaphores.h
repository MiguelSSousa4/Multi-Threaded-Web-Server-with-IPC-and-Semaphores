
#ifndef SEMAPHORES_H
#define SEMAPHORES_H
#include <pthread.h>
#include <semaphore.h>

extern sem_t empty_slots;
extern sem_t filled_slots;
extern pthread_mutex_t mutex;

void init_semaphores(int max_queue_size);

#endif
