#ifndef WORKER_H
#define WORKER_H

#include <stddef.h>
#include <time.h>
#include <pthread.h>

long get_time_diff_ms(struct timespec start, struct timespec end);
void get_client_ip(int client_fd, char *ip_buffer, size_t buffer_len);
const char *get_mime_type(const char *path);
void handle_client(int client_socket);

/* forward */
struct local_queue;
void *worker_thread(void *arg);

/* local per-worker queue used by threads inside a worker process */
typedef struct local_queue {
    int *fds;
    int head;
    int tail;
    int max_size;
    int shutting_down;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} local_queue_t;

int local_queue_init(local_queue_t *q, int max_size);
void local_queue_destroy(local_queue_t *q);
int local_queue_enqueue(local_queue_t *q, int client_fd);
int local_queue_dequeue(local_queue_t *q);

#endif