_Related: [Metrics & Timing Domain](../domain/MetricsAndTimingDomain.md) • [Architecture Overview](../architecture/ArchitectureOverview.md) • [Testing Standards](../developer/DevelopmentStandards.md)_

# Contract — Metrics and Timing Domain Testing

Status: Enforced

## Purpose

This contract specifies the **acceptance tests** that validate the Metrics & Timing Domain invariants. All tests in this contract must pass before the playout engine can be considered production-ready.

This document translates the 8 core invariants from `MetricsAndTimingDomain.md` into concrete, automated test specifications.

---

## Domain Invariants (Quick Reference)

The following 8 invariants from the [Metrics & Timing Domain](../domain/MetricsAndTimingDomain.md) are validated by this contract:

1. **MasterClock Authority**: MasterClock is the single authoritative time source for all scheduled playout activity
2. **Component Time Alignment**: All components align timing to MasterClock timestamps, not system wall clock
3. **Frame Cadence**: Nominal 29.97 fps with ±2 ms drift tolerance per frame
4. **End-to-End Latency**: < 33 ms average, < 50 ms sustained maximum
5. **Prometheus Metrics**: Six required metrics must be exported (`frame_decode_time_ms`, `frame_render_time_ms`, `frames_dropped_total`, `frames_skipped_total`, `clock_offset_ms`, `uptime_seconds`)
6. **Metrics Sampling**: 1-second sampling frequency for cumulative and instantaneous statistics
7. **Timing Anomalies**: All anomalies must be logged and surfaced via `/metrics` endpoint
8. **Forward Compatibility**: Phase 4 MasterClock integration must not require breaking changes

Each invariant is validated by one or more contract tests below.

---

## Test Structure

Each test suite follows this pattern:

```cpp
TEST(MetricsAndTimingContract, TestName) {
    // 1. Setup: Configure test environment
    // 2. Execute: Run playout operation
    // 3. Assert: Validate invariant holds
    // 4. Cleanup: Teardown resources
}
```

All tests must be:
- **Deterministic**: Same inputs produce same outputs
- **Isolated**: No dependencies on external state
- **Fast**: Complete within 5 seconds (except load tests)
- **Automated**: Run via `ctest` or equivalent

---

## Invariant #1: MasterClock Authority

**Statement**: The MasterClock service is the single authoritative time source for all scheduled playout activity.

### Test 1.1: No Direct System Clock Calls

**Objective**: Verify that decoder, buffer, and renderer components never call system time functions directly.

**Procedure**:
1. Build playout engine with `-Werror` and custom compile flag `-DNO_SYSTEM_CLOCK`
2. Provide mock MasterClock implementation
3. Start channel and decode 100 frames
4. Verify build succeeds (no system clock calls)

**Pass Criteria**: Build completes without errors; runtime does not call `std::chrono::system_clock::now()`, `gettimeofday()`, or `clock_gettime()`.

**Implementation Note**: Use link-time interposition or compile-time macros to detect forbidden calls.

---

### Test 1.2: MasterClock Injection

**Objective**: Verify that all timing-sensitive components receive MasterClock via dependency injection.

**Procedure**:
1. Instantiate `FFmpegDecoder`, `FrameRingBuffer`, `FrameRenderer`
2. Verify constructors accept `MasterClock*` or `std::shared_ptr<MasterClock>`
3. Assert that calling timing methods without MasterClock results in compile error

**Pass Criteria**: All components require MasterClock in constructor; no default system clock fallback.

---

### Test 1.3: MockMasterClock Determinism

**Objective**: Verify that tests using MockMasterClock produce identical results across runs.

**Procedure**:
1. Create MockMasterClock with fixed start time: `1672531200000000` (2023-01-01 00:00:00 UTC)
2. Decode known test asset (300 frames @ 29.97 fps)
3. Capture PTS values for all frames
4. Repeat test 10 times
5. Assert all runs produce identical PTS sequences

**Pass Criteria**: Zero variance in PTS values across runs.

---

## Invariant #2: Component Time Alignment

**Statement**: FFmpegDecoder, FrameRingBuffer, and FrameRenderer must align their timing to MasterClock timestamps, not system wall clock.

### Test 2.1: Decoder Uses MasterClock for Timestamps

**Objective**: Verify decoder queries MasterClock for decode timestamps.

**Procedure**:
1. Create MockMasterClock returning fixed time
2. Decode single frame
3. Verify decoder calls `clock->now_utc_us()` at least once
4. Assert frame metadata DTS correlates with MasterClock time

**Pass Criteria**: Decoder invokes MasterClock; DTS is not derived from system clock.

---

### Test 2.2: Renderer Synchronizes to MasterClock

**Objective**: Verify renderer pulls frames based on MasterClock, not wall clock.

**Procedure**:
1. Create MockMasterClock
2. Push 5 frames to buffer with PTS: [1000ms, 1033ms, 1067ms, 1100ms, 1133ms]
3. Advance MockMasterClock to 1000ms
4. Call `renderer.pull_frame()` → should succeed (PTS = 1000ms)
5. Advance MockMasterClock to 1030ms
6. Call `renderer.pull_frame()` → should fail (next frame PTS = 1033ms, too early)
7. Advance MockMasterClock to 1033ms
8. Call `renderer.pull_frame()` → should succeed (PTS = 1033ms)

**Pass Criteria**: Renderer respects MasterClock timestamps; frames not pulled early.

---

### Test 2.3: Buffer Timing Pass-Through

**Objective**: Verify buffer does not modify frame timestamps.

**Procedure**:
1. Create FrameRingBuffer
2. Push frame with PTS = 5000 µs
3. Pop frame
4. Assert output frame PTS == 5000 µs (unchanged)

**Pass Criteria**: Buffer preserves timing metadata exactly.

---

## Invariant #3: Frame Cadence (29.97 fps, ±2 ms tolerance)

**Statement**: The nominal frame cadence is 29.97 fps unless overridden by source time base; tolerance for drift is ±2 ms per frame.

### Test 3.1: Nominal Cadence Enforcement

**Objective**: Verify frames are rendered at 29.97 fps (33.37 ms intervals) when source specifies this rate.

**Procedure**:
1. Decode test asset with 29.97 fps time base
2. Render 900 frames (30 seconds)
3. Measure inter-frame intervals
4. Calculate mean and standard deviation

**Pass Criteria**:
- Mean interval: 33.37 ± 0.1 ms
- 95% of frames: interval within 33.37 ± 2 ms
- No frame interval > 35.37 ms or < 31.37 ms

---

### Test 3.2: Source Time Base Override

**Objective**: Verify playout respects non-standard time bases (e.g., 24 fps, 60 fps).

**Procedure**:
1. Decode test assets at: 24 fps (41.67 ms), 30 fps (33.33 ms), 60 fps (16.67 ms)
2. For each asset, render 100 frames
3. Measure actual frame intervals
4. Assert intervals match source time base ± 2 ms

**Pass Criteria**: Each asset renders at correct cadence with ± 2 ms tolerance.

---

### Test 3.3: Clock Offset Metric Accuracy

**Objective**: Verify `clock_offset_ms` metric reflects actual frame timing drift.

**Procedure**:
1. Create MockMasterClock
2. Decode frame with PTS = 1000 ms
3. Advance MockMasterClock to 1002 ms (2 ms late)
4. Render frame
5. Query `clock_offset_ms` metric
6. Assert metric value == -2 ms (negative = late)

**Pass Criteria**: `clock_offset_ms` matches calculated offset within 0.1 ms.

---

### Test 3.4: MasterClock Offset Propagation

**Objective**: Verify that the offset reported in Prometheus metrics matches the observed delta between MasterClock time and frame PTS.

**Procedure**:
1. Create MockMasterClock initialized to T=0
2. Push frame to buffer with PTS = 100 ms
3. Advance MockMasterClock to 95 ms (frame is 5 ms early)
4. Attempt to render frame (should fail - too early)
5. Query `/metrics` → `clock_offset_ms{channel="1"}` 
6. Assert metric == +5 ms (positive = frame ahead of clock)
7. Advance MockMasterClock to 100 ms
8. Render frame (should succeed)
9. Query `/metrics` → `clock_offset_ms{channel="1"}`
10. Assert metric ≈ 0 ms (frame on time)
11. Push frame with PTS = 133 ms
12. Advance MockMasterClock to 140 ms (frame is 7 ms late)
13. Render frame
14. Query `/metrics` → `clock_offset_ms{channel="1"}`
15. Assert metric == -7 ms (negative = frame behind clock)

**Pass Criteria**: 
- Offset metric matches calculated `(frame.pts - clock.now_utc_us())` within ±0.1 ms at each step
- Positive offset when frame early, negative when late, ~zero when on time
- Metric updates reflect MasterClock state changes within 1 second

**Implementation Note**: This test validates the complete propagation path from MasterClock → renderer timing logic → metrics exporter → HTTP endpoint.

---

## Invariant #4: End-to-End Latency (< 33 ms avg, < 50 ms sustained)

**Statement**: Latency between decode and render must remain < 33 ms on average and never exceed 50 ms sustained.

### Test 4.1: Average Latency Under Load

**Objective**: Verify average decode-to-render latency stays below 33 ms during normal operation.

**Procedure**:
1. Start 2 channels with 1080p30 H.264 content
2. Decode and render continuously for 60 seconds
3. Record timestamp pairs: `(decode_complete_ts, render_start_ts)`
4. Calculate average latency: `mean(render_start_ts - decode_complete_ts)`

**Pass Criteria**: Average latency < 33 ms across entire run.

---

### Test 4.2: Sustained Latency Peak

**Objective**: Verify no 5-second window exceeds 50 ms maximum latency.

**Procedure**:
1. Start channel with 1080p30 content
2. Decode and render for 120 seconds
3. Calculate max latency in each 5-second window (24 windows total)
4. Assert all windows: max latency < 50 ms

**Pass Criteria**: All 5-second windows have max latency < 50 ms.

---

### Test 4.3: Latency Under Contention

**Objective**: Verify latency bounds hold under CPU contention (multi-channel scenario).

**Procedure**:
1. Start 4 channels simultaneously (1080p30 content)
2. Run for 60 seconds
3. Measure per-channel latencies
4. Assert each channel meets invariant #4 independently

**Pass Criteria**: All 4 channels maintain < 33 ms average and < 50 ms sustained.

---

### Test 4.4: Latency Violation Logging

**Objective**: Verify system logs WARNING when average latency exceeds 33 ms.

**Procedure**:
1. Inject artificial delay in decode path (40 ms per frame)
2. Decode 100 frames
3. Search logs for "Average latency violated"
4. Assert warning appears within 60 seconds

**Pass Criteria**: Warning logged; `clock_drift_warning_total` incremented.

---

## Invariant #5: Prometheus Metrics (Required Metrics)

**Statement**: MetricsHTTPServer must export the 6 required metrics in Prometheus format.

### Test 5.1: Required Metrics Presence

**Objective**: Verify all 6 required metrics are exported by `/metrics` endpoint.

**Procedure**:
1. Start playout engine with single channel
2. Decode 10 frames
3. HTTP GET `http://localhost:9090/metrics`
4. Parse Prometheus text format
5. Assert presence of:
   - `frame_decode_time_ms{channel="1"}`
   - `frame_render_time_ms{channel="1"}`
   - `frames_dropped_total{channel="1"}`
   - `frames_skipped_total{channel="1"}`
   - `clock_offset_ms{channel="1"}`
   - `uptime_seconds{channel="1"}`

**Pass Criteria**: All 6 metrics present in response; no 404 or 500 errors.

---

### Test 5.2: Metric Value Correctness

**Objective**: Verify metric values accurately reflect system state.

**Procedure**:
1. Start channel, decode 300 frames (no drops, no skips)
2. Query `/metrics`
3. Assert:
   - `frames_decoded_total{channel="1"}` == 300
   - `frames_rendered_total{channel="1"}` == 300
   - `frames_dropped_total{channel="1"}` == 0
   - `frames_skipped_total{channel="1"}` == 0
   - `uptime_seconds{channel="1"}` ≈ 10 ± 0.5 (300 frames @ 29.97 fps)

**Pass Criteria**: All counter values match expected exactly; gauges within tolerance.

---

### Test 5.3: Metric Types Correct

**Objective**: Verify metrics use correct Prometheus types (Gauge vs Counter).

**Procedure**:
1. Query `/metrics`
2. Parse `# TYPE` declarations
3. Assert:
   - `frame_decode_time_ms` → `gauge`
   - `frame_render_time_ms` → `gauge`
   - `frames_dropped_total` → `counter`
   - `frames_skipped_total` → `counter`
   - `clock_offset_ms` → `gauge`
   - `uptime_seconds` → `gauge`

**Pass Criteria**: All metrics have correct types per Prometheus specification.

---

## Invariant #6: Metrics Sampling (Every 1 Second)

**Statement**: Metrics sampling must occur every 1 second and reflect cumulative and instantaneous statistics.

### Test 6.1: Sampling Frequency

**Objective**: Verify metrics update at 1 Hz (every 1 second).

**Procedure**:
1. Start channel and decode continuously
2. Query `/metrics` at T=0s, T=1s, T=2s, T=3s
3. For each query, record `frames_rendered_total`
4. Calculate deltas: Δ1 = (T1 - T0), Δ2 = (T2 - T1), Δ3 = (T3 - T2)
5. Assert frame count increases at expected rate (≈30 frames/sec @ 29.97 fps)

**Pass Criteria**: Metrics reflect changes within 1 second of occurrence; no stale values.

---

### Test 6.2: Instantaneous vs Cumulative

**Objective**: Verify gauges show instantaneous values, counters show cumulative.

**Procedure**:
1. Decode 10 frames rapidly (< 1 second)
2. Query `/metrics` at T=0.5s
3. Assert:
   - `frames_rendered_total` (counter) shows cumulative: 10
   - `frame_decode_time_ms` (gauge) shows most recent decode time
4. Decode 10 more frames
5. Query at T=1.5s
6. Assert:
   - `frames_rendered_total` now shows: 20 (cumulative)
   - `frame_decode_time_ms` shows most recent (not average)

**Pass Criteria**: Counters accumulate; gauges show current/last value.

---

## Invariant #7: Timing Anomalies (Logged and Surfaced)

**Statement**: Timing anomalies must be logged and surfaced via `/metrics` endpoint.

### Test 7.1: Frame Late Detection

**Objective**: Verify late frames are logged and counted.

**Procedure**:
1. Create MockMasterClock
2. Push frame with PTS = 1000 ms
3. Advance MockMasterClock to 1060 ms (60 ms late)
4. Render frame
5. Assert:
   - Log contains: "Frame late by 60ms on channel 1"
   - `frames_late_total{channel="1"}` incremented

**Pass Criteria**: Anomaly logged at WARNING level; counter updated.

---

### Test 7.2: Frame Dropped Detection

**Objective**: Verify dropped frames are logged and counted.

**Procedure**:
1. Create buffer with capacity = 10 frames
2. Pause renderer (stop pulling)
3. Push 15 frames (force overflow)
4. Assert:
   - Logs contain: "Buffer full, dropped frame PTS=..."
   - `frames_dropped_total{channel="1"}` == 5

**Pass Criteria**: All 5 drops logged; counter accurate.

---

### Test 7.3: Clock Drift Warning

**Objective**: Verify clock drift triggers warning when threshold exceeded.

**Procedure**:
1. Create MockMasterClock advancing at 1.05x speed (5% fast)
2. Decode 1000 frames
3. Assert:
   - `clock_drift_warning_total{channel="1"}` > 0
   - Log contains: "Clock drift detected: ..."

**Pass Criteria**: Drift detected within 10 seconds; warning logged.

---

### Test 7.4: Buffer Underrun Recovery

**Objective**: Verify buffer underrun is logged and state transitions correctly.

**Procedure**:
1. Start channel with small buffer (10 frames)
2. Pause producer for 500 ms (force underrun)
3. Assert during underrun:
   - `buffer_underrun_total{channel="1"}` incremented
   - `channel_state{channel="1"}` == 1 (buffering)
   - Log contains: "Buffer underrun on channel 1"
4. Resume producer
5. Wait for buffer refill
6. Assert after recovery:
   - `channel_state{channel="1"}` == 2 (ready)

**Pass Criteria**: State transitions captured; metrics and logs reflect underrun event.

---

## Invariant #8: Forward Compatibility (Phase 4 MasterClock Integration)

**Statement**: This contract anticipates Phase 4 MasterClock formalization and must remain forward-compatible.

### Test 8.1: MasterClock Interface Stability

**Objective**: Verify current MasterClock interface matches planned Phase 4 interface.

**Procedure**:
1. Read Phase 4 MasterClock specification from Retrovue runtime docs
2. Compare current interface against specification
3. Assert all required methods present:
   - `now_utc_us() const`
   - `now_local_us() const`
   - `to_local(int64_t utc_us) const`
   - `offset_from_schedule(int64_t scheduled_pts_us) const`
   - `frequency() const`

**Pass Criteria**: Current interface is superset of Phase 4 requirements; no breaking changes needed.

---

### Test 8.2: MasterClock Dependency Injection Readiness

**Objective**: Verify playout components can receive runtime-provided MasterClock without modification.

**Procedure**:
1. Simulate runtime initialization:
   ```cpp
   auto runtime_clock = std::make_shared<RuntimeMasterClock>();
   auto channel = Channel(1, runtime_clock);
   ```
2. Start channel and decode frames
3. Assert channel uses runtime_clock (not internal mock)

**Pass Criteria**: No code changes required to use runtime-provided MasterClock.

---

## Phase 4: Advanced Synchronization Tests (Future)

> **Status**: NOT YET IMPLEMENTED — Placeholder for Phase 4 multi-channel sync validation
> 
> **Activation**: These tests will be enabled when the RetroVue runtime provides the production MasterClock implementation with NTP synchronization and multi-process coordination capabilities.

<!--
### Test P4.1: Multi-Channel Frame Alignment

**Objective**: Verify multiple channels remain frame-synchronized within 1 frame (33 ms) across sustained playout.

**Procedure**:
1. Start 4 channels with synchronized content (same source, offset start times)
2. Run for 300 seconds (10 minutes)
3. Sample frame PTS from each channel every 100 ms (3000 samples × 4 channels)
4. Calculate pairwise frame offset: max(|PTS_ch1 - PTS_ch2|, |PTS_ch1 - PTS_ch3|, ...)
5. Assert: 99% of samples have pairwise offset < 33.37 ms (one frame)

**Pass Criteria**: All channel pairs remain within 1-frame sync for 99% of runtime.

---

### Test P4.2: NTP Clock Slew Handling

**Objective**: Verify playout continues smoothly when MasterClock adjusts for NTP synchronization (slewing, not stepping).

**Procedure**:
1. Start channel with production MasterClock (NTP-synced)
2. Simulate NTP slew: MasterClock advances at 1.001x speed for 60 seconds (60 ms accumulated drift)
3. Decode and render continuously during slew
4. Assert:
   - No frames dropped due to clock adjustment
   - `clock_drift_seconds` metric reflects slew
   - `clock_drift_warning_total` increments if drift exceeds threshold
   - Rendering continues without buffer underrun

**Pass Criteria**: System adapts to slew without frame loss or state transitions.

---

### Test P4.3: Cross-Process Clock Consistency

**Objective**: Verify MasterClock provides consistent time across multiple playout engine processes (containerized deployment).

**Procedure**:
1. Start 2 playout processes in separate containers/processes
2. Both query runtime MasterClock: `T1 = clock1->now_utc_us()`, `T2 = clock2->now_utc_us()`
3. Repeat 1000 times over 60 seconds
4. Calculate max observed delta: `max(|T1 - T2|)`
5. Assert: max delta < 1 ms (microsecond-level consistency)

**Pass Criteria**: All processes see consistent time within 1 ms.

---

### Test P4.4: Timezone and DST Transitions

**Objective**: Verify MasterClock correctly handles timezone conversions and daylight saving time transitions.

**Procedure**:
1. Configure runtime MasterClock with timezone: "America/New_York"
2. Start channel at simulated time: 2024-03-10 01:30:00 EST (30 minutes before DST spring forward)
3. Run for 60 minutes (crosses DST boundary at 02:00 → 03:00)
4. Query `now_local_us()` before and after transition
5. Assert:
   - UTC time continues monotonically (no jump)
   - Local time jumps from 01:59:59 to 03:00:00
   - Playout continues without disruption

**Pass Criteria**: UTC monotonic; local time reflects DST; no frame loss.

---

### Test P4.5: High-Frequency Clock Query Performance

**Objective**: Verify MasterClock queries remain low-latency under high-frequency polling (production hot path).

**Procedure**:
1. Start 4 channels decoding 1080p60 content (240 fps total across channels)
2. Each frame render calls `clock->now_utc_us()` (240 Hz aggregate)
3. Run for 60 seconds (14,400 clock queries)
4. Measure per-query latency using RDTSC or equivalent
5. Assert:
   - p50 latency: < 50 ns
   - p99 latency: < 100 ns
   - No queries exceed 1 µs

**Pass Criteria**: All queries meet sub-microsecond latency targets.
-->

**Implementation Checklist for Phase 4**:
- [ ] Production MasterClock implementation available from RetroVue runtime
- [ ] NTP synchronization enabled and testable
- [ ] Multi-process/container deployment infrastructure ready
- [ ] Test harness can simulate NTP slew and DST transitions
- [ ] High-resolution timing instrumentation (RDTSC or equivalent) integrated

---

## Test Execution Matrix

### Unit Tests (Fast, < 1 second each)

| Test ID | Test Name                          | Target Component      | Runtime  |
| ------- | ---------------------------------- | --------------------- | -------- |
| 1.2     | MasterClock Injection              | All                   | < 0.1s   |
| 1.3     | MockMasterClock Determinism        | Test Infrastructure   | < 0.5s   |
| 2.1     | Decoder Uses MasterClock           | FFmpegDecoder         | < 0.5s   |
| 2.2     | Renderer Synchronizes              | FrameRenderer         | < 0.5s   |
| 2.3     | Buffer Timing Pass-Through         | FrameRingBuffer       | < 0.1s   |
| 5.3     | Metric Types Correct               | MetricsHTTPServer     | < 0.1s   |
| 6.2     | Instantaneous vs Cumulative        | MetricsExporter       | < 0.5s   |
| 8.1     | Interface Stability                | MasterClock           | < 0.1s   |

### Integration Tests (Medium, 1-10 seconds)

| Test ID | Test Name                          | Components            | Runtime  |
| ------- | ---------------------------------- | --------------------- | -------- |
| 3.1     | Nominal Cadence Enforcement        | Decoder + Renderer    | ~10s     |
| 3.2     | Source Time Base Override          | Decoder + Renderer    | ~5s      |
| 3.3     | Clock Offset Metric Accuracy       | All + Metrics         | ~3s      |
| 3.4     | MasterClock Offset Propagation     | All + Metrics + HTTP  | ~4s      |
| 5.1     | Required Metrics Presence          | All + HTTP            | ~1s      |
| 5.2     | Metric Value Correctness           | All + HTTP            | ~10s     |
| 6.1     | Sampling Frequency                 | All + Metrics         | ~5s      |
| 7.1     | Frame Late Detection               | Renderer + Metrics    | ~2s      |
| 7.2     | Frame Dropped Detection            | Buffer + Metrics      | ~2s      |
| 7.3     | Clock Drift Warning                | All + Metrics         | ~5s      |
| 7.4     | Buffer Underrun Recovery           | All + Metrics         | ~3s      |
| 8.2     | Dependency Injection Readiness     | All                   | ~2s      |

### Load Tests (Slow, 60+ seconds)

| Test ID | Test Name                          | Scenario              | Runtime  |
| ------- | ---------------------------------- | --------------------- | -------- |
| 1.1     | No Direct System Clock Calls       | 100 frame decode      | ~3s      |
| 4.1     | Average Latency Under Load         | 2ch × 60s continuous  | ~60s     |
| 4.2     | Sustained Latency Peak             | 1ch × 120s continuous | ~120s    |
| 4.3     | Latency Under Contention           | 4ch × 60s continuous  | ~60s     |
| 4.4     | Latency Violation Logging          | 100 frame w/ delay    | ~10s     |

---

## Continuous Integration Requirements

All tests in this contract must:

1. **Pass on every commit** to main branch
2. **Run in CI pipeline** (GitHub Actions, Jenkins, etc.)
3. **Block merge** if any test fails
4. **Report coverage** (aim for 100% of invariant code paths)

**Recommended CI Stages**:

```yaml
stages:
  - build
  - test_unit       # Run 1.2, 1.3, 2.x, 5.3, 6.2, 8.1 (< 5s total)
  - test_integration # Run 3.x, 5.1, 5.2, 6.1, 7.x, 8.2 (< 45s total)
  - test_load       # Run 1.1, 4.x (< 200s total)
```

---

## Acceptance Criteria Summary

The Metrics & Timing Domain is considered **validated** when:

✅ All 25 tests pass consistently (10 consecutive runs)  
✅ No timing regressions detected in benchmark suite  
✅ Metrics endpoint responds < 100ms under load  
✅ Zero system clock calls detected in production build  
✅ All timing anomalies logged and surfaced in `/metrics`  
✅ MasterClock interface matches Phase 4 specification  

---

## See Also

- [Metrics & Timing Domain](../domain/MetricsAndTimingDomain.md) — Domain specification and invariants
- [Development Standards](../developer/DevelopmentStandards.md) — Testing conventions
- [Playout Engine Domain Contract](PlayoutEngineDomainContract.md) — Related contracts
- [Architecture Overview](../architecture/ArchitectureOverview.md) — System context
