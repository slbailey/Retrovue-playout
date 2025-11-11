_Metadata: Status=Planned; Scope=Milestone; Owner=@runtime-platform_

# Phase 3 - Real decode and renderer integration plan

## Purpose

Define the deliverables required to replace synthetic frames with FFmpeg-backed decoding and to stream frames through the Renderer with complete telemetry.

## Objectives

- Implement `FFmpegDecoder` and integrate it with `FrameProducer`.
- Deliver `FrameRenderer` interfaces for headless and preview consumption.
- Add `MetricsHTTPServer` exposing Prometheus metrics for decode and render performance.
- Synchronize decode, buffer, and render threads with deterministic timing.

## Deliverables

1. **Decode layer**
   - Implement `FFmpegDecoder` under `src/decode/` with support for H.264/HEVC inputs.
   - Push decoded frames into `FrameRingBuffer` with metadata intact.
   - Provide feature flag to fall back to stub decode when FFmpeg is unavailable.

2. **Renderer layer**
   - Define `FrameRenderer` interface and provide headless + preview implementations.
   - Drive render cadence from frame metadata (PTS/DTS) and monitor latency.

3. **Telemetry**
   - Introduce `MetricsHTTPServer` with `/metrics` endpoint (default port configurable).
  - Publish metrics: render FPS, frame delay, buffer health, decode failures.

4. **Integration**
   - Extend `PlayoutControlImpl` to orchestrate renderer lifecycle alongside producers.
   - Ensure clean start/stop semantics and resilience to decode failures.

## Validation strategy

- Unit tests covering FFmpeg initialization, frame decode, and renderer fetch behavior.
- Integration rehearsal `scripts/test_playout_loop.py` verifying end-to-end flow.
- Benchmark decoding latency and buffer depth stability under sustained playback.

## Risks and mitigations

- **Codec availability** - rely on vcpkg FFmpeg builds and document prerequisites.
- **Timing drift** - add telemetry for frame gaps and enforce MasterClock alignment.
- **Resource contention** - profile thread usage and adjust concurrency per codec.

## See also

- [Phase 2 - Decode and frame bus complete](Phase2_Complete.md)
- [Renderer Domain](../domain/RendererDomain.md)
- [Playout Runtime](../runtime/PlayoutRuntime.md)

