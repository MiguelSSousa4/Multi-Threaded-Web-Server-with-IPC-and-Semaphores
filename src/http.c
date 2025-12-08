#include <stdio.h>      
#include <string.h>     
#include <sys/socket.h> 
#include <time.h>
#include "http.h"

int parse_http_request(const char *buffer, http_request_t *req)
{
    char *line_end = strstr(buffer, "\r\n");

    if (!line_end)
        return -1;

    char first_line[1024];
    size_t len = line_end - buffer;
    strncpy(first_line, buffer, len);
    first_line[len] = '\0';

    if (sscanf(first_line, "%s %s %s", req->method, req->path, req->version) != 3)
    {
        return -1;
    }

    return 0;
}

void send_http_response(int fd, int status, const char *status_msg, const char *content_type, const char *body, size_t body_len)
{
    // 1. Get current time in GMT (HTTP requires GMT dates)
    time_t now = time(NULL);
    struct tm tm_data;
    gmtime_r(&now, &tm_data); // Thread-safe version of gmtime

    // 2. Format the date string (e.g., "Mon, 27 Jul 2009 12:28:53 GMT")
    char date_str[128];
    strftime(date_str, sizeof(date_str), "%a, %d %b %Y %H:%M:%S GMT", &tm_data);

    char header[2048];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 %d %s\r\n"
                              "Date: %s\r\n"                 // [ADD THIS]
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Server: ConcurrentHTTP/1.0\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              status, status_msg, 
                              date_str,                      // [ADD THIS argument]
                              content_type, body_len);

    send(fd, header, header_len, 0);

    if (body && body_len > 0)
    {
        send(fd, body, body_len, 0);
    }
}