CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -Isrc
LDLIBS = -lncurses -lcurl
SRCDIR = src
BUILDDIR = build
BINDIR = $(BUILDDIR)/bin
OBJS = $(BUILDDIR)/main.o $(BUILDDIR)/ui.o $(BUILDDIR)/input.o $(BUILDDIR)/buffer.o $(BUILDDIR)/runner.o

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  HOMEBREW_PREFIX := $(shell brew --prefix ncurses 2>/dev/null)
  ifneq ($(HOMEBREW_PREFIX),)
    CFLAGS += -I$(HOMEBREW_PREFIX)/include
    LDLIBS := -L$(HOMEBREW_PREFIX)/lib $(LDLIBS)
  endif
endif

all: $(BINDIR)/thingy

$(BINDIR)/thingy: $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	rm -rf $(BUILDDIR)

format:
	@astyle --indent=spaces=2 $(SRCDIR)/*.c $(SRCDIR)/*.h

.PHONY: all clean format
