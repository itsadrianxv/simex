# simex

simex is short for SIMulated EXchange. It's a C++ trading exchange-side program with an SHFE-style RB futures profile.

## Quick Start

### Prerequisites

- A C++20 compiler (GCC or Clang)
- CMake 3.25+ and Ninja
- OpenSSL (Crypto)
- nlohmann_json, or opt into the pinned source fallback with `SIMEX_FETCH_NLOHMANN_JSON=ON`

### Building with CMake

For reproducible Ninja builds, use the checked-in CMake presets:

```sh
cmake --preset ci-gcc
cmake --build --preset ci-gcc
ctest --preset test-ci-gcc
```

The `dev-gcc`, `dev-clang`, `ci-gcc`, `ci-clang`, and `sanitizer` presets provide the corresponding local, CI, and sanitizer configurations.

Without presets:

```sh
cmake -S . -B build -DSIMEX_FETCH_NLOHMANN_JSON=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the deterministic demo with:

```sh
./build/ci-gcc/simex_demo simex.json
```

## Note

For the standalone localhost TCP/UDP service used by Jev, see [the transport service guide](docs/jev-transport-service.md). The service starts with an empty book; market-data and counterparty generation are separate work.

I am very, very early in this project. Expect bugs.

## License

MIT
