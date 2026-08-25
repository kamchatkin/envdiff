# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

### Added

- Native Windows and macOS support.
- CMake builds and cross-platform integration tests.
- CI artifacts for Linux, macOS, and Windows.
- Automated multi-platform GitHub releases for version tags.

### Changed

- The command now behaves as a Unix filter and writes merged content to standard
  output by default.
- Added `-o` and `--output` for explicit atomic file output.
- Successful filtering and file output no longer emit status messages.
- File reading no longer depends on the POSIX `getline` function.
- Atomic replacement now uses the native Windows `ReplaceFile` API on Windows.

## 0.1.0 - 2026-08-25

### Added

- Merge missing `.env` keys from an example file.
- Add new comments without duplicating existing comments.
- Preserve all existing assignment values.
- Atomic writes with permission-bit preservation.
- `--check`, `--help`, and `--version` modes.
- LF and CRLF support.
- Duplicate-key validation.
- Size-optimized build flags and informational size output.
