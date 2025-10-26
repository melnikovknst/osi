CC      := gcc
CFLAGS  := -O2 -Wall -Wextra
AR      := ar
RANLIB  := ranlib

LIB     := libmythread.a
LIB_OBJS:= mythread.o

MAIN    := mytest      
MAIN_SRC:= mytest.c    

.PHONY: all clean run

all: $(LIB) $(MAIN)

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

mythread.o: mythread.c mythread.h
	$(CC) $(CFLAGS) -c $< -o $@

$(MAIN): $(MAIN_SRC) $(LIB) mythread.h
	$(CC) $(CFLAGS) $(MAIN_SRC) -L. -lmythread -o $@

run: all
	./$(MAIN)

clean:
	rm -f *.o $(LIB) $(MAIN)
