# Contributing

Contributions are welcome through GitHub issues and pull requests.

## Development setup

The project targets Linux and requires a C11 compiler, GNU Make, and POSIX
command-line tools.

```bash
make clean check
```

The executable must remain at or below 102,400 bytes with the default build
flags. CI verifies the project with GCC and Clang.

## Behavioral contract

Changes must preserve these guarantees:

- existing assignment values are never parsed, compared, or replaced;
- only missing keys and new comments are added;
- duplicate keys are rejected;
- updates use an atomic replacement in the target file's directory;
- diagnostics never print assignment values.

Add or update a case in `test.sh` for every behavior change.

## Pull requests

Keep pull requests focused and describe:

- the problem being solved;
- any observable behavior change;
- how the change was tested;
- the resulting executable size.
