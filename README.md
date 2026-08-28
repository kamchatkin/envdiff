# envdiff

[Russian version](README.ru.md)

`envdiff` merges an `.env.example` with a working `.env` without comparing or
overwriting existing values. It behaves as a Unix filter: the merged content is
written to standard output unless an output file is explicitly selected.

By default, it only:

- adds keys that do not exist in the working file;
- adds new comments from the example;
- preserves every existing assignment verbatim;
- writes only merged content to standard output;
- atomically creates or replaces files selected with `-o` and preserves existing
  file permissions.

The project is written in portable C11 and supports Linux, macOS, and Windows.
Release builds are optimized for size.

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

Merged output:

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

- CMake 3.20 or newer;
- a C11 compiler: GCC or Clang on Linux, Apple Clang on macOS, or MSVC on
  Windows.

Linux and macOS:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows with Visual Studio Build Tools, from PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The Windows executable is created at `build/Release/envdiff.exe`. With a
single-configuration Unix generator, the executable is `build/envdiff`.

On Linux and macOS, Make remains available as a convenience:

```bash
make
make check
```

The Makefile prints the resulting size for information. Do not add `-static` if
keeping the executable small matters: a statically linked libc is much larger.

Install the CMake build using its configured prefix:

```bash
cmake --install build
```

Alternatively, install the Make build to `~/.local/bin`:

```bash
make install
```

Use another prefix if needed:

```bash
make install PREFIX=/usr/local
```

## Usage

```text
envdiff [-c] [-f] [-r] [-i | -o FILE] <example.env> <current.env>
```

The two positional arguments always have the same order:

1. `<example.env>` is the template containing the expected keys and comments;
2. `<current.env>` is the working file whose existing values are preserved.

As a safety check, `envdiff` refuses to use a file whose base name contains
`example` (case-insensitively) as `<current.env>`. This usually means that the
positional arguments were reversed or that two example files were supplied.
Directory names are ignored by this check. Use `-f` or `--force` to override it
when such a current file name is intentional:

```bash
envdiff -f template.env current.example.env
```

Use `-r` or `--remove` to make the example's key set authoritative during a
merge. Keys found only in the current file, and their associated comments, are
removed from the result. For example, atomically remove obsolete keys while
preserving the values of all keys still present in the example:

```bash
envdiff .env.gateway.example .env.gateway -ri
```

Short options that do not take an argument can be combined, so `-ri` is
equivalent to `-r -i`. The `-o FILE` option must be written separately.

Without `-r`, current-only keys are always preserved. In filter mode, `-r`
changes only the generated output; an input file is modified only when `-i` or a
matching `-o FILE` is selected. With `--check`, `-r` does not remove anything.

`FILE` following `-o` or `--output` is an option argument: it is the output
destination, not one of the two positional arguments. Options may appear before
or after the positional arguments. The examples below put the output option last
to keep the flow visually consistent: `example -> current -> output`.

Print the merged environment to standard output without modifying either input
file:

```bash
envdiff .env.gateway.example .env.gateway
```

Redirect the output into a different file:

```bash
envdiff .env.gateway.example .env.gateway > .env.gateway.merged
```

Atomically update the working file:

```bash
envdiff .env.gateway.example .env.gateway -i
```

`-i`/`--in-place` is shorthand for selecting the current file as the output
destination. `envdiff` reads it completely before replacing it atomically. The
equivalent explicit command is:

```bash
envdiff .env.gateway.example .env.gateway -o .env.gateway
```

Write to another file without shell redirection:

```bash
envdiff .env.gateway.example .env.gateway --output .env.gateway.merged
```

Show a structural difference without modifying either file:

```bash
envdiff .env.gateway.example .env.gateway -c
```

`-c` is the short form of `--check`.

The report uses diff-like markers:

- `+` is a missing key or comment from the example;
- `-` is a key that exists only in the current file and should be reviewed
  before removal (or removed by a merge with `-r`); its value is replaced with
  `<value hidden>`;
- an unprefixed key is context for a missing comment; its value is not compared.

Complete example assignments are printed for `+` keys so they can be applied
manually. Current-only values are hidden because the key name is sufficient for
manual removal. Values of keys present in both files are never compared.

```text
Missing from .env:

+ # Request timeout
+ REQUEST_TIMEOUT=30

Only in .env (review before removing):

- # Legacy option
- LEGACY_TOKEN=<value hidden>
```

> **Warning:** `--check` prints values from the example file and comments from
> both files. Current-only assignment values are redacted, but comments are not.
> Do not store secrets in comments or `.env.example` files, and do not expose
> such output in public CI logs.

Do not redirect standard output back to an input file:

```bash
# Wrong: the shell truncates .env.gateway before envdiff reads it.
envdiff .env.gateway.example .env.gateway > .env.gateway
```

Use `-i` (or `-o .env.gateway`) for an atomic in-place update instead.

Show the version:

```bash
envdiff --version
```

Exit codes:

| Code | Meaning |
|---:|---|
| `0` | Output succeeded, or `--check` found no changes |
| `1` | `--check` found missing keys/comments or keys only in the current file |
| `2` | Invalid arguments, malformed input, or an I/O error |

Successful filtering and `-i`/`-o` writes are quiet. The `--check` report is
written to standard output; errors are written to standard error. Duplicate keys in
either file are treated as errors. Both `KEY=value` and `export KEY=value`
assignments are recognized. The output keeps the working file's LF or CRLF
line-ending style.

## Automated builds

CI compiles and tests the project with GCC and Clang on Linux, Apple Clang on
macOS, and MSVC on Windows. Every CI run uploads packaged executables as workflow
artifacts.

Pushing a version tag such as `v0.5.0` builds all three platform archives and
creates or updates the corresponding GitHub release.

## Development

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## License

[MIT](LICENSE)
