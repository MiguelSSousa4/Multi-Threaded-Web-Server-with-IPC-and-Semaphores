#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* Project headers - adjust path if your build expects a different include layout */
#include "../src/worker.h"
#include "../src/cache.h"

/* Small helpers */
static void pass(const char *name) { printf("[PASS] %s\n", name); fflush(stdout); }
static void fail(const char *name) { fprintf(stderr, "[FAIL] %s\n", name); fflush(stderr); exit(1); }

/* -------------------------
   Test 1: Basic queue ops
   ------------------------- */
void test_queue_basic(void)
{
    local_queue_t q;
    int max_sz = 8;
    int rc = local_queue_init(&q, max_sz);
    if (rc != 0) fail("test_queue_basic - init");

    /* enqueue 0..max_sz-1 */
    for (int i = 0; i < max_sz; ++i) {
        rc = local_queue_enqueue(&q, i);
        if (rc != 0) fail("test_queue_basic - enqueue");
    }

    /* dequeue and check order */
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
    int id = (int)(long)arg;
    for (int i = 0; i < OPS_PER_PROD; ++i) {
        int val = id * 100000 + i;
        /* retry until enqueue succeeds (main behavior may reject if full) */
        while (local_queue_enqueue(&q_conc, val) != 0) {
            /* queue full: yield briefly and retry */
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
        int v = local_queue_dequeue(&q_conc); /* blocks until item available */
        (void)v; /* value verification across threads is complex; ensure count */
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
        if (pthread_create(&prods[i], NULL, producer_fn, (void *)i) != 0)
            fail("test_queue_concurrent - create producer");
    }
    for (int i = 0; i < CONS_COUNT; ++i) {
        if (pthread_create(&cons[i], NULL, consumer_fn, NULL) != 0)
            fail("test_queue_concurrent - create consumer");
    }

    for (int i = 0; i < PROD_COUNT; ++i) pthread_join(prods[i], NULL);
    for (int i = 0; i < CONS_COUNT; ++i) pthread_join(cons[i], NULL);

    /* after all producers/consumers done, queue should be empty */
    /* Try a non-blocking check by attempting to dequeue with short timeout expectation:
       Our queue API blocks on dequeue, so we instead rely on counts. */
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
    size_t max_size = 10 * 1024 * 1024; /* 10MB */
    if (cache_init(max_size) != 0) fail("test_cache_basic - init");

    const char *key = "/tests/cache_basic_key";
    const char *payload = "hello-cache-basic";
    size_t payload_len = strlen(payload);

    /* cache_put: expected prototype int cache_put(const char *path, const char *buf, size_t len) */
    if (cache_put(key, payload, payload_len) != 0) fail("test_cache_basic - put");

    char *out_buf = NULL;
    size_t out_len = 0;
    if (cache_get(key, &out_buf, &out_len) != 0) fail("test_cache_basic - get");
    if (out_len != payload_len) {
        free(out_buf);
        fail("test_cache_basic - get length mismatch");
    }
    if (memcmp(out_buf, payload, payload_len) != 0) {
        free(out_buf);
        fail("test_cache_basic - content mismatch");
    }
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
    int id = (int)(long)arg;
    for (int i = 0; i < WRITES_PER_WRITER; ++i) {
        int k = (id + i) % n_cache_keys;
        char payload[1024];
        int len = snprintf(payload, sizeof(payload), "writer-%d-iter-%d", id, i);
        /* keep sizes small (<1MB) as per spec */
        cache_put(cache_keys[k], payload, (size_t)len);
        /* small sleep to mix readers/writers */
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
            /* If present, ensure length > 0 */
            if (out_len == 0) {
                free(out);
                fail("test_cache_concurrency - zero length read");
            }
            free(out);
        }
        /* else miss is acceptable */
        usleep(50);
    }
    return NULL;
}

void test_cache_concurrency(void)
{
    size_t max_size = 10 * 1024 * 1024; /* 10MB */
    if (cache_init(max_size) != 0) fail("test_cache_concurrency - init");

    pthread_t writers[CACHE_WRITERS], readers[CACHE_READERS];
    srand((unsigned)time(NULL));

    for (long i = 0; i < CACHE_WRITERS; ++i)
        if (pthread_create(&writers[i], NULL, cache_writer, (void *)i) != 0)
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
   Runner
   ------------------------- */

int main(void)
{
    printf("Running concurrency tests...\n");

    test_queue_basic();
    test_queue_concurrent();
    test_cache_basic();
    test_cache_concurrency();

    printf("All tests completed.\n");
    return 0;
}