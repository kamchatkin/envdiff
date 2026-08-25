CC ?= cc
VERSION ?= 0.1.0
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_FORTIFY_SOURCE=2 -DENVDIFF_VERSION=\"$(VERSION)\"
CFLAGS += -std=c11 -Os -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections -fstack-protector-strong
LDFLAGS += -Wl,--gc-sections -Wl,-s

PREFIX ?= $(HOME)/.local
MAX_BYTES ?= 102400

.PHONY: all check test install uninstall clean

all: envdiff

envdiff: envdiff.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@size="$$(wc -c < "$@")"; \
	if [ "$$size" -gt "$(MAX_BYTES)" ]; then \
		echo "envdiff: размер $$size байт превышает лимит $(MAX_BYTES)" >&2; \
		$(RM) "$@"; \
		exit 1; \
	fi
	@echo "envdiff: $$(wc -c < "$@") байт"

check: envdiff
	sh ./test.sh

test: check

install: envdiff
	install -Dm755 envdiff "$(DESTDIR)$(PREFIX)/bin/envdiff"

uninstall:
	$(RM) "$(DESTDIR)$(PREFIX)/bin/envdiff"

clean:
	$(RM) envdiff
