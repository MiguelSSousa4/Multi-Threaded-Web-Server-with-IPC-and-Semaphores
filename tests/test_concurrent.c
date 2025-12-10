#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>    /* intptr_t */

#include "../src/worker.h"
#include "../src/cache.h"
#include "../src/config.h"

server_config_t config;

static void pass(const char *name) { printf("[PASS] %s\n", name); fflush(stdout); }
static void fail(const char *name) { fprintf(stderr, "[FAIL] %s\n", name); fflush(stderr); exit(1); }

/* -------------------------
   Test 1: Basic queue ops
   ------------------------- */
void test_queue_basic(void)
{
    local_queue_t q;
    int max_sz = 8;
    int rc = local_queue_init(&q, max_sz + 1);
    if (rc != 0) fail("test_queue_basic - init");

    for (int i = 0; i < max_sz; ++i) {
        rc = local_queue_enqueue(&q, i);
        if (rc != 0) fail("test_queue_basic - enqueue");
    }

    for (int i = 0; i < max_sz; ++i) {
        int v = local_queue_dequeue(&q);
        if (v != i) fail("test_queue_basic - dequeue/order");
    }

    local_queue_destroy(&q);
    pass("test_queue_basic");
}

/* -------------------------
   Test 2: Concurrent producers/consumers on queue
   ------------------------- */

#define PROD_COUNT 4
#define CONS_COUNT 4
#define OPS_PER_PROD 2000

static local_queue_t q_conc;
static pthread_mutex_t consumed_lock = PTHREAD_MUTEX_INITIALIZER;
static int consumed_total = 0;

void *producer_fn(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < OPS_PER_PROD; ++i) {
        int val = id * 100000 + i;
        while (local_queue_enqueue(&q_conc, val) != 0) {
            usleep(100);
        }
    }
    return NULL;
}

void *consumer_fn(void *arg)
{
    (void)arg;
    int local_count = 0;
    int target = (PROD_COUNT * OPS_PER_PROD) / CONS_COUNT;
    for (int i = 0; i < target; ++i) {
        int v = local_queue_dequeue(&q_conc);
        (void)v;
        ++local_count;
    }
    pthread_mutex_lock(&consumed_lock);
    consumed_total += local_count;
    pthread_mutex_unlock(&consumed_lock);
    return NULL;
}

void test_queue_concurrent(void)
{
    int rc = local_queue_init(&q_conc, 128);
    if (rc != 0) fail("test_queue_concurrent - init");

    pthread_t prods[PROD_COUNT], cons[CONS_COUNT];

    for (long i = 0; i < PROD_COUNT; ++i) {
        if (pthread_create(&prods[i], NULL, producer_fn, (void *)(intptr_t)i) != 0)
            fail("test_queue_concurrent - create producer");
    }
    for (int i = 0; i < CONS_COUNT; ++i) {
        if (pthread_create(&cons[i], NULL, consumer_fn, NULL) != 0)
            fail("test_queue_concurrent - create consumer");
    }

    for (int i = 0; i < PROD_COUNT; ++i) pthread_join(prods[i], NULL);
    for (int i = 0; i < CONS_COUNT; ++i) pthread_join(cons[i], NULL);

    int expected = PROD_COUNT * OPS_PER_PROD;
    if (consumed_total != expected) {
        fprintf(stderr, "Consumed %d, expected %d\n", consumed_total, expected);
        fail("test_queue_concurrent - mismatch");
    }

    local_queue_destroy(&q_conc);
    pass("test_queue_concurrent");
}

/* -------------------------
   Test 3: Basic cache ops
   ------------------------- */
void test_cache_basic(void)
{
    size_t max_size = 10 * 1024 * 1024;
    if (cache_init(max_size) != 0) fail("test_cache_basic - init");

    const char *key = "/tests/cache_basic_key";
    const char *payload = "hello-cache-basic";
    size_t payload_len = strlen(payload);

    if (cache_put(key, payload, payload_len) != 0) fail("test_cache_basic - put");

    char *out_buf = NULL;
    size_t out_len = 0;
    if (cache_get(key, &out_buf, &out_len) != 0) fail("test_cache_basic - get");
    if (out_len != payload_len) { free(out_buf); fail("test_cache_basic - get length mismatch"); }
    if (memcmp(out_buf, payload, payload_len) != 0) { free(out_buf); fail("test_cache_basic - content mismatch"); }
    free(out_buf);

    cache_destroy();
    pass("test_cache_basic");
}

/* -------------------------
   Test 4: Simple cache concurrency
   ------------------------- */

#define CACHE_WRITERS 2
#define CACHE_READERS 8
#define WRITES_PER_WRITER 200
#define READS_PER_READER 500

static const char *cache_keys[] = { "/k/one", "/k/two", "/k/three", "/k/four", "/k/five" };
static int n_cache_keys = sizeof(cache_keys) / sizeof(cache_keys[0]);

void *cache_writer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < WRITES_PER_WRITER; ++i) {
        int k = (id + i) % n_cache_keys;
        char payload[1024];
        int len = snprintf(payload, sizeof(payload), "writer-%d-iter-%d", id, i);
        cache_put(cache_keys[k], payload, (size_t)len);
        usleep(100);
    }
    return NULL;
}

void *cache_reader(void *arg)
{
    (void)arg;
    for (int i = 0; i < READS_PER_READER; ++i) {
        int k = rand() % n_cache_keys;
        char *out = NULL;
        size_t out_len = 0;
        if (cache_get(cache_keys[k], &out, &out_len) == 0) {
            if (out_len == 0) { free(out); fail("test_cache_concurrency - zero length read"); }
            free(out);
        }
        usleep(50);
    }
    return NULL;
}

void test_cache_concurrency(void)
{
    size_t max_size = 10 * 1024 * 1024;
    if (cache_init(max_size) != 0) fail("test_cache_concurrency - init");

    pthread_t writers[CACHE_WRITERS], readers[CACHE_READERS];
    srand((unsigned)(time(NULL) ^ getpid()));

    for (long i = 0; i < CACHE_WRITERS; ++i)
        if (pthread_create(&writers[i], NULL, cache_writer, (void *)(intptr_t)i) != 0)
            fail("test_cache_concurrency - create writer");

    for (int i = 0; i < CACHE_READERS; ++i)
        if (pthread_create(&readers[i], NULL, cache_reader, NULL) != 0)
            fail("test_cache_concurrency - create reader");

    for (int i = 0; i < CACHE_WRITERS; ++i) pthread_join(writers[i], NULL);
    for (int i = 0; i < CACHE_READERS; ++i) pthread_join(readers[i], NULL);

    cache_destroy();
    pass("test_cache_concurrency");
}

/* -------------------------
   Test 5: Cache Data Integrity
   ------------------------- */

#define INTEGRITY_ITERS 1000
#define INTEGRITY_THREADS 4

void *integrity_worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    char key[64];
    char data[64];
    snprintf(key, sizeof(key), "/integrity/%d", id);
    snprintf(data, sizeof(data), "data-for-%d", id);
    size_t len = strlen(data);

    for (int i = 0; i < INTEGRITY_ITERS; ++i) {
        /* Write own key */
        cache_put(key, data, len);
        
        /* Read own key and verify */
        char *out = NULL;
        size_t out_len = 0;
        if (cache_get(key, &out, &out_len) == 0) {
            if (out_len != len || memcmp(out, data, len) != 0) {
                free(out);
                fail("test_cache_integrity - data mismatch");
            }
            free(out);
        }
        
        /* Occasionally read neighbor's key */
        int neighbor = (id + 1) % INTEGRITY_THREADS;
        char nkey[64];
        snprintf(nkey, sizeof(nkey), "/integrity/%d", neighbor);
        if (cache_get(nkey, &out, &out_len) == 0) {
            char expected[64];
            snprintf(expected, sizeof(expected), "data-for-%d", neighbor);
            if (out_len != strlen(expected) || memcmp(out, expected, out_len) != 0) {
                free(out);
                fail("test_cache_integrity - neighbor data mismatch");
            }
            free(out);
        }
    }
    return NULL;
}

void test_cache_integrity(void)
{
    if (cache_init(1024 * 1024) != 0) fail("test_cache_integrity - init");
    
    pthread_t threads[INTEGRITY_THREADS];
    for (long i = 0; i < INTEGRITY_THREADS; ++i) {
        pthread_create(&threads[i], NULL, integrity_worker, (void *)(intptr_t)i);
    }
    
    for (int i = 0; i < INTEGRITY_THREADS; ++i) pthread_join(threads[i], NULL);
    
    cache_destroy();
    pass("test_cache_integrity");
}

/* -------------------------
   Test 6: Cache Eviction (LRU)
   ------------------------- */
void test_cache_eviction(void)
{
    /* Small cache: 100 bytes */
    if (cache_init(100) != 0) fail("test_cache_eviction - init");

    /* Add 5 items of 20 bytes each (fills cache) */
    char val[20];
    memset(val, 'A', 20);
    
    cache_put("/k/1", val, 20);
    cache_put("/k/2", val, 20);
    cache_put("/k/3", val, 20);
    cache_put("/k/4", val, 20);
    cache_put("/k/5", val, 20);

    /* Verify all present */
    char *out; size_t len;
    if (cache_get("/k/1", &out, &len) != 0) fail("test_cache_eviction - missing /k/1"); free(out);
    if (cache_get("/k/5", &out, &len) != 0) fail("test_cache_eviction - missing /k/5"); free(out);

    /* Access /k/1 to make it MRU (Most Recently Used) */
    if (cache_get("/k/1", &out, &len) != 0) fail("test_cache_eviction - missing /k/1 (2)"); free(out);

    /* Cache state (MRU -> LRU): 1, 5, 4, 3, 2 */
    
    /* Add new item (forces eviction of LRU: /k/2) */
    cache_put("/k/6", val, 20);

    /* Verify /k/2 is gone */
    if (cache_get("/k/2", &out, &len) == 0) { free(out); fail("test_cache_eviction - /k/2 should be evicted"); }
    
    /* Verify /k/1 (MRU) and /k/6 (New) are present */
    if (cache_get("/k/1", &out, &len) != 0) fail("test_cache_eviction - /k/1 evicted incorrectly"); free(out);
    if (cache_get("/k/6", &out, &len) != 0) fail("test_cache_eviction - /k/6 missing"); free(out);

    cache_destroy();
    pass("test_cache_eviction");
}

/* -------------------------
   Test 7: Queue Shutdown
   ------------------------- */
static local_queue_t q_shut;

void *shutdown_consumer(void *arg)
{
    (void)arg;
    int v = local_queue_dequeue(&q_shut);
    if (v != -1) {
        fprintf(stderr, "Expected -1 (shutdown), got %d\n", v);
        exit(1);
    }
    return NULL;
}

void test_queue_shutdown(void)
{
    if (local_queue_init(&q_shut, 10) != 0) fail("test_queue_shutdown - init");

    pthread_t t;
    if (pthread_create(&t, NULL, shutdown_consumer, NULL) != 0) fail("test_queue_shutdown - create");

    /* Give thread time to block */
    usleep(100000);

    /* Trigger shutdown */
    pthread_mutex_lock(&q_shut.mutex);
    q_shut.shutting_down = 1;
    pthread_cond_broadcast(&q_shut.cond);
    pthread_mutex_unlock(&q_shut.mutex);

    pthread_join(t, NULL);
    
    local_queue_destroy(&q_shut);
    pass("test_queue_shutdown");
}

/* -------------------------
   Runner
   ------------------------- */

int main(void)
{
    printf("Running concurrency tests...\n");
    test_queue_basic();
    test_queue_concurrent();
    test_cache_basic();
    test_cache_concurrency();
    test_cache_integrity();
    test_cache_eviction();
    test_queue_shutdown();
    printf("All tests completed.\n");
    return 0;
}