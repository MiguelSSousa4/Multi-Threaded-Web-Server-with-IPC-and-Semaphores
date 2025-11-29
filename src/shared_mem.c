
#include "shared_mem.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

connection_queue_t *queue = NULL;

void init_shared_queue(int max_queue_size)
{
    queue = mmap(NULL, sizeof(connection_queue_t), PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (queue == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    queue->connections = mmap(NULL, sizeof(int) * max_queue_size,
                              PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (queue->connections == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }

    queue->head = 0;
    queue->tail = 0;
    queue->max_size = max_queue_size;
}
