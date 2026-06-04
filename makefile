CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -D_POSIX_C_SOURCE=200809L -Isrc
LDLIBS = -lncurses -lcurl
SRCDIR = src
BUILDDIR = build
BINDIR = $(BUILDDIR)/bin
OBJS = $(BUILDDIR)/main.o $(BUILDDIR)/ui.o $(BUILDDIR)/input.o $(BUILDDIR)/buffer.o $(BUILDDIR)/runner.o

PREFIX ?= /usr/local
VERSION ?= 1.0.0
RELEASEDIR = release

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_S),Darwin)
  HOMEBREW_PREFIX := $(shell brew --prefix ncurses 2>/dev/null)
  ifneq ($(HOMEBREW_PREFIX),)
    CFLAGS += -I$(HOMEBREW_PREFIX)/include
    LDLIBS := -L$(HOMEBREW_PREFIX)/lib $(LDLIBS)
  endif
endif

# --- Core targets ---

all: $(BINDIR)/thingy

$(BINDIR)/thingy: $(OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

install: $(BINDIR)/thingy
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BINDIR)/thingy $(DESTDIR)$(PREFIX)/bin/thingy

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/thingy

clean:
	rm -rf $(BUILDDIR) $(RELEASEDIR)

format:
	@astyle --indent=spaces=2 $(SRCDIR)/*.c $(SRCDIR)/*.h

# --- Packaging targets ---

# macOS native package (current arch)
package-macos:
	@echo "==> Packaging for macOS $(UNAME_M)..."
	@mkdir -p $(RELEASEDIR)/thingy-$(VERSION)-macos-$(UNAME_M)
	$(MAKE) clean && $(MAKE)
	install -m 755 $(BINDIR)/thingy $(RELEASEDIR)/thingy-$(VERSION)-macos-$(UNAME_M)/thingy
	@echo "thingy $(VERSION)" > $(RELEASEDIR)/thingy-$(VERSION)-macos-$(UNAME_M)/VERSION
	@cd $(RELEASEDIR) && tar -czf thingy-$(VERSION)-macos-$(UNAME_M).tar.gz thingy-$(VERSION)-macos-$(UNAME_M)
	@echo "==> Package: $(RELEASEDIR)/thingy-$(VERSION)-macos-$(UNAME_M).tar.gz"

# Linux packages via Docker (multi-arch)
package-linux-amd64:
	@echo "==> Building for Linux amd64 via Docker..."
	@mkdir -p $(RELEASEDIR)
	docker run --rm -v "$(CURDIR):/src" -w /src ubuntu:22.04 bash -c "\
		apt-get update -qq && \
		apt-get install -y -qq gcc make libncurses-dev libcurl4-openssl-dev > /dev/null 2>&1 && \
		make clean && make CC=gcc && \
		mkdir -p /tmp/pkg/thingy-$(VERSION)-linux-amd64 && \
		install -m 755 build/bin/thingy /tmp/pkg/thingy-$(VERSION)-linux-amd64/thingy && \
		echo 'thingy $(VERSION)' > /tmp/pkg/thingy-$(VERSION)-linux-amd64/VERSION && \
		cd /tmp/pkg && tar -czf /src/$(RELEASEDIR)/thingy-$(VERSION)-linux-amd64.tar.gz thingy-$(VERSION)-linux-amd64"
	@echo "==> Package: $(RELEASEDIR)/thingy-$(VERSION)-linux-amd64.tar.gz"

package-linux-arm64:
	@echo "==> Building for Linux arm64 via Docker..."
	@mkdir -p $(RELEASEDIR)
	docker run --rm --platform linux/arm64 -v "$(CURDIR):/src" -w /src ubuntu:22.04 bash -c "\
		apt-get update -qq && \
		apt-get install -y -qq gcc make libncurses-dev libcurl4-openssl-dev > /dev/null 2>&1 && \
		make clean && make CC=gcc && \
		mkdir -p /tmp/pkg/thingy-$(VERSION)-linux-arm64 && \
		install -m 755 build/bin/thingy /tmp/pkg/thingy-$(VERSION)-linux-arm64/thingy && \
		echo 'thingy $(VERSION)' > /tmp/pkg/thingy-$(VERSION)-linux-arm64/VERSION && \
		cd /tmp/pkg && tar -czf /src/$(RELEASEDIR)/thingy-$(VERSION)-linux-arm64.tar.gz thingy-$(VERSION)-linux-arm64"
	@echo "==> Package: $(RELEASEDIR)/thingy-$(VERSION)-linux-arm64.tar.gz"

# All packages
package-all: package-macos package-linux-amd64 package-linux-arm64
	@echo ""
	@echo "==> All packages in $(RELEASEDIR)/:"
	@ls -lh $(RELEASEDIR)/*.tar.gz 2>/dev/null

.PHONY: all install uninstall clean format \
	package-macos package-linux-amd64 package-linux-arm64 package-all
