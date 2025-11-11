_Metadata: Status=Draft; Scope=Contract; Owner=@runtime-platform_

_Related: [MasterClock Domain](../domain/MasterClockDomain.md); [Architecture Overview](../architecture/ArchitectureOverview.md)_

# Contract - MasterClock Domain

## Purpose

Define the enforceable rules and test expectations for the MasterClock service that coordinates timing across `retrovue-air`.

## MT_001: Monotonic now()

- `now_monotonic_s()` MUST never decrease between successive calls.
- Contract tests inject jitter to confirm p95 jitter < 1 ms across sample windows.

## MT_002: Stable PTS to UTC mapping

- Given fixed `epoch_utc_us` and `rate_ppm`, successive calls to `scheduled_to_utc_us()` with the same PTS MUST return the same UTC value within ±0.1 ms.
- Rate changes require explicit reconfiguration; historical mappings remain deterministic.

## MT_003: Pace controller convergence

- When absolute frame gap exceeds threshold, the pace controller MUST reduce |gap| across N consecutive frames.
- Per-frame correction magnitude MUST NOT exceed 0.5 ms to avoid oscillation.

## MT_004: Underrun recovery

- When buffer depth drops below minimum threshold, MasterClock signals buffering mode.
- Renderer resumes normal playback only after minimum depth is re-established.

## MT_005: Large gap handling

- If computed frame gap exceeds 5 s, MasterClock MUST raise error state, increment `masterclock_gap_errors_total`, and request slate playback until recovery.
- Recovery requires gap < configured recovery threshold for M consecutive frames.

## MT_006: Telemetry coverage

- Prometheus exporter MUST surface:
  - `masterclock_drift_ppm`
  - `masterclock_jitter_ms_p95`
  - `masterclock_corrections_total`
  - `masterclock_frame_gap_seconds`
- Metrics update at least once per scrape interval and reflect corrections applied during the interval.
