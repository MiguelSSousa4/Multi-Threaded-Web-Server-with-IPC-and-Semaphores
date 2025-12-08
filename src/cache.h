#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>

int cache_init(size_t max_size_bytes);
void cache_destroy();

int cache_get(const char *path, char **out_buf, size_t *out_len);

int cache_put(const char *path, const char *buf, size_t len);

#endif
