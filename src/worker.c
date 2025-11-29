#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include "http.h"
#include "config.h"
#include "shared_mem.h"
#include "semaphores.h"

extern server_config_t config;

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

        sem_wait(&filled_slots);

        pthread_mutex_lock(&mutex);
        int client_socket = queue->connections[queue->head];
        queue->head = (queue->head + 1) % queue->max_size;
        pthread_mutex_unlock(&mutex);

        sem_post(&empty_slots);

        handle_client(client_socket);
    }
    return NULL;
}
