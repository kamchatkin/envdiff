#!/bin/sh
set -eu

test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT HUP INT TERM

example="$test_dir/example.env"
current="$test_dir/current.env"
example_named_current="$test_dir/current.EXAMPLE.env"
expected="$test_dir/expected.env"
filtered="$test_dir/filtered.env"
output="$test_dir/output.env"
original="$test_dir/original.env"
check_output="$test_dir/check.out"
check_error="$test_dir/check.err"
expected_check="$test_dir/expected-check.out"

version="$(./envdiff --version)"
if [ "$version" != 'envdiff 0.5.0' ]; then
    echo "unexpected version: $version" >&2
    exit 1
fi

printf '%s\n' \
    '# NATS' \
    'NATS_URL=tls://nats:4222' \
    'NATS_PASSWORD=example-placeholder' \
    '# CA certificate' \
    'NATS_TLS_CA_FILE=/etc/nats/certs/ca.crt' > "$example"

printf '%s\n' \
    'NATS_URL=nats://private-host:4222' \
    'NATS_PASSWORD=do-not-touch' > "$current"
cp "$current" "$original"
cp "$current" "$example_named_current"

printf '%s\n' \
    '# NATS' \
    'NATS_URL=nats://private-host:4222' \
    'NATS_PASSWORD=do-not-touch' \
    '# CA certificate' \
    'NATS_TLS_CA_FILE=/etc/nats/certs/ca.crt' > "$expected"

if ./envdiff "$example" "$example_named_current" > "$check_output" 2> "$check_error"; then
    echo "current file containing 'example' was not rejected" >&2
    exit 1
else
    status=$?
    if [ "$status" -ne 2 ]; then
        echo "example-name guard returned unexpected status $status" >&2
        exit 1
    fi
fi
if [ -s "$check_output" ] || ! grep -F -- '--force' "$check_error" >/dev/null; then
    echo "example-name guard did not produce the expected diagnostic" >&2
    exit 1
fi
./envdiff -f "$example" "$example_named_current" > "$filtered"
cmp "$expected" "$filtered"

if ./envdiff "$example" "$current" -c > "$check_output" 2> "$check_error"; then
    echo 'envdiff --check did not report missing keys' >&2
    exit 1
else
    status=$?
    if [ "$status" -ne 1 ]; then
        echo "envdiff --check returned unexpected status $status" >&2
        exit 1
    fi
fi
if [ -s "$check_error" ]; then
    echo 'envdiff --check wrote an unexpected diagnostic' >&2
    exit 1
fi
printf '%s\n' \
    'missing keys: 1, only in current: 0, new comments: 2' \
    '' \
    "Missing from $current:" \
    '' \
    '+ # CA certificate' \
    '+ NATS_TLS_CA_FILE=/etc/nats/certs/ca.crt' \
    '' \
    "Comments missing from $current:" \
    '' \
    '+ # NATS' \
    '  NATS_URL (existing value is not compared)' \
    '' > "$expected_check"
cmp "$expected_check" "$check_output"

./envdiff "$example" "$current" > "$filtered"
cmp "$expected" "$filtered"
cmp "$original" "$current"

./envdiff --output "$output" "$example" "$current"
cmp "$expected" "$output"
cmp "$original" "$current"

./envdiff "$example" "$current" -i
cmp "$expected" "$current"

if ./envdiff --check "$example" "$current" > "$check_output" 2> "$check_error"; then
    :
else
    status=$?
    echo "envdiff --check returned unexpected status $status" >&2
    exit 1
fi
if [ -s "$check_output" ] || [ -s "$check_error" ]; then
    echo 'envdiff --check was not quiet for matching files' >&2
    exit 1
fi

printf '%s\n' \
    '# Legacy option' \
    'LEGACY_TOKEN=remove-this-secret' >> "$current"
if ./envdiff --check "$example" "$current" > "$check_output" 2> "$check_error"; then
    echo 'envdiff --check did not report a current-only key' >&2
    exit 1
else
    status=$?
    if [ "$status" -ne 1 ]; then
        echo "current-only check returned unexpected status $status" >&2
        exit 1
    fi
fi
printf '%s\n' \
    'missing keys: 0, only in current: 1, new comments: 0' \
    '' \
    "Only in $current (review before removing):" \
    '' \
    '- # Legacy option' \
    '- LEGACY_TOKEN=<value hidden>' \
    '' > "$expected_check"
cmp "$expected_check" "$check_output"
if [ -s "$check_error" ]; then
    echo 'current-only check wrote an unexpected diagnostic' >&2
    exit 1
fi

./envdiff "$example" "$current" > "$filtered"
cmp "$current" "$filtered"

./envdiff -f "$example" "$current" > "$filtered"
cmp "$current" "$filtered"

./envdiff -r "$example" "$current" > "$filtered"
cmp "$expected" "$filtered"
if ! grep -F 'LEGACY_TOKEN=remove-this-secret' "$current" >/dev/null; then
    echo 'remove filter mode modified the current file' >&2
    exit 1
fi

./envdiff "$example" "$current" -ri
cmp "$expected" "$current"

if ./envdiff --check "$example" "$current" > "$check_output" 2> "$check_error"; then
    :
else
    status=$?
    echo "envdiff --check after remove merge returned unexpected status $status" >&2
    exit 1
fi
if [ -s "$check_output" ] || [ -s "$check_error" ]; then
    echo 'envdiff --check found changes after remove merge' >&2
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

if ./envdiff "$example" "$current" --check -i 2>/dev/null; then
    echo '--check accepted --in-place' >&2
    exit 1
else
    status=$?
    if [ "$status" -ne 2 ]; then
        echo "invalid in-place combination returned unexpected status $status" >&2
        exit 1
    fi
fi

if ./envdiff "$example" "$current" -i -o "$output" 2>/dev/null; then
    echo '--in-place accepted --output' >&2
    exit 1
else
    status=$?
    if [ "$status" -ne 2 ]; then
        echo "conflicting output modes returned unexpected status $status" >&2
        exit 1
    fi
fi

printf '%s\n' 'A=one' 'A=two' > "$example"
if ./envdiff "$example" "$current" 2>/dev/null; then
    echo 'duplicate key in example file was not rejected' >&2
    exit 1
fi

echo 'envdiff: tests passed'
