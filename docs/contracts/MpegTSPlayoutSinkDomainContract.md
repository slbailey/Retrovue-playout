_Related: [MPEG-TS Playout Sink Domain](../domain/MpegTSPlayoutSinkDomain.md) • [Playout Engine Contract](PlayoutEngineContract.md) • [Video File Producer Contract](VideoFileProducerDomainContract.md) • [Architecture Overview](../architecture/ArchitectureOverview.md)_

# Contract — MPEG-TS Playout Sink Domain

Status: Enforced

## Purpose

This document defines the **behavioral and testing contract** for the MPEG-TS Playout Sink subsystem in the RetroVue Air playout engine. The MPEG-TS Playout Sink is responsible for consuming decoded frames from a `FrameRingBuffer`, encoding them into H.264, wrapping them in MPEG-TS format, and streaming them over TCP socket. The sink owns its own timing loop that continuously queries MasterClock and compares with frame PTS to determine when to output frames.

This contract establishes:

- **Functional guarantees** for frame consumption, encoding, muxing, and streaming operations
- **Performance expectations** for encoding throughput, latency, and resource utilization
- **Error recovery procedures** for encoding failures, network errors, and buffer underruns
- **Lifecycle guarantees** for start, stop, and teardown operations
- **Verification criteria** for automated testing and continuous validation

The MPEG-TS Playout Sink must operate deterministically, own its timing loop (continuously querying MasterClock), and provide observable statistics for all operational states. All frames consumed are decoded and ready for encoding. The sink pulls time from MasterClock (MasterClock never pushes ticks).

---

## Scope

This contract enforces guarantees for the following MPEG-TS Playout Sink components:

### 1. MpegTSPlayoutSink (Core Component)

**Purpose**: Manages the sink worker thread lifecycle (owns timing loop) and coordinates frame consumption, encoding, and streaming. Performs encoding, muxing, and network output internally. The sink continuously queries MasterClock and compares with frame PTS to determine when to output frames.

**Contract**:

```cpp
class MpegTSPlayoutSink : public IPlayoutSink {
public:
    MpegTSPlayoutSink(
        const SinkConfig& config,
        buffer::FrameRingBuffer& input_buffer,
        std::shared_ptr<timing::MasterClock> master_clock
    );
    
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    
    uint64_t getFramesSent() const override;
    uint64_t getFramesDropped() const override;
    uint64_t getLateFrames() const override;
    
    uint64_t getEncodingErrors() const;
    uint64_t getNetworkErrors() const;
    uint64_t getBufferEmptyCount() const;
};
```

**Guarantees**:

- `start()` returns false if already running (idempotent start prevention)
- `start()` starts worker thread that owns the timing loop
- `stop()` stops worker thread and blocks until thread exits (safe to call from any thread)
- Destructor automatically calls `stop()` if sink is still running
- Statistics (`getFramesSent()`, `getFramesDropped()`, `getLateFrames()`) are thread-safe
- All frames consumed are decoded (YUV420 format, ready for encoding)
- Sink owns timing loop: continuously queries MasterClock and compares with frame PTS
- MasterClock never pushes ticks (sink pulls time from MasterClock)

### 2. Internal Encoder Subsystem

**Purpose**: Encapsulated libx264 and FFmpeg-based components that perform encoding, muxing, and network output.

**Components**:

- **Encoder** (libx264): Encodes decoded YUV420 frames into H.264 NAL units
- **Muxer** (libavformat): Packages H.264 packets into MPEG-TS transport stream format
- **Network Output**: Sends MPEG-TS packets to TCP socket (server mode, accepts one client)

**Guarantees**:

- All encoding operations are internal to MpegTSPlayoutSink
- External components only interact with MPEG-TS stream output
- Encoder subsystem handles H.264 encoding, MPEG-TS muxing, and network streaming
- Encoder subsystem reports encoding errors for recovery handling
- Encoder subsystem reports network errors for recovery handling

---

## Test Environment Setup

All MPEG-TS Playout Sink contract tests must run in a controlled environment with the following prerequisites:

### Required Resources

| Resource              | Specification                                      | Purpose                            |
| --------------------- | -------------------------------------------------- | ---------------------------------- |
| Test Decoded Frames   | YUV420 1080p30, 10s duration, monotonic PTS       | Standard frame source for testing  |
| FrameRingBuffer       | 60-frame capacity (default)                        | Frame staging buffer (decoded frames only) |
| MasterClock Mock      | Monotonic, microsecond precision                   | Controlled timing source           |
| Internal Encoder      | libx264 + libavformat (optional for stub mode)    | Real video encode (if available)   |
| Network Test Harness  | TCP client for MPEG-TS validation                | Stream validation                  |
| VLC Media Player      | For MPEG-TS playback verification                 | Stream playability verification    |

### Environment Variables

```bash
RETROVUE_SINK_TARGET_FPS=30.0        # Target frame rate
RETROVUE_SINK_BITRATE=5000000         # Encoding bitrate (5 Mbps)
RETROVUE_SINK_GOP_SIZE=30             # GOP size (1 second at 30fps)
RETROVUE_SINK_PORT=9000                          # TCP server port
RETROVUE_SINK_STUB_MODE=false        # Use real encode (true for stub mode)
RETROVUE_SINK_BUFFER_SIZE=60         # FrameRingBuffer capacity
```

### Pre-Test Validation

Before running contract tests, verify:

1. ✅ FrameRingBuffer operations pass smoke tests
2. ✅ MasterClock advances monotonically
3. ✅ Test decoded frames are valid YUV420 format
4. ✅ Internal encoder subsystem initializes correctly (if using real encode mode)
5. ✅ Network test harness can connect to TCP socket
6. ✅ VLC can play MPEG-TS streams (for PS-005 verification)
7. ✅ Stub mode consumes frames correctly (fallback validation)

---

## Functional Expectations

The MPEG-TS Playout Sink must satisfy the following behavioral guarantees:

### FE-001: Sink Lifecycle

**Rule**: Sink must support clean start, stop, and teardown operations.

**Expected Behavior**:

- Sink initializes in stopped state with zero frames sent
- `start()` returns true on first call, false if already running
- `start()` starts worker thread that owns the timing loop
- `stop()` stops worker thread and blocks until thread exits (no hanging threads)
- `stop()` is idempotent (safe to call multiple times)
- Destructor automatically stops sink if still running

**Test Criteria**:

- ✅ Construction: `isRunning() == false`, `getFramesSent() == 0`
- ✅ Start: `start() == true`, `isRunning() == true`
- ✅ Start twice: Second `start()` returns false
- ✅ Worker thread: Worker thread starts and runs timing loop
- ✅ Stop: `stop()` stops worker thread and blocks until thread exits, `isRunning() == false`
- ✅ Stop idempotent: Multiple `stop()` calls are safe
- ✅ Destructor: Sink stops automatically on destruction

**Test Files**: `tests/test_sink.cpp` (Construction, StartStop, CannotStartTwice, StopIdempotent, DestructorStopsSink)

---

### FE-002: Pulls Frames in Order

**Rule**: Sink must pull decoded frames from buffer in order (FIFO).

**Expected Behavior**:

- Sink pops frames from buffer in the order they were pushed
- Sink maintains frame order through encoding and output
- Sink does not skip frames unless they are late (see FE-003)
- Frame PTS values increase monotonically in output stream

**Test Criteria**:

- ✅ Frame order: Frames are output in the same order as pushed to buffer
- ✅ PTS monotonicity: Output frame PTS values increase monotonically
- ✅ No frame reordering: Sink does not reorder frames during encoding
- ✅ FIFO compliance: Buffer pop order matches push order

**Test Files**: `tests/test_sink.cpp` (FrameOrder, FramePTSMonotonicity)

---

### FE-003: Obeys Master Clock

**Rule**: Sink must own timing loop, query MasterClock, and output frames when PTS is due.

**Expected Behavior**:

- Sink owns timing loop (worker thread continuously running)
- Sink queries `MasterClock::now_utc_us()` in timing loop
- Sink compares frame PTS with MasterClock time: if `frame.pts_us <= master_time_us`, output frame
- Sink sleeps when ahead of schedule (frame PTS in future)
- Sink drops frames when behind schedule (frame PTS overdue)
- Sink maintains real-time output pacing aligned to MasterClock
- MasterClock never pushes ticks (sink pulls time from MasterClock)

**Test Criteria**:

- ✅ Timing loop: Sink worker thread continuously runs timing loop
- ✅ Clock query: Sink queries MasterClock in timing loop (`master_clock_->now_utc_us()`)
- ✅ Frame selection: Sink compares frame PTS with MasterClock time to determine when to output
- ✅ Ahead of schedule: Sink sleeps when frame PTS is in future
- ✅ Behind schedule: Sink drops frames when frame PTS is overdue
- ✅ Timing accuracy: Frame output timing matches MasterClock time (within 1ms)
- ✅ Late frame detection: Sink detects late frames correctly (threshold: 33ms at 30fps)
- ✅ No tick() calls: MasterClock never calls tick() on sink (sink pulls time)

**Test Files**: `tests/test_sink.cpp` (MasterClockAlignment), `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp`

---

### FE-004: Encodes Valid H.264 Frames

**Rule**: Sink must encode decoded frames into valid H.264 NAL units.

**Expected Behavior**:

- Encoder produces valid H.264 NAL units (SPS, PPS, IDR, P, B frames)
- Encoder maintains encoder state correctly (reference frames, GOP structure)
- Encoded bitstream is decodable by standard H.264 decoders
- Encoder respects bitrate and GOP size configuration

**Test Criteria**:

- ✅ Valid H.264: Encoded output is valid H.264 bitstream (parsable by libavcodec)
- ✅ SPS/PPS: Encoder generates SPS/PPS when needed (IDR frames)
- ✅ Decodable: Encoded frames can be decoded back to YUV420 (round-trip test)
- ✅ Bitrate compliance: Encoded bitrate matches configuration (within 10% tolerance)
- ✅ GOP structure: IDR frames appear at configured GOP interval

**Test Files**: `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp` (FE_004_EncodesValidH264)

---

### FE-005: Produces Playable MPEG-TS Stream Verified in VLC

**Rule**: Sink must produce a playable MPEG-TS stream that can be verified in VLC.

**Expected Behavior**:

- Muxer packages H.264 packets into valid MPEG-TS transport stream
- MPEG-TS stream contains PAT/PMT tables
- MPEG-TS stream has correct PTS/DTS mapping
- Stream is playable in VLC media player
- Stream maintains correct frame rate for broadcast

**Test Criteria**:

- ✅ Valid MPEG-TS: Output stream is valid MPEG-TS format (parsable by libavformat)
- ✅ PAT/PMT: Stream contains Program Association Table and Program Map Table
- ✅ VLC playback: Stream plays correctly in VLC (no artifacts, correct frame rate)
- ✅ Frame rate: Stream maintains target frame rate (30fps for 30fps target)
- ✅ PTS/DTS mapping: MPEG-TS PTS/DTS values match input frame PTS/DTS

**Test Files**: `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp` (FE_005_ProducesPlayableMPEGTS)

---

### FE-006: Handles Empty Buffer (Waits, No Crash)

**Rule**: Sink must handle empty buffer gracefully by waiting, not crashing.

**Expected Behavior**:

- When buffer is empty in timing loop:
  - Sink increments `buffer_empty_count_` statistic
  - Sink applies underflow policy (frame freeze, black frame, or skip)
  - Sink waits 10ms (`kSinkWaitUs`)
  - Sink retries peek on next loop iteration
- Sink never crashes or throws exception on empty buffer
- Sink resumes normal operation when buffer has frames

**Test Criteria**:

- ✅ Empty buffer detection: `getBufferEmptyCount() > 0` when buffer is empty
- ✅ Underflow policy: Sink applies underflow policy (frame freeze, black frame, or skip)
- ✅ Wait timing: Sink waits ~10ms before retry (within 2ms tolerance)
- ✅ No crash: Sink does not crash or throw exception on empty buffer
- ✅ Recovery: Sink resumes frame consumption when buffer has frames
- ✅ Statistics accuracy: `getBufferEmptyCount()` accurately tracks empty buffer events

**Test Files**: `tests/test_sink.cpp` (EmptyBufferHandling)

---

### FE-007: Handles Buffer Overrun (Drops, No Crash)

**Rule**: Sink must handle buffer overrun gracefully by dropping late frames, not crashing.

**Expected Behavior**:

- When sink is behind schedule (frames are late):
  - Sink drops late frames (more than `kLateThresholdUs` late)
  - Sink increments `frames_dropped_` and `late_frames_` counters
  - Sink continues to next frame (attempts to catch up)
- Sink never crashes or throws exception on buffer overrun
- Sink maintains real-time output (no buffering of late frames)

**Test Criteria**:

- ✅ Late frame detection: Sink detects when frames are late (deadline in past)
- ✅ Frame dropping: Sink drops late frames when behind schedule
- ✅ Statistics tracking: `GetFramesDropped()` and `GetLateFrames()` increment correctly
- ✅ No crash: Sink does not crash or throw exception on buffer overrun
- ✅ Real-time output: Sink maintains real-time output pacing (no buffering)

**Test Files**: `tests/test_sink.cpp` (BufferOverrunHandling), `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp`

---

### FE-008: Properly Reports Stats (Frames Sent, Frames Dropped, Late Frames)

**Rule**: Sink statistics must accurately reflect operational state.

**Expected Behavior**:

- `getFramesSent()`: Counts only successfully sent frames
- `getFramesDropped()`: Counts frames dropped due to lateness
- `getLateFrames()`: Counts late frame events
- `getEncodingErrors()`: Counts encoding failures
- `getNetworkErrors()`: Counts network send failures
- `getBufferEmptyCount()`: Counts buffer empty events
- Statistics are updated atomically (thread-safe)
- Statistics are safe to read from any thread

**Test Criteria**:

- ✅ Frame counting: `getFramesSent()` matches actual frames sent to output
- ✅ Drop tracking: `getFramesDropped()` increments on each late frame drop
- ✅ Late frame tracking: `getLateFrames()` increments on each late frame event
- ✅ Error tracking: `getEncodingErrors()` and `getNetworkErrors()` track failures
- ✅ Thread safety: Statistics can be read from any thread without race conditions
- ✅ Accuracy: Statistics reflect actual operational state

**Test Files**: `tests/test_sink.cpp` (StatisticsAccuracy, BufferOverrunHandling, EmptyBufferHandling)

---

## Performance Expectations

The MPEG-TS Playout Sink must meet the following performance targets:

### PE-001: Encoding Throughput

**Target**: Sink must encode and output frames at or above `target_fps` rate.

**Metrics**:

- **Stub mode**: ≥ `target_fps` frames/second (e.g., ≥ 30 fps for 30 fps target)
- **Real encode mode**: ≥ `target_fps` frames/second for 1080p30 H.264 encoding
- **4K content**: ≥ 30 frames/second with hardware acceleration enabled

**Measurement**:

- Run sink for 10 seconds
- Measure `getFramesSent() / elapsed_time`
- Verify throughput ≥ `target_fps × 0.95` (5% tolerance)
- Verify all frames are encoded and sent

**Test Files**: `tests/test_performance.cpp`

---

### PE-002: Frame Encoding Latency

**Target**: Frame encoding latency (from buffer pop to MPEG-TS output) must be bounded.

**Metrics**:

- **Stub mode**: Frame consumption latency < 1ms (p95)
- **Real encode mode**: Frame encoding latency < 33ms (p95) for 1080p30 H.264
- **4K content**: Frame encoding latency < 50ms (p95) with hardware acceleration

**Measurement**:

- Measure time from buffer pop to MPEG-TS packet send completion
- Calculate p95 latency over 1000 frames
- Verify p95 latency < target threshold

**Test Files**: `tests/test_performance.cpp`

---

### PE-003: Memory Utilization

**Target**: Sink memory usage must be bounded.

**Metrics**:

- **Per-channel memory**: < 150 MB (encoder buffers + muxer overhead)
- **Internal encoder overhead**: < 50 MB per channel
- **Total memory**: < 200 MB per channel

**Measurement**:

- Run sink for 60 seconds
- Measure peak memory usage (RSS)
- Verify memory < threshold

**Test Files**: `tests/test_performance.cpp`

---

### PE-004: CPU Utilization

**Target**: Sink CPU usage must be reasonable.

**Metrics**:

- **Stub mode**: < 5% CPU per channel (single core)
- **Real encode mode (1080p30)**: < 40% CPU per channel (single core)
- **Real encode mode (4K)**: < 60% CPU per channel with hardware acceleration

**Measurement**:

- Run sink for 60 seconds
- Measure average CPU usage (single core)
- Verify CPU usage < threshold

**Test Files**: `tests/test_performance.cpp`

---

### PE-005: Network Output Rate

**Target**: Network output must maintain MPEG-TS packet delivery rate.

**Metrics**:

- **TCP mode**: Packet send rate ≥ `target_fps` packets/second
- **Non-blocking writes**: Network writes never block (EAGAIN/EWOULDBLOCK handled)
- **Client disconnect handling**: Sink tears down muxer and waits for new connection

**Measurement**:

- Run sink for 10 seconds
- Measure network packet send rate
- Verify send rate ≥ `target_fps × 0.95` (5% tolerance)
- Verify non-blocking writes (no blocking on network backpressure)

**Test Files**: `tests/test_performance.cpp`

---

## Error Handling

The MPEG-TS Playout Sink must handle the following error conditions:

### EH-001: Encoder Initialization Failure

**Condition**: Internal encoder subsystem initialization fails (libx264 not available, codec error, etc.).

**Expected Behavior**:

- Sink logs error: `"Failed to initialize encoder, falling back to stub mode"`
- Sink sets `config.stub_mode = true`
- Sink continues with stub frame consumption
- Sink does not crash or stop

**Test Criteria**:

- ✅ Encoder failure: Sink continues operation in stub mode
- ✅ Error is logged to stderr
- ✅ No crash or exception thrown
- ✅ Stub mode consumes frames correctly

**Test Files**: `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp`

---

### EH-002: Encoding Error (Encoder Failure)

**Condition**: Internal encoder subsystem reports encoding error (codec error, frame encoding failure).

**Expected Behavior**:

- Sink logs error: `"Encoding errors: N"`
- Sink increments `stats.encoding_errors`
- Sink skips frame and continues (allows recovery)
- Sink does not stop (allows recovery)
- Sink continues encoding frames after recovery

**Test Criteria**:

- ✅ Sink continues operation after encoding error
- ✅ Error is logged to stderr
- ✅ Sink skips failed frame and continues
- ✅ Sink resumes frame encoding after recovery

**Test Files**: `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp`

---

### EH-003: Network Error (TCP Send Failure)

**Condition**: Network output reports send failure (TCP write fails, client disconnect).

**Expected Behavior**:

- Sink logs error: `"Network errors: N"`
- Sink increments `stats.network_errors`
- If `EAGAIN`/`EWOULDBLOCK`: Drop frame, emit backpressure event, continue
- If client disconnect: Tear down muxer, wait for new connection
- Sink does not stop (allows recovery)
- Sink continues streaming after recovery

**Test Criteria**:

- ✅ Sink continues operation after network error
- ✅ Error is logged to stderr
- ✅ Sink retries send after backoff
- ✅ Sink resumes streaming after recovery

**Test Files**: `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp`

---

### EH-004: Buffer Empty

**Condition**: Buffer is empty when timing loop checks (peek returns null).

**Expected Behavior**:

- Sink increments `buffer_empty_count_`
- Sink applies underflow policy (frame freeze, black frame, or skip)
- Sink waits 10ms (`kSinkWaitUs`)
- Sink retries peek on next loop iteration
- Sink does not block or crash
- Sink continues consuming frames when buffer has frames

**Test Criteria**:

- ✅ Sink waits when buffer is empty
- ✅ Sink retries pop after wait
- ✅ Sink does not block or crash
- ✅ Sink resumes frame consumption when buffer has frames

**Test Files**: `tests/test_sink.cpp` (EmptyBufferHandling)

---

### EH-005: Late Frames (Behind Schedule)

**Condition**: Sink is behind schedule (frame PTS is overdue compared to MasterClock time).

**Expected Behavior**:

- Sink detects late frames in timing loop (frame PTS < master_time_us - threshold)
- Sink drops late frames (more than `kLateThresholdUs` late)
- Sink increments `frames_dropped_` and `late_frames_` counters
- Sink continues to next frame (attempts to catch up)
- Sink maintains real-time output (no buffering)

**Test Criteria**:

- ✅ Sink detects late frames correctly
- ✅ Sink drops late frames when behind schedule
- ✅ Sink tracks dropped frames in statistics
- ✅ Sink maintains real-time output pacing

**Test Files**: `tests/test_sink.cpp` (BufferOverrunHandling), `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp`

---

### EH-006: Worker Thread Stop

**Condition**: `stop()` is called while worker thread is running.

**Expected Behavior**:

- Sink sets `stop_requested_ = true`
- Worker thread exits timing loop
- Sink waits for worker thread to exit (join thread)
- Sink closes muxer, encoder, and socket
- Sink exits gracefully (may drop frames in buffer)

**Test Criteria**:

- ✅ Worker thread stops when `stop()` is called
- ✅ Sink waits for thread exit (no hanging threads)
- ✅ All resources are cleaned up (muxer, encoder, socket)
- ✅ Sink exits within 100ms of `stop()` call

**Test Files**: `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp`

---

## Test Coverage Requirements

All functional expectations (FE-001 through FE-008) must have corresponding test coverage.

### Test File Mapping

| Functional Expectation | Test File                                    | Test Case Name                    |
| ---------------------- | -------------------------------------------- | ---------------------------------- |
| FE-001                 | `tests/test_sink.cpp`                        | Construction, StartStop, CannotStartTwice, StopIdempotent, DestructorStopsSink |
| FE-002                 | `tests/test_sink.cpp`                        | FrameOrder, FramePTSMonotonicity   |
| FE-003                 | `tests/test_sink.cpp`                        | MasterClockAlignment               |
| FE-003                 | `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp` | FE_003_ObeysMasterClock |
| FE-004                 | `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp` | FE_004_EncodesValidH264 |
| FE-005                 | `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp` | FE_005_ProducesPlayableMPEGTS |
| FE-006                 | `tests/test_sink.cpp`                        | EmptyBufferHandling                |
| FE-007                 | `tests/test_sink.cpp`                        | BufferOverrunHandling              |
| FE-007                 | `tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp` | FE_007_HandlesBufferOverrun |
| FE-008                 | `tests/test_sink.cpp`                        | StatisticsAccuracy, BufferOverrunHandling, EmptyBufferHandling |

### Coverage Requirements

- ✅ **Unit Tests**: All FE rules must have unit test coverage
- ✅ **Integration Tests**: FE rules involving multiple components must have integration test coverage
- ✅ **Contract Tests**: All FE rules must be verified in contract test suite
- ✅ **Performance Tests**: All PE rules must have performance test coverage

### Test Execution

All tests must:

- Run in < 10 seconds (unit tests) or < 60 seconds (integration tests)
- Produce deterministic results (no flaky tests)
- Provide actionable diagnostics on failure
- Pass in both stub mode and real encode mode (where applicable)
- Verify all frames consumed are decoded (YUV420 format, not encoded packets)
- Verify MPEG-TS stream is playable in VLC (for FE-005)

---

## CI Enforcement

The following rules are enforced in continuous integration:

### Pre-Merge Requirements

1. ✅ All unit tests pass (`tests/test_sink.cpp`)
2. ✅ All contract tests pass (`tests/contracts/MpegTSPlayoutSink/MpegTSPlayoutSinkContractTests.cpp`)
3. ✅ Code coverage ≥ 90% for MpegTSPlayoutSink domain
4. ✅ No memory leaks (valgrind or AddressSanitizer)
5. ✅ No undefined behavior (UB sanitizer)

### Performance Gates

1. ✅ Encoding throughput ≥ `target_fps × 0.95` (5% tolerance)
2. ✅ Frame encoding latency < 33ms (p95) for 1080p30
3. ✅ Memory usage < 200 MB per channel
4. ✅ CPU usage < 40% per channel (1080p30)
5. ✅ Network output rate ≥ `target_fps × 0.95` (5% tolerance)

### Quality Gates

1. ✅ No compiler warnings (treat warnings as errors)
2. ✅ Static analysis passes (clang-tidy, cppcheck)
3. ✅ Documentation is up-to-date (domain doc matches implementation)
4. ✅ All frames verified as decoded (YUV420 format, not encoded packets)
5. ✅ MPEG-TS stream verified as playable in VLC (for FE-005)

---

## See Also

- [MPEG-TS Playout Sink Domain](../domain/MpegTSPlayoutSinkDomain.md) — Domain model and architecture
- [Playout Engine Contract](PlayoutEngineContract.md) — Overall playout engine contract
- [Video File Producer Contract](VideoFileProducerDomainContract.md) — Frame production contract
- [Architecture Overview](../architecture/ArchitectureOverview.md) — System-wide architecture

---

## Design Notes (2025)

### Architectural Pattern

This contract document establishes the **Sink Pattern** for RetroVue Air playout:

**Core Rule**: A Sink in RetroVue consumes *decoded frames* from FrameRingBuffer and outputs them in a specific format (MPEG-TS, file, display, etc.). A Sink does *not* decode frames; frames are already decoded when consumed.

### Key Design Decisions

1. **Unified Sink Model**: MpegTSPlayoutSink performs encoding, muxing, and streaming internally. There is no separate encoder stage in the pipeline. Instead, MpegTSPlayoutSink *contains* the encoding subsystem internally.

2. **Decoded Frames Only**: FrameRingBuffer explicitly contains only decoded frames. Sinks consume decoded frames and transform them into output format (MPEG-TS, file, display).

3. **Simplified Pipeline**: The pipeline is now simpler and more object-oriented:
   - Input: decoded Frame objects from FrameRingBuffer
   - MpegTSPlayoutSink: consumes decoded frames + encodes internally → outputs MPEG-TS stream
   - FrameRingBuffer: stores decoded frames
   - Producer: generates decoded frames (never encoded packets)

4. **Interface Inheritance**: All sinks inherit from `IPlayoutSink`, enabling interchangeable sink implementations and hot-swapping without changing core architecture.

5. **MasterClock Timing**: Sink owns timing loop that continuously queries MasterClock (`master_clock_->now_utc_us()`) and compares with frame PTS to determine when to output. Sink sleeps when ahead of schedule and drops frames when behind schedule. MasterClock never pushes ticks (sink pulls time from MasterClock).

### Benefits

- **Cleaner Architecture**: Clear separation of concerns with Sinks handling all encoding/output internally
- **Simplified Pipeline**: No separate encode stage simplifies the system
- **Consistent Pattern**: All sinks follow the same pattern (consume decoded frames, output in specific format)
- **Better Encapsulation**: Encoding operations are internal implementation details
- **Interchangeable Sinks**: Base interface enables hot-swapping of sink implementations
- **Real-Time Guarantees**: MasterClock timing ensures broadcast timing accuracy

