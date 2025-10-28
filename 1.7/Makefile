CC      := gcc
CFLAGS  := -O2 -Wall -Wextra
AR      := ar
RANLIB  := ranlib

LIB     := libuthread.a
LIB_OBJS:= uthread.o

MAIN    := utest      
MAIN_SRC:= utest.c    

.PHONY: all clean run

all: $(LIB) $(MAIN)

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

uthread.o: uthread.c uthread.h
	$(CC) $(CFLAGS) -c $< -o $@

$(MAIN): $(MAIN_SRC) $(LIB) uthread.h
	$(CC) $(CFLAGS) $(MAIN_SRC) -L. -luthread -o $@

run: all
	./$(MAIN)

clean:
	rm -f *.o $(LIB) $(MAIN)
