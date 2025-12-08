#define _XOPEN_SOURCE 700

#include "shared_mem.h"
#include "config.h"
#include "ipc.h"
#include "worker.h"  
#include "stats.h"
#include "thread_pool.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <pthread.h> 
#include <signal.h>  // [Change 1] Include signal header
#include <errno.h>

server_config_t config;

/* Globals used by signal handler for graceful shutdown */
volatile sig_atomic_t running = 1;
int server_socket_fd = -1;
int *g_worker_pipes = NULL;
pid_t *g_worker_pids = NULL;
int g_num_workers = 0;

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
    if (server_socket_fd >= 0) {
        close(server_socket_fd);
        server_socket_fd = -1;
    }

    /* Close master side of worker IPC sockets to notify workers */
    if (g_worker_pipes) {
        for (int i = 0; i < g_num_workers; i++) {
            if (g_worker_pipes[i] >= 0) {
                close(g_worker_pipes[i]);
                g_worker_pipes[i] = -1;
            }
        }
    }
}

// [Change 2] Global flag to control the main loop
volatile sig_atomic_t server_running = 1;

// [Change 2] Signal handler function
void handle_sigint(int sig) {
    (void)sig;
    server_running = 0; // Stop the loop when Ctrl+C is pressed
}

int main()
{
    if (load_config("server.conf", &config) != 0) return 1;

    // [Change 3] Register the signal handler
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    init_shared_stats();

    /* install signal handler for graceful shutdown */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(config.port);

    if (bind(server_socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(server_socket_fd, 128) < 0) {
        perror("listen");
        return 1;
    }

    printf("Master (PID: %d) listening on port %d.\n", getpid(), config.port);

    pthread_t stats_tid;
    pthread_create(&stats_tid, NULL, stats_monitor_thread, NULL);

    g_num_workers = config.num_workers;
    g_worker_pipes = malloc(sizeof(int) * config.num_workers);
    g_worker_pids = malloc(sizeof(pid_t) * config.num_workers);
    if (!g_worker_pipes || !g_worker_pids) {
        perror("malloc");
        return 1;
    }

    for (int i = 0; i < config.num_workers; i++)
    {
        int sv[2]; 
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
            perror("socketpair");
            exit(1);
        }

        pid_t pid = fork();
        if (pid == 0) {
            /* child (worker) */
            close(server_socket_fd); 
            close(sv[0]); 
            
            // [Change 3] Workers ignore Ctrl+C so they don't die instantly.
            // They will wait for the pipe to close (EOF) to exit cleanly.
            signal(SIGINT, SIG_IGN); 

            start_worker_process(sv[1]); 
            exit(0);
        }
        
        /* parent (master) */
        close(sv[1]); 
        g_worker_pipes[i] = sv[0]; 
        g_worker_pids[i] = pid;
    }

    int current_worker = 0;
    while (running) {
    
    // [Change 4] Loop checks the flag instead of '1'
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_socket_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!running) break;
            continue;
        }
        int client_fd = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        
        // If accept failed because of Ctrl+C (EINTR), loop back and check server_running
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            continue;
        }

        if (send_fd(g_worker_pipes[current_worker], client_fd) < 0) {
            perror("send_fd");
            close(client_fd);
        } else {
            close(client_fd);
            current_worker = (current_worker + 1) % config.num_workers;
        }
    }

    /* master shutting down: close any remaining worker IPC sockets (handler may have closed them already) */
    if (g_worker_pipes) {
        for (int i = 0; i < g_num_workers; i++) {
            if (g_worker_pipes[i] >= 0) {
                close(g_worker_pipes[i]);
                g_worker_pipes[i] = -1;
            }
        }
    }

    /* Wait for child workers to exit */
    for (int i = 0; i < g_num_workers; i++) {
        if (g_worker_pids[i] > 0) {
            waitpid(g_worker_pids[i], NULL, 0);
        }
    }

    free(g_worker_pipes);
    free(g_worker_pids);

    // [Change 5] Cleanup Phase (Only reached after Ctrl+C)
    printf("\nShutting down server...\n");

    // Close pipes to tell workers to exit
    for (int i = 0; i < config.num_workers; i++) {
        close(worker_pipes[i]);
    }

    // Wait for all workers to finish their cleanup
    while (wait(NULL) > 0);

    // Free master memory
    free(worker_pipes);
    close(server_socket);

    printf("Server stopped cleanly.\n");
    return 0;
}