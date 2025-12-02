#define _POSIX_C_SOURCE 199309L 

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h> 
#include "http.h"
#include "config.h"
#include "shared_mem.h"
#include "ipc.h"

extern server_config_t config;
extern connection_queue_t *queue;

extern int enqueue(int client_socket);
extern int dequeue();
extern void send_http_response(int fd, int status, const char *status_msg, 
                               const char *content_type, const char *body, size_t body_len);

long get_time_diff_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
}

const char *get_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    return "application/octet-stream";
}

void handle_client(int client_socket)
{
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    sem_wait(&stats->mutex);
    stats->active_connections++;
    sem_post(&stats->mutex);

    char buffer[2048];
    ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

    int status_code = 0;
    long bytes_sent = 0;

    if (bytes <= 0)
    {
        close(client_socket);

        sem_wait(&stats->mutex);
        stats->active_connections--;
        sem_post(&stats->mutex);
        return;
    }
    buffer[bytes] = '\0';

    http_request_t req;
    if (parse_http_request(buffer, &req) != 0)
    {
        status_code = 400;
        const char *body = "<h1>400 Bad Request</h1>";
        size_t len = strlen(body);
        send_http_response(client_socket, 400, "Bad Request", "text/html", body, len);
        bytes_sent = len; 
        close(client_socket);
        goto update_stats;
    }

    int is_head = (strcmp(req.method, "HEAD") == 0);
    if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0)
    {
        status_code = 405;
        const char *body = "<h1>405 Method Not Allowed</h1>";
        size_t len = strlen(body);
        send_http_response(client_socket, 405, "Method Not Allowed", "text/html", body, len);
        bytes_sent = len;
        close(client_socket);
        goto update_stats;
    }

    if (strstr(req.path, ".."))
    {
        status_code = 403;
        const char *body = "<h1>403 Forbidden</h1>";
        size_t len = strlen(body);
        send_http_response(client_socket, 403, "Forbidden", "text/html", body, len);
        bytes_sent = len;
        close(client_socket);
        goto update_stats;
    }

    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", config.document_root, req.path);

    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
    {
        strncat(full_path, "/index.html", sizeof(full_path) - strlen(full_path) - 1);
    }

    FILE *fp = fopen(full_path, "rb");
    if (!fp)
    {
        status_code = 404;
        const char *body = "<h1>404 Not Found</h1>";
        size_t len = strlen(body);
        send_http_response(client_socket, 404, "Not Found", "text/html", body, len);
        bytes_sent = len;
        close(client_socket);
        goto update_stats;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);

    char *content = malloc(fsize);
    if (!content)
    {
        fclose(fp);
        status_code = 500;
        const char *body = "<h1>500 Internal Server Error</h1>";
        size_t len = strlen(body);
        send_http_response(client_socket, 500, "Internal Server Error", "text/html", body, len);
        bytes_sent = len;
        close(client_socket);
        goto update_stats;
    }

    size_t read_bytes = fread(content, 1, fsize, fp);
    fclose(fp);
    if (read_bytes != (size_t)fsize)
    {
        free(content);
        status_code = 500;
        const char *body = "<h1>500 Internal Server Error</h1>";
        size_t len = strlen(body);
        send_http_response(client_socket, 500, "Internal Server Error", "text/html", body, len);
        bytes_sent = len;
        close(client_socket);
        goto update_stats;
    }

    const char *mime = get_mime_type(full_path);
    status_code = 200;
    if (is_head)
    {
        send_http_response(client_socket, 200, "OK", mime, NULL, 0);
        bytes_sent = 0;
    }
    else
    {
        send_http_response(client_socket, 200, "OK", mime, content, fsize);
        bytes_sent = fsize;
    }

    free(content);
    close(client_socket);

update_stats:
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    long elapsed_ms = get_time_diff_ms(start_time, end_time);

    sem_wait(&stats->mutex);
    stats->active_connections--;
    stats->total_requests++;
    stats->bytes_transferred += bytes_sent;
    stats->average_response_time += elapsed_ms;

    if (status_code == 200) stats->status_200++;
    else if (status_code == 404) stats->status_404++;
    else if (status_code == 500) stats->status_500++;
    
    sem_post(&stats->mutex);
}

void *worker_thread(void *arg)
{
    (void)arg;
    while (1)
    {
        int client_socket = dequeue();
        handle_client(client_socket);
    }
    return NULL;
}

void start_worker_process(int ipc_socket)
{
    printf("Worker (PID: %d) started\n", getpid());
    
    init_shared_queue(config.max_queue_size);
    
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