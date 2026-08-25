#!/bin/sh
set -eu

test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT HUP INT TERM

example="$test_dir/example.env"
current="$test_dir/current.env"
expected="$test_dir/expected.env"
filtered="$test_dir/filtered.env"
output="$test_dir/output.env"
original="$test_dir/original.env"

version="$(./envdiff --version)"
if [ "$version" != 'envdiff 0.2.0' ]; then
    echo "unexpected version: $version" >&2
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
cp "$current" "$original"

printf '%s\n' \
    '# NATS' \
    'NATS_URL=nats://private-host:4222' \
    'NATS_PASSWORD=do-not-touch' \
    '# CA certificate' \
    'NATS_TLS_CA_FILE=/etc/nats/certs/ca.crt' > "$expected"

if ./envdiff --check "$example" "$current"; then
    echo 'envdiff --check did not report missing keys' >&2
    exit 1
else
    status=$?
    if [ "$status" -ne 1 ]; then
        echo "envdiff --check returned unexpected status $status" >&2
        exit 1
    fi
fi

./envdiff "$example" "$current" > "$filtered"
cmp "$expected" "$filtered"
cmp "$original" "$current"

./envdiff --output "$output" "$example" "$current"
cmp "$expected" "$output"
cmp "$original" "$current"

./envdiff -o "$current" "$example" "$current"
cmp "$expected" "$current"

if ./envdiff --check "$example" "$current"; then
    :
else
    status=$?
    echo "envdiff --check returned unexpected status $status" >&2
    exit 1
fi

if ./envdiff --check -o "$output" "$example" "$current" 2>/dev/null; then
    echo '--check accepted --output' >&2
    exit 1
else
    status=$?
    if [ "$status" -ne 2 ]; then
        echo "invalid option combination returned unexpected status $status" >&2
        exit 1
    fi
fi

printf '%s\n' 'A=one' 'A=two' > "$example"
if ./envdiff "$example" "$current" 2>/dev/null; then
    echo 'duplicate key in example file was not rejected' >&2
    exit 1
fi

echo 'envdiff: tests passed'
