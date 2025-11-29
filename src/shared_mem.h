
#ifndef SHARED_MEM_H
#define SHARED_MEM_H

typedef struct
{
    int *connections;
    int head;
    int tail;
    int max_size;
} connection_queue_t;

extern connection_queue_t *queue;

void init_shared_queue(int max_queue_size);

#endif
