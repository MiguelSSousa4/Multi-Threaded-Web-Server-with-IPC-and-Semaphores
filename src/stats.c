#include "shared_mem.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

void *stats_monitor_thread(void *arg) {
    (void)arg;
    while (1) {
        sleep(30); 

        sem_wait(&stats->mutex);
        
        double avg_time = 0.0;
        if (stats->total_requests > 0) {
            avg_time = (double)stats->average_response_time / stats->total_requests;
        }

        printf("\n=== SERVER STATISTICS ===\n");
        printf("Active Connections: %d\n", stats->active_connections);
        printf("Total Requests:     %ld\n", stats->total_requests);
        printf("Bytes Transferred:  %ld\n", stats->bytes_transferred);
        printf("Avg Response Time:  %.2f ms\n", avg_time);
        printf("Status 200 (OK):    %ld\n", stats->status_200);
        printf("Status 404 (NF):    %ld\n", stats->status_404);
        printf("Status 500 (Err):   %ld\n", stats->status_500);
        printf("=========================\n\n");

        sem_post(&stats->mutex);
    }
    return NULL;
}