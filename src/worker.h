#ifndef WORKER_H
#define WORKER_H

long get_time_diff_ms(struct timespec start, struct timespec end);
void get_client_ip(int client_fd, char *ip_buffer, size_t buffer_len);
const char *get_mime_type(const char *path);
void handle_client(int client_socket);
void *worker_thread(void *arg);

#endif