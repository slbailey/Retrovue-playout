_Metadata: Status=Planned; Scope=Milestone; Owner=@runtime-platform_

# Phase 2 - Decode and frame bus plan

## Purpose

Outline the goals for transforming the playout engine into a real-time decoder with shared buffering and telemetry ahead of Phase 3.

## Objectives

| Subsystem              | Goal                                           | Target outcome                                        |
| ---------------------- | ---------------------------------------------- | ----------------------------------------------------- |
| Decode pipeline        | Integrate `libavformat`/`libavcodec`           | Continuous frame decoding with metadata capture       |
| Frame bus              | Provide lock-free staging buffer               | 60-frame baseline depth, backpressure safety          |
| Telemetry              | Expose Prometheus metrics                      | Channel state, buffer depth, frame gap instrumentation|
| Integration testing    | Exercise Python ↔ C++ loop                     | Ready for contract suites and Renderer consumption    |

## Workstreams

### Decode pipeline

- Implement `FrameProducer` for media ingest and decode threads per channel.
- Populate `FrameMetadata` (PTS, DTS, duration, asset URI) for each output frame.
- Maintain 2-3 frame lead over Renderer without blocking control plane.

### Frame bus

- Enhance `FrameRingBuffer` with atomic indices and overflow/underflow reporting.
- Expose buffer depth metrics and warning logs when limits are breached.

### Telemetry

- Extend `MetricsExporter` to serve `/metrics`.
- Publish gauges and counters (`retrovue_playout_channel_state`, `retrovue_playout_buffer_depth_frames`, `retrovue_playout_frame_gap_seconds`, `retrovue_playout_decode_failure_count`).

### Integration testing

- Add `tests/test_decode.cpp` and `tests/test_buffer.cpp` for pipeline and buffer coverage.
- Provide `scripts/test_playout_loop.py` for end-to-end rehearsal with the Renderer.
- Define success criteria: continuous frame flow, stable metrics, no flaky tests.

## Risks and mitigations

- **Decode performance** - profile with representative media and adjust thread counts.
- **Telemetry accuracy** - validate metrics against contract rules and Renderer observations.
- **Buffer exhaustion** - implement backpressure and slate fallback hooks for Phase 3.

## See also

- [Phase 2 Goals (developer)](../developer/Phase2_Goals.md)
- [Playout Engine Contract](../contracts/PlayoutEngineContract.md)
- [Phase 1 - Bring-up complete](Phase1_Complete.md)

