#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
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
    char buffer[2048];
    ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0)
    {
        close(client_socket);
        return;
    }
    buffer[bytes] = '\0';

    http_request_t req;
    if (parse_http_request(buffer, &req) != 0)
    {
        send_http_response(client_socket, 400, "Bad Request", "text/html", "<h1>400 Bad Request</h1>", 22);
        close(client_socket);
        return;
    }

    int is_head = (strcmp(req.method, "HEAD") == 0);
    if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0)
    {
        send_http_response(client_socket, 405, "Method Not Allowed", "text/html", "<h1>405 Method Not Allowed</h1>", 34);
        close(client_socket);
        return;
    }

    if (strstr(req.path, ".."))
    {
        send_http_response(client_socket, 403, "Forbidden", "text/html", "<h1>403 Forbidden</h1>", 22);
        close(client_socket);
        return;
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
        send_http_response(client_socket, 404, "Not Found", "text/html", "<h1>404 Not Found</h1>", 22);
        close(client_socket);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);

    char *content = malloc(fsize);
    if (!content)
    {
        fclose(fp);
        send_http_response(client_socket, 500, "Internal Server Error", "text/html", "<h1>500 Internal Server Error</h1>", 37);
        close(client_socket);
        return;
    }

    size_t read_bytes = fread(content, 1, fsize, fp);
    fclose(fp);
    if (read_bytes != (size_t)fsize)
    {
        free(content);
        send_http_response(client_socket, 500, "Internal Server Error", "text/html", "<h1>500 Internal Server Error</h1>", 37);
        close(client_socket);
        return;
    }

    const char *mime = get_mime_type(full_path);
    if (is_head)
    {
        send_http_response(client_socket, 200, "OK", mime, NULL, 0);
    }
    else
    {
        send_http_response(client_socket, 200, "OK", mime, content, fsize);
    }

    free(content);
    close(client_socket);
}

void *worker_thread(void *arg)
{
    (void)arg;
    while (1)
    {
        // 1. Dequeue (Blocks until Main Thread pushes a connection)
        int client_socket = dequeue();

        // 2. Handle
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
        // 1. Receive the FD from Master via IPC
        int client_fd = recv_fd(ipc_socket);
        if (client_fd < 0) break; // Master died or error

        // 2. Try to Enqueue
        if (enqueue(client_fd) != 0) {
            // 3. QUEUE FULL: Reject client
            fprintf(stderr, "[Worker %d] Queue full! Rejecting client.\n", getpid());
            
            const char *error_body = "<h1>503 Service Unavailable</h1>Server too busy.\n";
            send_http_response(client_fd, 503, "Service Unavailable", 
                               "text/html", error_body, strlen(error_body));
            
            // Critical: Close the socket immediately so the client isn't left hanging
            close(client_fd);
        }
        // Else: Successfully queued, a thread will pick it up
    }
    
    free(threads);
}