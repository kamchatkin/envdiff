CC ?= cc
VERSION ?= 0.1.0
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_FORTIFY_SOURCE=2 -DENVDIFF_VERSION=\"$(VERSION)\"
CFLAGS += -std=c11 -Os -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections -fstack-protector-strong
LDFLAGS += -Wl,--gc-sections -Wl,-s

PREFIX ?= $(HOME)/.local

.PHONY: all check test install uninstall clean

all: envdiff

envdiff: envdiff.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@echo "envdiff: $$(wc -c < "$@") bytes"

check: envdiff
	sh ./test.sh

test: check

install: envdiff
	install -Dm755 envdiff "$(DESTDIR)$(PREFIX)/bin/envdiff"

uninstall:
	$(RM) "$(DESTDIR)$(PREFIX)/bin/envdiff"

clean:
	$(RM) envdiff
