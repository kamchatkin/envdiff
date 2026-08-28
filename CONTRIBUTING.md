# Contributing

Contributions are welcome through GitHub issues and pull requests.

## Development setup

The project targets Linux, macOS, and Windows. Development requires CMake 3.20
or newer and a C11 compiler.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Linux and macOS contributors can also run `make clean check`. CI verifies GCC
and Clang on Linux, Apple Clang on macOS, and MSVC on Windows.

## Behavioral contract

Changes must preserve these guarantees:

- existing assignment values are never parsed, compared, or replaced;
- only missing keys and new comments are added unless `--remove` is selected;
- `--remove` deletes only current-only keys and their associated comments;
- duplicate keys are rejected;
- default filtering writes merged content only to standard output;
- `-o` updates use an atomic replacement in the target file's directory;
- values of keys present in both files are never compared;
- ordinary error diagnostics never print assignment values;
- `--check` prints complete assignments only for missing example keys and
  redacts values of keys that exist only in the current file.

Add or update a cross-platform case in `tests/integration.cmake` for every
behavior change. Add a case to `test.sh` as well when the behavior is specific
to the Unix test suite.

## Pull requests

Keep pull requests focused and describe:

- the problem being solved;
- any observable behavior change;
- how the change was tested;
