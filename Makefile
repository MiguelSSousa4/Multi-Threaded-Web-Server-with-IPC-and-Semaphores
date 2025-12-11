CC = gcc
CFLAGS = -Wall -Wextra -pthread 
LDFLAGS = -lrt
SRC = src/main.c src/master.c src/worker.c src/shared_mem.c src/semaphores.c src/config.c src/http.c src/ipc.c src/stats.c src/logger.c src/thread_pool.c src/cache.c
OBJ = $(SRC:.c=.o)
TARGET = server

TEST_SRC = tests/test_concurrent.c
TEST_BIN = tests/test_concurrent

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_SRC) $(OBJ)
	$(CC) $(CFLAGS) $(TEST_SRC) $(filter-out src/main.o, $(OBJ)) -o $(TEST_BIN) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_BIN) *.log

test: $(TARGET) $(TEST_BIN)
	@echo "--- Executing tests in c ---"
	./$(TEST_BIN)
	@echo "--- Executing tests in bash ---"
	chmod +x tests/test_load.sh
	./tests/test_load.sh

.PHONY: all clean run test