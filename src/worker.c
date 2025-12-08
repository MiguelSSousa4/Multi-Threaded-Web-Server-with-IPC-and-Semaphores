#define _POSIX_C_SOURCE 199309L 

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
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
#include "logger.h"
#include "worker.h"
#include "cache.h"

extern server_config_t config;
extern connection_queue_t *queue;


long get_time_diff_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
}

void get_client_ip(int client_fd, char *ip_buffer, size_t buffer_len) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(client_fd, (struct sockaddr *)&addr, &addr_len) == 0) {
        inet_ntop(AF_INET, &addr.sin_addr, ip_buffer, buffer_len);
    } else {
        strncpy(ip_buffer, "unknown", buffer_len);
    }
}

const char *get_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)
        return "application/octet-stream";
    if (strcmp(ext, ".html") == 0)
        return "text/html";
    if (strcmp(ext, ".css") == 0)
        return "text/css";
    if (strcmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcmp(ext, ".png") == 0)
        return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    return "application/octet-stream";
}

void handle_client(int client_socket)
{
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    sem_wait(&stats->mutex);
    stats->active_connections++;
    sem_post(&stats->mutex);

    char client_ip[INET_ADDRSTRLEN];
    get_client_ip(client_socket, client_ip, sizeof(client_ip));

    char buffer[2048];
    ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

    int status_code = 0;
    long bytes_sent = 0;
    http_request_t req = {0}; 

    if (bytes <= 0)
    {
        close(client_socket);
        sem_wait(&stats->mutex);
        stats->active_connections--;
        sem_post(&stats->mutex);
        return;
    }
    buffer[bytes] = '\0';

    if (parse_http_request(buffer, &req) != 0)
    {
        status_code = 400;
        const char *body = "<h1>400 Bad Request</h1>";
        size_t len = strlen(body);
        send_http_response(client_socket, 400, "Bad Request", "text/html", body, len);
        bytes_sent = len; 
        close(client_socket);
        goto update_stats_and_log;
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
        goto update_stats_and_log;
    }

    if (strstr(req.path, ".."))
    {
        status_code = 403;
        const char *body = "<h1>403 Forbidden</h1>";
        size_t len = strlen(body);
        send_http_response(client_socket, 403, "Forbidden", "text/html", body, len);
        bytes_sent = len;
        close(client_socket);
        goto update_stats_and_log;
    }

    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", config.document_root, req.path);

    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
    {
        strncat(full_path, "/index.html", sizeof(full_path) - strlen(full_path) - 1);
    }

    if (stat(full_path, &st) != 0) {
        status_code = 404;
        const char *body = "<h1>404 Not Found</h1>";
        size_t len = strlen(body);
        send_http_response(client_socket, 404, "Not Found", "text/html", body, len);
        bytes_sent = len;
        close(client_socket);
        goto update_stats_and_log;
    }

    long fsize = st.st_size;
    char *content = NULL;
    size_t read_bytes = 0;

    /* Try per-worker cache for small files (<1MB) */
    if (fsize > 0 && fsize < (1 * 1024 * 1024)) {
        if (cache_get(full_path, &content, &read_bytes) == 0) {
            /* served from cache */
        } else {
            FILE *fp = fopen(full_path, "rb");
            if (!fp) {
                status_code = 404;
                const char *body = "<h1>404 Not Found</h1>";
                size_t len = strlen(body);
                send_http_response(client_socket, 404, "Not Found", "text/html", body, len);
                bytes_sent = len;
                close(client_socket);
                goto update_stats_and_log;
            }
            char *buf = malloc(fsize);
            if (!buf) {
                fclose(fp);
                status_code = 500;
                const char *body = "<h1>500 Internal Server Error</h1>";
                size_t len = strlen(body);
                send_http_response(client_socket, 500, "Internal Server Error", "text/html", body, len);
                bytes_sent = len;
                close(client_socket);
                goto update_stats_and_log;
            }
            size_t rb = fread(buf, 1, fsize, fp);
            fclose(fp);
            if (rb != (size_t)fsize) {
                free(buf);
                status_code = 500;
                const char *body = "<h1>500 Internal Server Error</h1>";
                size_t len = strlen(body);
                send_http_response(client_socket, 500, "Internal Server Error", "text/html", body, len);
                bytes_sent = len;
                close(client_socket);
                goto update_stats_and_log;
            }
            read_bytes = rb;
            content = buf;
            /* Best-effort cache insertion */
            cache_put(full_path, content, read_bytes);
        }
    } else {
        /* Large files: read directly without caching */
        FILE *fp = fopen(full_path, "rb");
        if (!fp) {
            status_code = 404;
            const char *body = "<h1>404 Not Found</h1>";
            size_t len = strlen(body);
            send_http_response(client_socket, 404, "Not Found", "text/html", body, len);
            bytes_sent = len;
            close(client_socket);
            goto update_stats_and_log;
        }
        char *buf = malloc(fsize);
        if (!buf) {
            fclose(fp);
            status_code = 500;
            const char *body = "<h1>500 Internal Server Error</h1>";
            size_t len = strlen(body);
            send_http_response(client_socket, 500, "Internal Server Error", "text/html", body, len);
            bytes_sent = len;
            close(client_socket);
            goto update_stats_and_log;
        }
        size_t rb = fread(buf, 1, fsize, fp);
        fclose(fp);
        if (rb != (size_t)fsize) {
            free(buf);
            status_code = 500;
            const char *body = "<h1>500 Internal Server Error</h1>";
            size_t len = strlen(body);
            send_http_response(client_socket, 500, "Internal Server Error", "text/html", body, len);
            bytes_sent = len;
            close(client_socket);
            goto update_stats_and_log;
        }
        content = buf;
        read_bytes = rb;
    }

    const char *mime = get_mime_type(full_path);
    status_code = 200;
    if (is_head)
    {
        send_http_response(client_socket, 200, "OK", mime, NULL, fsize);
        bytes_sent = 0;
    }
    else
    {
        send_http_response(client_socket, 200, "OK", mime, content, fsize);
        bytes_sent = fsize;
    }

    free(content);
    close(client_socket);

update_stats_and_log:
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

    const char *log_method = (req.method[0] != '\0') ? req.method : "-";
    const char *log_path = (req.path[0] != '\0') ? req.path : "-";
    
    log_request(&queue->log_mutex, client_ip, log_method, log_path, status_code, bytes_sent);
}

/* Local queue implementation for per-worker use */
int local_queue_init(local_queue_t *q, int max_size)
{
    q->fds = malloc(sizeof(int) * max_size);
    if (!q->fds) return -1;
    q->head = 0;
    q->tail = 0;
    q->max_size = max_size;
    q->shutting_down = 0;
    if (pthread_mutex_init(&q->mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&q->cond, NULL) != 0) return -1;
    return 0;
}

void local_queue_destroy(local_queue_t *q)
{
    if (!q) return;
    free(q->fds);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

int local_queue_enqueue(local_queue_t *q, int client_fd)
{
    pthread_mutex_lock(&q->mutex);
    int next = (q->tail + 1) % q->max_size;
    if (next == q->head) {
        pthread_mutex_unlock(&q->mutex);
        return -1; /* full */
    }
    q->fds[q->tail] = client_fd;
    q->tail = next;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

int local_queue_dequeue(local_queue_t *q)
{
    pthread_mutex_lock(&q->mutex);
    while (q->head == q->tail && !q->shutting_down) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    if (q->head == q->tail && q->shutting_down) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    int fd = q->fds[q->head];
    q->head = (q->head + 1) % q->max_size;
    pthread_mutex_unlock(&q->mutex);
    return fd;
}

void *worker_thread(void *arg)
{
    local_queue_t *q = (local_queue_t *)arg;
    while (1)
    {
        int client_socket = local_queue_dequeue(q);
        if (client_socket < 0) {
            break; /* shutdown signaled */
        }

        handle_client(client_socket);
    }
    return NULL;
}

