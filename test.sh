#!/bin/sh
set -eu

test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT HUP INT TERM

example="$test_dir/example.env"
current="$test_dir/current.env"
expected="$test_dir/expected.env"

version="$(./envdiff --version)"
if [ "$version" != 'envdiff 0.1.0' ]; then
    echo "неожиданная версия: $version" >&2
    exit 1
fi

printf '%s\n' \
    '# NATS' \
    'NATS_URL=tls://nats:4222' \
    '# CA certificate' \
    'NATS_TLS_CA_FILE=/etc/nats/certs/ca.crt' > "$example"

printf '%s\n' \
    'NATS_URL=nats://private-host:4222' \
    'NATS_PASSWORD=do-not-touch' > "$current"

printf '%s\n' \
    '# NATS' \
    'NATS_URL=nats://private-host:4222' \
    'NATS_PASSWORD=do-not-touch' \
    '# CA certificate' \
    'NATS_TLS_CA_FILE=/etc/nats/certs/ca.crt' > "$expected"

if ./envdiff --check "$example" "$current"; then
    echo 'envdiff --check не сообщил о новых ключах' >&2
    exit 1
else
    status=$?
    if [ "$status" -ne 1 ]; then
        echo "envdiff --check вернул неожиданный код $status" >&2
        exit 1
    fi
fi

./envdiff "$example" "$current"
cmp "$expected" "$current"

if ./envdiff --check "$example" "$current"; then
    :
else
    status=$?
    echo "envdiff --check вернул неожиданный код $status" >&2
    exit 1
fi

printf '%s\n' 'A=one' 'A=two' > "$example"
if ./envdiff "$example" "$current" 2>/dev/null; then
    echo 'повторяющийся ключ в example не был отклонён' >&2
    exit 1
fi

echo 'envdiff: тесты пройдены'
