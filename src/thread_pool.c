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

/* Access global configuration and shared queue structure */
extern server_config_t config;
extern connection_queue_t *queue;

/*
 * Start Worker Process
 * Purpose: This is the main entry point for a Worker process. It initializes 
 * process-local resources (cache, thread pool, logger thread) and enters 
 * a loop to receive client connections from the Master process.
 *
 * Parameters:
 * - ipc_socket: The UNIX domain socket used to receive File Descriptors 
 * from the Master process.
 */
void start_worker_process(int ipc_socket)
{
    printf("Worker (PID: %d) started\n", getpid());

    /* Initialize time zone information for logging */
    tzset();
    
    /* * Initialize shared queue structures. 
     * Note: In this architecture, this primarily sets up the shared log_mutex 
     * needed for thread-safe logging across processes.
     */
    init_shared_queue(config.max_queue_size);

    /* * Start the Logger Flush Thread
     * This background thread ensures logs are written to disk periodically 
     * even if the buffer isn't full.
     */
    pthread_t flush_tid;
    if (pthread_create(&flush_tid, NULL, logger_flush_thread, (void *)&queue->log_mutex) != 0) {
        perror("Failed to create logger flush thread");
    }

    /* * Initialize Local Request Queue
     * This queue acts as the buffer between the Worker process (Main Thread) 
     * and its pool of worker threads.
     */
    local_queue_t local_q;
    if (local_queue_init(&local_q, config.max_queue_size) != 0) {
        perror("local_queue_init");
    }
    
    /* * Initialize File Cache
     * Sets up the in-memory LRU cache with the size defined in server.conf.
     */
    size_t cache_bytes = (size_t)config.cache_size_mb * 1024 * 1024;
    if (cache_init(cache_bytes) != 0) {
        perror("cache_init");
    }

    /* * Create Thread Pool
     * Spawns a fixed number of threads (consumer) that will block waiting 
     * for work on the local_q.
     */
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

    /* * Main Loop: Receive and Dispatch
     * 1. Block waiting for a File Descriptor from Master (IPC).
     * 2. Enqueue the FD into the local thread pool queue.
     */
    while (1)
    {
        int client_fd = recv_fd(ipc_socket);
        if (client_fd < 0) {
            /* IPC socket closed or error — begin shutdown sequence */
            break;
        }

        /* * Dispatch to Thread Pool
         * Try to add the client FD to the local queue. If the queue is full,
         * we reject the request immediately with 503 to prevent overload.
         */
        if (local_queue_enqueue(&local_q, client_fd) != 0) {
            fprintf(stderr, "[Worker %d] Queue full! Rejecting client.\n", getpid());
            
            const char *error_body = "<h1>503 Service Unavailable</h1>Server too busy.\n";
            send_http_response(client_fd, 503, "Service Unavailable", 
                               "text/html", error_body, strlen(error_body));

            close(client_fd);
        }
    }

    /* * === Graceful Shutdown Sequence === 
     */

    /* 1. Signal Worker Threads to Stop */
    /* Acquire lock to ensure condition broadcast is not missed by threads */
    pthread_mutex_lock(&local_q.mutex); 
    local_q.shutting_down = 1;
    pthread_cond_broadcast(&local_q.cond);
    pthread_mutex_unlock(&local_q.mutex);

    /* 2. Stop Logger Thread */
    logger_request_shutdown();
    pthread_join(flush_tid, NULL);

    /* 3. Join Worker Threads */
    for (int i = 0; i < created; i++) {
        pthread_join(threads[i], NULL);
    }

    /* 4. Cleanup Resources */
    if (threads) free(threads);
    local_queue_destroy(&local_q);
    cache_destroy();
    
    close(ipc_socket);
}