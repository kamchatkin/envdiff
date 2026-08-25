CC ?= cc
VERSION ?= 0.3.0
CPPFLAGS += -DENVDIFF_VERSION=\"$(VERSION)\"
CFLAGS += -std=c11 -Os -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections -fstack-protector-strong

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LDFLAGS += -Wl,-dead_strip -Wl,-x
else
CPPFLAGS += -D_FORTIFY_SOURCE=2
LDFLAGS += -Wl,--gc-sections -Wl,-s
endif

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
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	install -m 755 envdiff "$(DESTDIR)$(PREFIX)/bin/envdiff"

uninstall:
	$(RM) "$(DESTDIR)$(PREFIX)/bin/envdiff"

clean:
	$(RM) envdiff
