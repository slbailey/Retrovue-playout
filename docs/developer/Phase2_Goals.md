_Metadata: Status=Complete; Scope=Milestone planning; Owner=@runtime-platform_

_Related: [Architecture Overview](../architecture/ArchitectureOverview.md); [Playout Engine Contract](../contracts/PlayoutEngineContract.md); [Phase 1 Skeleton](../milestones/Phase1_Skeleton.md)_

# Phase 2 - Decode and frame bus integration

## Purpose

Capture the goals and deliverables for Phase 2, which elevates the playout engine from a stub RPC service to a production-ready decoder with real buffering and telemetry.

## Objectives

| Subsystem              | Goal                                           | Outcome                                               |
| ---------------------- | ---------------------------------------------- | ----------------------------------------------------- |
| Decode pipeline        | Leverage `libavformat`/`libavcodec`            | Real frame decoding from file or URI sources          |
| Frame bus / ring buffer| Provide shared-memory frame queue              | Thread-safe producer/consumer bridge to Renderer      |
| Telemetry / metrics    | Expose Prometheus `/metrics` endpoint          | Channel state and timing visibility                   |
| Integration testing    | Validate Python ↔ C++ plumbing                 | End-to-end confidence entering Phase 3                |

## Subsystem details

### Decode pipeline

- Key files: `src/decode/FrameProducer.h`, `src/decode/FrameProducer.cpp`.
- Responsibilities:
  - Open media inputs via `avformat_open_input`.
  - Select optimal stream, initialize codecs, and decode frames.
  - Populate `FrameMetadata` (PTS, DTS, duration, asset URI) for each frame.
- Thread model:
  - One decode thread per channel.
  - Maintain a 2-3 frame lead ahead of Renderer consumption.
  - Avoid blocking gRPC or telemetry threads.

### Frame bus and ring buffer

- Key files: `src/buffer/FrameRingBuffer.h`, `src/buffer/FrameRingBuffer.cpp`.
- Design:
  - Fixed-size circular buffer (baseline: 60 frames).
  - Atomic read/write indices and non-blocking `push`/`pop`.
  - Future option for condition variables to smooth underflow recovery.
- Telemetry:
  - `retrovue_playout_buffer_depth_frames` exposes current buffer depth.
  - Overflow/underflow raise Prometheus counters and warning logs.

### Telemetry

- Key file: `src/telemetry/MetricsExporter.cpp`.
- Responsibilities:
  - Serve metrics at `/metrics`.
  - Export gauges and counters:

    | Metric                                  | Type    | Description                                      |
    | --------------------------------------- | ------- | ------------------------------------------------ |
    | `retrovue_playout_channel_state`        | Gauge   | Channel state (`ready`, `buffering`, `error`)    |
    | `retrovue_playout_buffer_depth_frames`  | Gauge   | Frames in buffer                                 |
    | `retrovue_playout_frame_gap_seconds`    | Gauge   | MasterClock delta                                |
    | `retrovue_playout_decode_failure_count` | Counter | Accumulated decode failures                      |

### Integration testing

- Assets:
- `tests/test_decode.cpp` - decode pipeline coverage.
- `tests/test_buffer.cpp` - ring buffer stress scenarios.
- `scripts/test_playout_loop.py` - Python client rehearsal.
- Success indicators:
  - `StartChannel` spins up threads and buffers frames deterministically.
  - Frames flow continuously from decoder to Renderer.
  - Metrics remain accurate under steady load.
  - Contract suites pass without flakiness.

## Follow-ups

- Add structured logs for frame timing and buffer state.
- Expose buffer sizing and thread counts as runtime configuration.
- Extend metrics with `help`/`type` metadata and slate injection coverage.
