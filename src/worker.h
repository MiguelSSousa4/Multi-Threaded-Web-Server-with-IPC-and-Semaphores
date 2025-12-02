#ifndef WORKER_H
#define WORKER_H

const char *get_mime_type(const char *path);
void handle_client(int client_socket);
void *worker_thread(void *arg);
void start_worker_process(int ipc_socket);

#endif