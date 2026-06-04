CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99
LDLIBS = -lncurses -lcurl
OBJS = main.o buffer.o runner.o

all: thingy

thingy: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

clean:
	rm -f thingy $(OBJS)

format:
	@astyle --indent=spaces=2 main.c buffer.c runner.c buffer.h runner.h

.PHONY: all clean format
