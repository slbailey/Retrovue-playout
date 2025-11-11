# RetroVue Playout Engine

_Metadata: Status=Stable; Audience=Runtime engineers and platform operators_

## Purpose

Provide a native C++ playout engine that converts ChannelManager plans into frame-accurate video streams. The service pairs with RetroVue Core and Renderer to deliver continuous broadcast output with deterministic timing and observability.

## Scope

- Decode media assets using libavformat/libavcodec.
- Stage frames in a lock-free buffer for Renderer consumption.
- Expose telemetry (Prometheus metrics, structured logs) for operator visibility.
- Manage channel lifecycle through the gRPC `PlayoutControl` API.

**Out of scope**

- Scheduling logic (owned by RetroVue Core).
- Long-term asset storage or transcoding pipelines.
- User-facing dashboards.

## Status

| Phase  | State     | Outcome                                               |
| ------ | --------- | ----------------------------------------------------- |
| Phase 1 | Complete  | gRPC skeleton and contract-first bring-up             |
| Phase 2 | Complete  | Frame buffer, stub decode, telemetry scaffolding      |
| Phase 3 | Complete  | FFmpeg decoder, Renderer integration, HTTP metrics    |
| Phase 4 | Planned   | Production hardening, multi-channel, MasterClock sync |

## Architecture overview

- Control plane: `PlayoutControl` gRPC service defined in `proto/retrovue/playout.proto`.
- Decode pipeline: per-channel threads backed by `FFmpegDecoder` and `FrameProducer`.
- Buffering: `FrameRingBuffer` provides lock-free staging with deterministic depth targets.
- Rendering: Renderer pulls frames over shared memory or TCP and emits MPEG-TS streams.
- Telemetry: `MetricsExporter` exposes Prometheus metrics at `:9308/metrics`.

Refer to `docs/architecture/ArchitectureOverview.md` for contextual diagrams and detailed flow.

## Quick start

### Prerequisites

- CMake 3.15 or newer.
- C++20 compiler (MSVC 2019+, GCC 10+, or Clang 11+).
- vcpkg with `grpc`, `protobuf`, `abseil`. Install FFmpeg libraries for real decode scenarios.

### Configure and build

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config RelWithDebInfo
```

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config RelWithDebInfo
```

### Run and smoke-test

```powershell
.\build\RelWithDebInfo\retrovue_playout.exe --port 50051
python scripts\test_server.py
```

```bash
./build/RelWithDebInfo/retrovue_playout --port 50051
python scripts/test_server.py
```

Expected output includes passing RPC checks for `GetVersion`, `StartChannel`, `UpdatePlan`, and `StopChannel`.

## Components

| Module              | Path                    | Description                                       |
| ------------------- | ----------------------- | ------------------------------------------------- |
| gRPC service        | `src/playout_service.*` | Channel lifecycle orchestration                   |
| Frame buffer        | `src/buffer/`           | Lock-free circular buffer (default 60 frames)     |
| Decode pipeline     | `src/decode/`           | FFmpeg-backed decode threads and helpers          |
| Renderer integration| `src/renderer/`         | Preview and headless frame consumers              |
| Telemetry           | `src/telemetry/`        | Metrics exporter and HTTP server                  |
| Proto definitions   | `proto/retrovue/`       | Contract for control plane interactions           |

Public headers live under `include/retrovue/<module>/`, mirroring namespaces as documented in `docs/developer/DevelopmentStandards.md`.

## Testing

- Contract and unit tests live under `tests/`; build via CMake targets (`test_buffer`, `test_decode`, contract suites).
- Run unit tests:

```powershell
ctest --test-dir build --output-on-failure
```

```bash
ctest --test-dir build --output-on-failure
```

- Integration scripts (`scripts/test_server.py`) verify gRPC control flow against a running engine.

## Documentation

- `docs/README.md` - complete documentation index organized by audience.
- `docs/domain/` - Domain contracts and invariants.
- `docs/contracts/` - Behavioral guarantees, rule IDs, and test requirements.
- `docs/developer/` - Build, debug, and quick start guides.
- `docs/milestones/` - Historical milestone summaries and roadmap context.
- `docs/GLOSSARY.md` - Canonical terminology list.

## Contributing

- Follow `_standards/documentation-standards.md` and `_standards/repository-conventions.md`.
- Update relevant documentation before code changes.
- Run contract and unit tests prior to submitting pull requests.
- See `CONTRIBUTING.md` for workflow details.

## License

RetroVue Playout Engine is released under the MIT License. See `LICENSE` for full terms.

## See also

- `docs/architecture/ArchitectureOverview.md` - Architectural context.
- `docs/runtime/PlayoutRuntime.md` - Execution and threading model.
- RetroVue Core - Scheduling and channel orchestration counterpart.
