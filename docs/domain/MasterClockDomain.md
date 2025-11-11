_Metadata: Status=Draft; Scope=Domain; Owner=@runtime-platform_

_Related: [Architecture Overview](../architecture/ArchitectureOverview.md); [Playout Engine Contract](../contracts/PlayoutEngineContract.md)_

# Domain - MasterClock

## Purpose

Define the responsibilities, interfaces, and timing guarantees of the MasterClock service that coordinates playout timing across `retrovue-air`.

## Role in runtime

- Supplies authoritative wall-clock and monotonic time references to decoding and rendering components.
- Converts scheduled presentation timestamps (PTS) into UTC deadlines so producers and renderers stay aligned with broadcast schedules.
- Enforces drift bounds and exposes metrics that allow operators to detect timing degradation early.

## Interfaces

- `now_utc_us()` - Returns current UTC time in microseconds since Unix epoch; MUST be monotonic and synchronized with facility reference (e.g., NTP grandmaster).
- `now_monotonic_s()` - Returns elapsed monotonic time in seconds with microsecond precision; immune to wall-clock adjustments.
- `scheduled_to_utc_us(epoch_utc_us, pts_us, rate_ppm)` - Maps a scheduled PTS to UTC using the configured epoch and rate; MUST remain stable for the life of the schedule.
- `drift_ppm()` - Reports measured drift between the MasterClock and upstream reference in parts-per-million; consumers use it to tune correction loops.

## PTS <-> UTC mapping model

- Schedules declare an `epoch_utc_us` and nominal frame rate (in ppm) for each channel.
- MasterClock derives UTC deadlines as `deadline_utc_us = epoch_utc_us + pts_us * (1 + drift_ppm / 1_000_000)`.
- Corrections apply gradually: pace controller adjusts deadlines by ≤0.5 ms per frame to avoid jitter.
- Mapping remains deterministic while epoch and rate stay unchanged; drift corrections never rewrite historical PTS values.

## Integration points

- **Producer** - `FrameProducer` queries `scheduled_to_utc_us` to determine decode deadlines and to compute lead time targets.
- **Renderer** - `FrameRenderer` compares `now_monotonic_s` against scheduled deadlines to decide when to output frames and when to enter slate mode.
- **Telemetry** - `MetricsExporter` publishes `masterclock_drift_ppm`, `masterclock_jitter_ms_p95`, and related counters for operator dashboards.

## Timing guarantees

- `now_monotonic_s` MUST never decrease; jitter target is <1 ms p95 in contract tests.
- Pace controller reduces absolute frame gap over N successive frames when drift exists.
- Drift corrections apply at most 0.5 ms per frame, preventing visual artifacts.
- Gap exceeding 5 seconds forces the channel into error and initiates slate playback until recovery.

## Failure modes

- **Reference loss** - Upstream NTP/GPS failure increases `drift_ppm`; telemetry alerts operators while pace controller holds last known rate.
- **Underrun** - Producer runs out of frames; renderer enters buffering mode until `min_buffer_depth` restored.
- **Excessive drift** - If |drift| > configured threshold or frame gap > 5 s, MasterClock signals error state, raises metrics, and requests slate.

## See also

- [Metrics and Timing Domain](MetricsAndTimingDomain.md)
- [Playout Runtime](../runtime/PlayoutRuntime.md)
- [MasterClock contract](../contracts/MasterClockDomainContract.md)
