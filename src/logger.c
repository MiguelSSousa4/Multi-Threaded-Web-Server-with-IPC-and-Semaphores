#include "logger.h"
#include "config.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

extern server_config_t config;

static char log_buffer[LOG_BUFFER_SIZE];
static size_t buffer_offset = 0;

static volatile int logger_shutting_down = 0;

void check_and_rotate_log()
{
    struct stat st;
    if (stat(config.log_file, &st) == 0)
    {
        if (st.st_size >= MAX_LOG_FILE_SIZE)
        {
            char old_log_name[512]; 
            
            snprintf(old_log_name, sizeof(old_log_name), "%s.old", config.log_file);
            rename(config.log_file, old_log_name);
        }
    }
}

void flush_buffer_to_disk_internal()
{
    if (buffer_offset == 0) return; 

    check_and_rotate_log();

    FILE *fp = fopen(config.log_file, "a");
    if (fp)
    {
        fwrite(log_buffer, 1, buffer_offset, fp);
        fclose(fp);
    }

    buffer_offset = 0;
}

void flush_logger(sem_t *log_sem)
{
    sem_wait(log_sem);
    flush_buffer_to_disk_internal();
    sem_post(log_sem);
}

void log_request(sem_t *log_sem, const char *client_ip, const char *method,
                 const char *path, int status, size_t bytes)
{
    time_t now = time(NULL);
    struct tm tm_info;
 
    localtime_r(&now, &tm_info);
    
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%d/%b/%Y:%H:%M:%S %z", &tm_info);

    char entry[512];
    int len = snprintf(entry, sizeof(entry), "%s - - [%s] \"%s %s HTTP/1.1\" %d %zu\n",
                       client_ip, timestamp, method, path, status, bytes);

    if (len < 0) return;

    sem_wait(log_sem);

    if (buffer_offset + len >= LOG_BUFFER_SIZE)
    {

        flush_buffer_to_disk_internal();
    }

    memcpy(log_buffer + buffer_offset, entry, len);
    buffer_offset += len;

    sem_post(log_sem);

}

void flush_buffer_to_disk(sem_t *log_sem)
{
    flush_logger(log_sem);
}

void *logger_flush_thread(void *arg)
{
    sem_t *log_sem = (sem_t *)arg;

    while (!__atomic_load_n(&logger_shutting_down, __ATOMIC_SEQ_CST))
    {

        for (int i = 0; i < 5; i++) {
             if (__atomic_load_n(&logger_shutting_down, __ATOMIC_SEQ_CST)) break;
             sleep(1);
        }

        flush_logger(log_sem);
    }

    flush_logger(log_sem);
    return NULL;
}

void logger_request_shutdown()
{
    __atomic_store_n(&logger_shutting_down, 1, __ATOMIC_SEQ_CST);
}