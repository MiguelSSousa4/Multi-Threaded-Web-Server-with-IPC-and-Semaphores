#include "shared_mem.h"
#include "config.h"
#include "ipc.h"
#include "worker.h"  
#include "stats.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <pthread.h> 

server_config_t config;

int main()
{
    if (load_config("server.conf", &config) != 0) return 1;

    init_shared_stats();

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(config.port);

    bind(server_socket, (struct sockaddr *)&address, sizeof(address));
    listen(server_socket, 128);

    printf("Master (PID: %d) listening on port %d.\n", getpid(), config.port);

    pthread_t stats_tid;
    pthread_create(&stats_tid, NULL, stats_monitor_thread, NULL);

    int *worker_pipes = malloc(sizeof(int) * config.num_workers);
    for (int i = 0; i < config.num_workers; i++)
    {
        int sv[2]; 
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
            perror("socketpair");
            exit(1);
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(server_socket); 
            close(sv[0]); 

            start_worker_process(sv[1]); 
            exit(0);
        }
        
        close(sv[1]); 
        worker_pipes[i] = sv[0]; 
    }


    int current_worker = 0;
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        send_fd(worker_pipes[current_worker], client_fd);
        close(client_fd);
        current_worker = (current_worker + 1) % config.num_workers;
    }

    return 0;
}