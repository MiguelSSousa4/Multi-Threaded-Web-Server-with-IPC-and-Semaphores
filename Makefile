CC = gcc
CFLAGS = -Wall -Wextra -pthread 
LDFLAGS = -lrt
SRC = src/main.c src/master.c src/worker.c src/shared_mem.c src/semaphores.c src/config.c src/http.c src/ipc.c src/stats.c src/logger.c src/thread_pool.c src/cache.c
OBJ = $(SRC:.c=.o)
TARGET = server

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET) *.log

test: $(TARGET)
	chmod +x tests/test_load.sh
	./tests/test_load.sh

.PHONY: all clean run test

