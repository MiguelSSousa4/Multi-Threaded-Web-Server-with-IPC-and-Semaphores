#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <bits/pthreadtypes.h>

/* Simple LRU cache with hash table + doubly-linked list. Uses a single
   pthread_rwlock_t to allow concurrent readers and exclusive writers. */

typedef struct cache_node {
    char *path;
    char *data;
    size_t len;
    struct cache_node *prev, *next;
    struct cache_node *hnext; /* hash chain */
} cache_node_t;

static cache_node_t **htable = NULL;
static size_t hsize = 0;
static cache_node_t *head = NULL; /* most recently used */
static cache_node_t *tail = NULL; /* least recently used */
static size_t current_size = 0;
static size_t max_size = 0;
static pthread_rwlock_t cache_lock = PTHREAD_RWLOCK_INITIALIZER;

/* simple djb2 hash */
static unsigned long hash_str(const char *s)
{
    unsigned long h = 5381;
    int c;
    while ((c = *s++)) h = ((h << 5) + h) + (unsigned long)c;
    return h;
}

static int ensure_table(size_t size)
{
    if (htable) return 0;
    hsize = size;
    htable = calloc(hsize, sizeof(cache_node_t *));
    return htable ? 0 : -1;
}

int cache_init(size_t max_size_bytes)
{
    max_size = max_size_bytes;
    current_size = 0;
    head = tail = NULL;
    if (pthread_rwlock_init(&cache_lock, NULL) != 0) return -1;
    return ensure_table(4096);
}

void cache_destroy()
{
    if (!htable) return;
    pthread_rwlock_wrlock(&cache_lock);
    for (size_t i = 0; i < hsize; i++) {
        cache_node_t *n = htable[i];
        while (n) {
            cache_node_t *next = n->hnext;
            free(n->path);
            free(n->data);
            free(n);
            n = next;
        }
        htable[i] = NULL;
    }
    free(htable);
    htable = NULL;
    head = tail = NULL;
    current_size = 0;
    pthread_rwlock_unlock(&cache_lock);
    pthread_rwlock_destroy(&cache_lock);
}

/* remove node from linked list */
static void remove_from_list(cache_node_t *n)
{
    if (!n) return;
    if (n->prev) n->prev->next = n->next; else head = n->next;
    if (n->next) n->next->prev = n->prev; else tail = n->prev;
    n->prev = n->next = NULL;
}

static void insert_at_head(cache_node_t *n)
{
    n->prev = NULL;
    n->next = head;
    if (head) head->prev = n;
    head = n;
    if (!tail) tail = n;
}

/* evict least recently used until current_size <= max_size */
static void evict_if_needed()
{
    while (current_size > max_size && tail) {
        cache_node_t *n = tail;
        /* remove from hash */
        unsigned long h = hash_str(n->path) % hsize;
        cache_node_t *prev = NULL;
        cache_node_t *iter = htable[h];
        while (iter) {
            if (iter == n) {
                if (prev) prev->hnext = iter->hnext; else htable[h] = iter->hnext;
                break;
            }
            prev = iter; iter = iter->hnext;
        }
        remove_from_list(n);
        current_size -= n->len;
        free(n->path);
        free(n->data);
        free(n);
    }
}

int cache_get(const char *path, char **out_buf, size_t *out_len)
{
    if (!htable) return -1;
    unsigned long h = hash_str(path) % hsize;

    /* First try read lock to locate */
    if (pthread_rwlock_rdlock(&cache_lock) != 0) return -1;
    cache_node_t *n = htable[h];
    while (n) {
        if (strcmp(n->path, path) == 0) break;
        n = n->hnext;
    }
    if (!n) {
        pthread_rwlock_unlock(&cache_lock);
        return -1;
    }
    /* found; we need to move it to head -> upgrade to write lock */
    pthread_rwlock_unlock(&cache_lock);

    if (pthread_rwlock_wrlock(&cache_lock) != 0) return -1;
    /* re-find to be safe */
    cache_node_t *n2 = htable[h];
    while (n2) {
        if (strcmp(n2->path, path) == 0) break;
        n2 = n2->hnext;
    }
    if (!n2) {
        pthread_rwlock_unlock(&cache_lock);
        return -1;
    }
    /* move to head */
    remove_from_list(n2);
    insert_at_head(n2);
    /* allocate a copy for caller */
    char *buf = malloc(n2->len);
    if (!buf) {
        pthread_rwlock_unlock(&cache_lock);
        return -1;
    }
    memcpy(buf, n2->data, n2->len);
    *out_buf = buf;
    *out_len = n2->len;
    pthread_rwlock_unlock(&cache_lock);
    return 0;
}

int cache_put(const char *path, const char *buf, size_t len)
{
    if (!htable) return -1;
    if (len == 0 || !buf) return -1;
    if (len > (1 * 1024 * 1024)) return -1; /* only cache files < 1MB */

    if (pthread_rwlock_wrlock(&cache_lock) != 0) return -1;
    unsigned long h = hash_str(path) % hsize;

    /* see if exists */
    cache_node_t *n = htable[h];
    while (n) {
        if (strcmp(n->path, path) == 0) break;
        n = n->hnext;
    }
    if (n) {
        /* replace data */
        current_size -= n->len;
        free(n->data);
        n->data = malloc(len);
        if (!n->data) {
            pthread_rwlock_unlock(&cache_lock);
            return -1;
        }
        memcpy(n->data, buf, len);
        n->len = len;
        current_size += len;
        remove_from_list(n);
        insert_at_head(n);
        evict_if_needed();
        pthread_rwlock_unlock(&cache_lock);
        return 0;
    }

    /* create new node */
    cache_node_t *node = malloc(sizeof(cache_node_t));
    if (!node) { pthread_rwlock_unlock(&cache_lock); return -1; }
    node->path = strdup(path);
    node->data = malloc(len);
    if (!node->path || !node->data) {
        free(node->path); free(node->data); free(node);
        pthread_rwlock_unlock(&cache_lock);
        return -1;
    }
    memcpy(node->data, buf, len);
    node->len = len;
    node->prev = node->next = NULL;
    /* insert into hash */
    node->hnext = htable[h];
    htable[h] = node;
    /* insert at head */
    insert_at_head(node);
    current_size += len;
    evict_if_needed();
    pthread_rwlock_unlock(&cache_lock);
    return 0;
}
