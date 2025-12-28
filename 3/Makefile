CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -pthread -std=c11
LDFLAGS = -pthread
SRC = main.c threadpool.c cache.c net.c http.c proxy.c logger.c
OBJ = $(SRC:.c=.o)
BIN = proxy

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
