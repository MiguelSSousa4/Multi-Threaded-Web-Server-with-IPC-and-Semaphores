
#include "shared_mem.h"
#include "semaphores.h"
#include "config.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

server_config_t config;

extern void *worker_thread(void *);

int main()
{

    if (load_config("server.conf", &config) != 0)
    {
        fprintf(stderr, "Failed to load config\n");
        return 1;
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(config.port);

    if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind");
        return 1;
    }
    listen(server_socket, 128);

    init_shared_queue(config.max_queue_size);
    init_semaphores(config.max_queue_size);

    pthread_t workers[config.num_workers];
    for (int i = 0; i < config.num_workers; i++)
    {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    while (1)
    {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0)
            continue;

        if (sem_trywait(&empty_slots) != 0)
        {
            send(client_socket, "HTTP/1.1 503 Service Unavailable\r\n\r\n", 36, 0);
            close(client_socket);
            continue;
        }

        pthread_mutex_lock(&mutex);
        queue->connections[queue->tail] = client_socket;
        queue->tail = (queue->tail + 1) % queue->max_size;
        pthread_mutex_unlock(&mutex);

        sem_post(&filled_slots);
    }

    close(server_socket);
    return 0;
}
