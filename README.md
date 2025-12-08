# Multi-Threaded Web Server with IPC and Semaphores

## Authors
Alan Marques
125046

Miguel Sousa
125624

## Description
This project implements a high-performance Concurrent HTTP Web Server in C, designed to handle high loads through a Multi-Process and Multi-Threaded architecture. The server acts as a robust system that serves static files while maintaining detailed runtime statistics and access logs

## Installation

### Prerequisites
* **OS:** Linux 
* **Compiler:** GCC (GNU Compiler Collection)
* **Build Tool:** GNU Make
* **Libraries:** `pthread` (POSIX Threads), `rt` (Real-time Extensions)

### Build Instructions
Build the server executable:
```make all```

Then run the server:
```./server```

To remove object files and executables:
```make clean```

To compile and run in a single step:
```make run```

To run automated tests:
```make test```

## Features
- Feature 1: Producer-Consumer
- Feature 2: Thread Pool Management
- Feature 3: Shared Statistics
- Feature 4: Thread-Safe File Cache
- Feature 5: Thread-Safe Logging

## Configuration
The server is configured via the `server.conf` file located in the root directory. This file allows you to tune performance parameters without recompiling the code.

**Default Configuration:**
```ini
PORT=8080
DOCUMENT_ROOT=www
NUM_WORKERS=4
THREADS_PER_WORKER=10
MAX_QUEUE_SIZE=100
LOG_FILE=access.log
CACHE_SIZE_MB=10
TIMEOUT_SECONDS=30
```

## Examples

### 1. Basic File Request
Start the server and fetch a static HTML file using `curl`. This verifies that the server handles standard GET requests correctly.

```bash
# Terminal 1: Start Server
./server

# Terminal 2: Client Request
curl -v http://localhost:8080/index.html
```
### 2. High-Concurrency Stress Test
Use Apache Bench (```ab```) to simulate high load. This demonstrates the Thread Pool and Producer-Consumer architecture handling concurrent connections.
```bash
# -n: Total requests (10000)
# -c: Concurrent users (100)
ab -n 10000 -c 100 http://localhost:8080/index.html
```

### 3. Monitoring Statistics
While the server is running under load, observe the console output. The Shared Statistics module prints metrics every ```TIMEOUT_SECONDS``` (default: 30s).

```
=== SERVER STATISTICS ===
Active Connections: 50
Total Requests:     2000
Bytes Transferred:  1024000
Avg Response Time:  2.45 ms
Status 200 (OK):    2000
Status 404 (NF):    0
Status 500 (Err):   0
=========================
```

## References

* **Linux Man Pages:** Used extensively for system calls like [`fork(2)`](https://man7.org/linux/man-pages/man2/fork.2.html), [`mmap(2)`](https://man7.org/linux/man-pages/man2/mmap.2.html), and [`socketpair(2)`](https://man7.org/linux/man-pages/man2/socketpair.2.html).
* **POSIX Threads (pthreads):** Reference for thread management functions like [`pthread_create`](https://man7.org/linux/man-pages/man3/pthread_create.3.html) and synchronization primitives.
* **Valgrind Documentation:** Used for memory leak checks and race condition analysis. [Official Docs](https://valgrind.org/docs/manual/manual.html)
