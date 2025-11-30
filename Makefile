CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -pthread -std=c11
LDFLAGS = -pthread
SRC = main.c list.c
OBJ = $(SRC:.c=.o)
BIN = test

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
