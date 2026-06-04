CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99
LDLIBS = -lncurses -lcurl
OBJS = main.o ui.o input.o buffer.o runner.o

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  HOMEBREW_PREFIX := $(shell brew --prefix ncurses 2>/dev/null)
  ifneq ($(HOMEBREW_PREFIX),)
    CFLAGS += -I$(HOMEBREW_PREFIX)/include
    LDLIBS := -L$(HOMEBREW_PREFIX)/lib $(LDLIBS)
  endif
endif

all: thingy

thingy: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

clean:
	rm -f thingy $(OBJS)

format:
	@astyle --indent=spaces=2 main.c ui.c input.c buffer.c runner.c buffer.h runner.h editor.h

.PHONY: all clean format
