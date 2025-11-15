_Metadata: Status=Draft; Scope=Contract; Owner=@runtime-platform_

_Related: [Playout Control Domain](../domain/PlayoutControlDomain.md); [Architecture Overview](../architecture/ArchitectureOverview.md); [Metrics Export Domain](../domain/MetricsExportDomain.md)_

# Contract - Playout Control Domain

## Purpose

Define enforceable guarantees for session control covering state transitions, latency tolerances, and fault telemetry.

## CTL_001: Deterministic State Transitions

**Intent**  
Ensure the control plane only performs legal transitions from the published matrix and records violations.

**Setup**  
Channel initialized with valid schedule. Controller instrumented to observe `playout_control_state_transition_total` and `playout_control_illegal_transition_total`.

**Stimulus**  
Issue every legal command sequence once (`BeginSession`, `Pause`, `Resume`, `Seek`, `Stop`, `Recover`), then attempt an illegal transition (e.g., `Paused → Idle`).

**Assertions**

- Legal transitions move the channel through expected states with no intermediate states skipped.
- `playout_control_state_transition_total{from="<X>",to="<Y>"}` increments exactly once per successful transition.
- Illegal request rejected with no state change and increments `playout_control_illegal_transition_total{from="Paused",to="Idle"}` within 1 s.

**Failure Semantics**  
If any illegal transition proceeds, controller enters `Error`, emits `playout_control_illegal_transition_total`, and surfaces a critical alert.

## CTL_002: Control Action Latency Compliance

**Intent**  
Guarantee pause/resume/seek/stop latencies remain within documented tolerances while preserving ordering.

**Setup**  
Running channel in `Playing` state with MasterClock drift within nominal bounds. Metrics exporter captures latency histograms.

**Stimulus**  
Execute sequentially: `Pause`, `Resume`, `Seek` (forward 5 s), `Stop`, recording wall-clock deltas between command UTC and state change.

**Assertions**

- `playout_control_pause_latency_ms` p95 ≤ 33 ms; renderer halts on next frame boundary.
- `playout_control_resume_latency_ms` p95 ≤ 50 ms; first frame post-resume aligns with scheduled deadline.
- Seek completes (`Playing` → `Buffering` → `Playing`) in ≤ 250 ms end-to-end; resume latency logged.
- `playout_control_stop_duration_ms` ≤ 500 ms; final state `Idle`.
- Teardown initiated via `RequestTeardown` drains producer within the configured timeout and logs `playout_control_teardown_duration_ms`.

**Failure Semantics**  
Breaching any threshold triggers `playout_control_latency_violation_total` and escalates channel to `Error` with requirement for manual `Recover`.

## CTL_003: Command Idempotency & Failure Telemetry

**Intent**  
Validate `(channel_id, command_id)` deduplication window and failure telemetry coverage.

**Setup**  
Channel in `Playing`. Dedup window configured for 60 s. Metrics exporter watching `playout_control_timeout_ms`, `playout_control_queue_overflow_total`, `playout_control_recover_total`.

**Stimulus**  
Send duplicate `Seek` command with same `command_id` within 5 s, then simulate external control timeout (> SLA) and command queue overflow (flood of commands).
- Additionally, trigger a `RequestTeardown` to confirm deduplication and latency metrics remain consistent.

**Assertions**

- Duplicate command acknowledged without side effects; state remains `Playing`; no additional transition metric increment.
- Timeout forces controller to `Error`, increments `playout_control_timeout_ms` histogram bucket and requires subsequent `Recover` to exit error.
- Overflow increments `playout_control_queue_overflow_total` while channel stays in current state and throttles new commands for ≥100 ms.

**Failure Semantics**  
If duplicates mutate state or telemetry missing, controller marks channel `Error` and raises `playout_control_consistency_failure` alert.

## CTL_004: Dual-Producer Preview/Live Slot Management

**Intent**  
Ensure preview and live slots are managed correctly, allowing seamless switching between producers using pull-based architecture.

**Setup**  
Channel initialized with state machine. Producer factory configured to create VideoFileProducer instances. FrameRouter configured to pull from active producer.

**Stimulus**  
1. Load preview asset via `loadPreviewAsset(path, assetId, ringBuffer, clock)`
2. Verify preview slot is loaded in shadow decode mode, live slot is empty
3. Verify FrameRouter is pulling from live producer (if any) and writing to buffer
4. Activate preview as live via `activatePreviewAsLive()`
5. Verify preview slot is empty, live slot contains the producer
6. Verify FrameRouter switches to pull from new live producer
7. Load another preview asset
8. Verify preview slot contains new asset, live slot still has original

**Assertions**

- Preview slot loads producer successfully with correct asset_id and file_path
- Preview producer runs in shadow decode mode (decodes frames but FrameRouter does not pull from it)
- Preview producer exposes pull-based API (`nextFrame()`) but is not pulled by FrameRouter until switch
- Live slot remains empty until `activatePreviewAsLive()` is called
- After activation, preview slot is reset and live slot contains the producer
- FrameRouter switches which producer it calls `nextFrame()` on atomically
- Multiple preview loads replace previous preview producer
- Live slot producer is stopped gracefully before switching

**Failure Semantics**  
If preview cannot be loaded or activation fails, operation returns false and state remains unchanged.

## CTL_005: Producer Switching Seamlessness

**Intent**  
Guarantee that switching from preview to live happens at frame boundaries with perfect PTS continuity and no visual discontinuity, using pull-based architecture.

**Setup**  
Channel with live producer running. FrameRouter pulling from live producer and writing to ring buffer. Preview producer loaded in shadow decode mode and ready.

**Stimulus**  
1. Verify FrameRouter is pulling from live producer via `nextFrame()` and writing frames to buffer
2. Load preview asset (shadow decode mode)
3. Verify preview producer has decoded first frame and PTS is aligned
4. Call `activatePreviewAsLive()` to switch
5. Verify FrameRouter switches which producer it pulls from

**Assertions**

- **Slot switching occurs at a frame boundary, and the engine guarantees that the final LIVE frame and first PREVIEW frame are placed consecutively in the output ring buffer with no discontinuity in timing or PTS.**
- FrameRouter pulls last frame from live producer via `nextFrame()` and writes to buffer
- Preview producer PTS is aligned: `preview_first_pts = live_last_pts + frame_duration`
- FrameRouter switches which producer it calls `nextFrame()` on atomically
- FrameRouter pulls first frame from preview producer and writes to buffer immediately after last live frame
- Old live producer is stopped gracefully after switch
- New producer is moved to live slot
- Preview slot is reset after switch
- **Ring buffer persists** (not flushed during switch)
- **Renderer pipeline is NOT reset** (continues reading seamlessly)
- FrameRingBuffer contains: `[last_live_frame][first_preview_frame][...]` with continuous PTS
- No PTS jumps, no resets to zero, no negative deltas
- No visual discontinuity, no black frames, no stutter
- Switch completes within 100ms

**Failure Semantics**  
If switching fails, channel remains in current state with original live producer. Error is logged and operation returns false.
