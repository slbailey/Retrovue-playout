_Metadata: Status=Stable; Scope=Developer guide_

_Related: [Build and Debug](BuildAndDebug.md); [Project Overview](../PROJECT_OVERVIEW.md)_

# Quick start guide

## Purpose

Provide the minimum steps to configure, build, and exercise the RetroVue Playout Engine on a local workstation.

## Scope

- Covers developer builds on Windows and Linux using CMake and vcpkg.
- Demonstrates the Python smoke-test client for verifying the gRPC control plane.
- Highlights next references for deeper architecture and runtime understanding.

## Prerequisites

- CMake 3.22 or newer.
- C++20 compiler (MSVC 2022, GCC 10+, or Clang 12+).
- vcpkg with the following packages: `grpc`, `protobuf`, `abseil`.
- Optional: FFmpeg development libraries (`libavformat`, `libavcodec`, `libavutil`, `libswscale`) for real decode mode.

## Configure and build

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config RelWithDebInfo
```

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config RelWithDebInfo
```

- Builds default to RelWithDebInfo for balanced optimization and debugging.
- Generated binaries live under `build/RelWithDebInfo/`.

## Run the playout service

```powershell
.\build\RelWithDebInfo\retrovue_playout.exe --port 50051
```

```bash
./build/RelWithDebInfo/retrovue_playout --port 50051
```

- Use `--port <value>` to override the default gRPC port.
- Additional arguments are documented in `retrovue_playout --help`.

## Smoke-test the gRPC API

```powershell
# Terminal 1
.\build\RelWithDebInfo\retrovue_playout.exe

# Terminal 2
python scripts\test_server.py
```

```bash
./build/RelWithDebInfo/retrovue_playout &
python scripts/test_server.py
```

The script exercises `GetVersion`, `StartChannel`, `UpdatePlan`, and `StopChannel`, and reports pass/fail for each RPC.

## What’s next

1. Review `docs/runtime/PlayoutRuntime.md` for threading and timing behavior.
2. Study `docs/architecture/ArchitectureOverview.md` for integration context.
3. Update the relevant contract doc before changing code (`docs/contracts/PlayoutEngineContract.md`).
4. Run targeted contract suites under `tests/contracts/` when adding or modifying behavior.

## See also

- [Build and Debug](BuildAndDebug.md)
- [Project Overview](../PROJECT_OVERVIEW.md)
- [Playout Runtime](../runtime/PlayoutRuntime.md)

