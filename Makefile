CC = gcc
CFLAGS = -Wall -Wextra -pthread 
LDFLAGS = -lrt
SRC = src/main.c src/master.c src/worker.c src/shared_mem.c src/semaphores.c src/config.c src/http.c src/ipc.c src/stats.c src/logger.c src/thread_pool.c src/cache.c
OBJ = $(SRC:.c=.o)
TARGET = server

TEST_SRC = tests/test_concurrent.c src/worker.c src/shared_mem.c src/semaphores.c src/config.c src/http.c src/ipc.c src/stats.c src/logger.c src/thread_pool.c src/cache.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_TARGET = test_concurrent

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET) $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJ)
	$(CC) $(CFLAGS) $(TEST_OBJ) -o $(TEST_TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TEST_OBJ) $(TARGET) $(TEST_TARGET) *.log

test: $(TARGET)
	chmod +x tests/test_load.sh
	./tests/test_load.sh

.PHONY: all clean run test

