_Related: [Playout runtime](../runtime/PlayoutRuntime.md) • [Deployment integration](../infra/Integration.md)_

# Developer guide

## Purpose

Provide build, testing, and debugging guidance for engineers working on the RetroVue playout engine.

## Prerequisites

- CMake 3.24 or later.
- Visual Studio 2022 (Windows) or GCC/Clang with C++20 support (Linux).
- FFmpeg development headers (`libavcodec`, `libavformat`, `libswscale`).
- `protoc` and gRPC plugins aligned with the RetroVue core toolchain.

## Building

- Windows (PowerShell):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

- Linux (Bash):

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

- Generated binaries default to `build/retrovue_air` (or `build/RelWithDebInfo/retrovue_air` depending on build type).

## Tests

- Unit tests (GoogleTest) live under `tests/unit/`. Run with:

```bash
ctest --test-dir build
```

- Contract tests align with RetroVue runtime expectations and reside in the main RetroVue repository.
- Keep test fixtures in sync with `PLAYOUT_API_VERSION` whenever the gRPC schema changes.

## Protobuf generation

- Update `proto/retrovue/playout.proto` when the control API evolves.
- Regenerate bindings:

```bash
protoc --proto_path=proto --cpp_out=src --grpc_out=src --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` proto/retrovue/playout.proto
protoc --proto_path=proto --python_out=../Retrovue/src --grpc_python_out=../Retrovue/src proto/retrovue/playout.proto
```

- Commit generated code alongside schema updates to keep Python and C++ bindings aligned.

## Debugging

- Add `--log-level trace` to surface frame-level diagnostics.
- Use `ffprobe` on `/dev/shm/retrovue-air-<channel-id>` captures to validate timing metadata.
- Inspect Prometheus metrics for frame gap trends before diving into code-level profiling.
- On Windows, attach Visual Studio to the running process and enable GPU acceleration debugging if hardware decoders are involved.

## Performance tuning

- Profile decode threads with `perf` (Linux) or Windows Performance Analyzer to spot codec hotspots.
- Adjust per-codec thread pool sizes in configuration to match target hardware capabilities.
- Enable frame batching experiments behind feature flags; document outcomes in `docs/runtime/PlayoutRuntime.md`.

## Contribution checklist

- Update this guide and related docs when introducing new build flags, dependencies, or workflows.
- Ensure `PLAYOUT_API_VERSION` compatibility with RetroVue runtime contract tests.
- Run unit tests and relevant RetroVue contract tests before opening a pull request.

## See also

- [Playout runtime](../runtime/PlayoutRuntime.md)
- [Deployment integration](../infra/Integration.md)
- [RetroVue developer docs](../../Retrovue/docs/developer/architecture.md)

