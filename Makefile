CC = gcc
CFLAGS = -Wall -Wextra -pthread 
LDFLAGS = -lrt
SRC = src/master.c src/worker.c src/shared_mem.c src/semaphores.c src/config.c src/http.c src/ipc.c src/stats.c
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
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean run

