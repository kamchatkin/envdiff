# envdiff

[Russian version](README.ru.md)

`envdiff` updates a working `.env` file from an `.env.example` without comparing
or overwriting existing values.

It only:

- adds keys that do not exist in the working file;
- adds new comments from the example;
- preserves every existing assignment verbatim;
- writes changes atomically and preserves file permission bits.

The project is written in portable C11 for Linux. Its Makefile produces a
dynamically linked, size-optimized, stripped executable.

## Example

Example file:

```dotenv
# NATS endpoint
NATS_URL=tls://nats:4222

# CA used to verify the NATS server
NATS_TLS_CA_FILE=/etc/nats/certs/ca.crt
```

Working file before merging:

```dotenv
NATS_URL=nats://custom-host:4222
NATS_PASSWORD=local-secret
```

Working file after merging:

```dotenv
# NATS endpoint
NATS_URL=nats://custom-host:4222
NATS_PASSWORD=local-secret
# CA used to verify the NATS server
NATS_TLS_CA_FILE=/etc/nats/certs/ca.crt
```

Notice that the existing `NATS_URL` and `NATS_PASSWORD` assignments remain
untouched. `envdiff` does not parse or compare the text to the right of `=` for
existing keys.

## Build

Requirements:

- Linux;
- a C11 compiler such as GCC or Clang;
- GNU Make and standard POSIX command-line tools.

```bash
make
```

The Makefile prints the resulting size for information. Do not add `-static` if
keeping the executable small matters: a statically linked libc is much larger.

Install to `~/.local/bin`:

```bash
make install
```

Use another prefix if needed:

```bash
make install PREFIX=/usr/local
```

## Usage

```text
envdiff [--check] <example.env> <current.env>
```

Apply missing keys and comments:

```bash
envdiff .env.gateway.example .env.gateway
```

Check without writing:

```bash
envdiff --check .env.gateway.example .env.gateway
```

Show the version:

```bash
envdiff --version
```

Exit codes:

| Code | Meaning |
|---:|---|
| `0` | Merge succeeded, or `--check` found no changes |
| `1` | `--check` found missing keys or comments |
| `2` | Invalid arguments, malformed input, or an I/O error |

Duplicate keys in either file are treated as errors. Both `KEY=value` and
`export KEY=value` assignments are recognized. The output keeps the working
file's LF or CRLF line-ending style.

## Development

```bash
make check
```

CI builds and tests the project with both GCC and Clang. See
[CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## License

[MIT](LICENSE)
