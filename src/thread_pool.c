#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "config.h"
#include "logger.h"
#include "shared_mem.h"
#include "worker.h"
#include "ipc.h"
#include "http.h"
#include "cache.h"

extern server_config_t config;
extern connection_queue_t *queue;

void start_worker_process(int ipc_socket)
{
    printf("Worker (PID: %d) started\n", getpid());

    tzset();
    
    init_shared_queue(config.max_queue_size);

    pthread_t flush_tid;
    if (pthread_create(&flush_tid, NULL, logger_flush_thread, (void *)&queue->log_mutex) != 0) {
        perror("Failed to create logger flush thread");
    }

    local_queue_t local_q;
    if (local_queue_init(&local_q, config.max_queue_size) != 0) {
        perror("local_queue_init");
    }
    
        size_t cache_bytes = (size_t)config.cache_size_mb * 1024 * 1024;

        if (cache_init(cache_bytes) != 0) {
            perror("cache_init");
        }

    int thread_count = config.threads_per_worker > 0 ? config.threads_per_worker : 0;
    pthread_t *threads = NULL;
    if (thread_count > 0) {
        threads = malloc(sizeof(pthread_t) * thread_count);
        if (!threads) {
            perror("Failed to allocate worker threads array");
            thread_count = 0;
        }
    }

    int created = 0;
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, &local_q) != 0) {
            perror("pthread_create");
            break;
        }
        created++;
    }

    while (1)
    {
        int client_fd = recv_fd(ipc_socket);
        if (client_fd < 0) {

            break;
        }

        if (local_queue_enqueue(&local_q, client_fd) != 0) {
            fprintf(stderr, "[Worker %d] Queue full! Rejecting client.\n", getpid());
            
            const char *error_body = "<h1>503 Service Unavailable</h1>Server too busy.\n";
            send_http_response(client_fd, 503, "Service Unavailable", 
                               "text/html", error_body, strlen(error_body));

            close(client_fd);
        }
    }

    pthread_mutex_lock(&local_q.mutex); 
    local_q.shutting_down = 1;
    pthread_cond_broadcast(&local_q.cond);
    pthread_mutex_unlock(&local_q.mutex);

    logger_request_shutdown();
    pthread_join(flush_tid, NULL);

    for (int i = 0; i < created; i++) {
        pthread_join(threads[i], NULL);
    }

    if (threads) free(threads);
        local_queue_destroy(&local_q);
        cache_destroy();
    close(ipc_socket);
}
