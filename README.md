# Tree

[![Cpp Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Tests status](https://img.shields.io/github/actions/workflow/status/TreeLanguage/Tree/ci.yml?branch=main&label=test)](https://github.com/TreeLanguage/Tree/actions/workflows/ci.yml)

## Building from source

### Prerequisites

- A C++ compiler with C++20 (or later) support
- CMake (>= 3.16)
- Make (or another CMake-supported generator)

### Build

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Install

```sh
cmake --install build
```

You can customize the install location with `--prefix`:

```sh
cmake --install build --prefix /usr/local
```

### For developers

If you're working on Tree itself, build with debug symbols and no optimizations:

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

Run the test suite with:

```sh
ctest --test-dir build --output-on-failure -C Debug
```
