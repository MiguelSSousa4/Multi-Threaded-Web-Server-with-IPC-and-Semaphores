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

    pthread_t *threads = malloc(sizeof(pthread_t) * config.threads_per_worker);
    for (int i = 0; i < config.threads_per_worker; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    while (1)
    {
        int client_fd = recv_fd(ipc_socket);
        if (client_fd < 0) break; 

        if (enqueue(client_fd) != 0) {
            fprintf(stderr, "[Worker %d] Queue full! Rejecting client.\n", getpid());
            
            const char *error_body = "<h1>503 Service Unavailable</h1>Server too busy.\n";
            send_http_response(client_fd, 503, "Service Unavailable", 
                               "text/html", error_body, strlen(error_body));

            close(client_fd);
        }
    }
    
    free(threads);
}
