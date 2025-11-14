_Related: [Video File Producer Domain](../domain/VideoFileProducerDomain.md) • [Playout Engine Contract](PlayoutEngineContract.md) • [Renderer Contract](RendererContract.md) • [Architecture Overview](../architecture/ArchitectureOverview.md)_

# Contract — Video File Producer Domain

Status: Enforced

## Purpose

This document defines the **behavioral and testing contract** for the Video File Producer subsystem in the RetroVue Air playout engine. The Video File Producer is responsible for reading local video files, decoding them internally, and producing raw decoded video frames for consumption by the Renderer.

This contract establishes:

- **Functional guarantees** for decoded frame production, decode pacing, and buffer management
- **Performance expectations** for decode throughput, latency, and resource utilization
- **Error recovery procedures** for internal decode failures, corrupt files, and buffer saturation
- **Lifecycle guarantees** for start, stop, and teardown operations
- **Verification criteria** for automated testing and continuous validation

The Video File Producer must operate deterministically, respect buffer capacity limits, and provide observable statistics for all operational states. All frames produced are decoded and ready for rendering.

---

## Scope

This contract enforces guarantees for the following Video File Producer components:

### 1. VideoFileProducer (Core Component)

**Purpose**: Manages the decode thread lifecycle and coordinates frame production. Performs both file reading and frame decoding internally.

**Contract**:

```cpp
class VideoFileProducer {
public:
    VideoFileProducer(
        const ProducerConfig& config,
        buffer::FrameRingBuffer& output_buffer,
        std::shared_ptr<timing::MasterClock> clock = nullptr
    );
    
    bool Start();
    void Stop();
    void RequestTeardown(std::chrono::milliseconds drain_timeout);
    void ForceStop();
    
    bool IsRunning() const;
    uint64_t GetFramesProduced() const;
    uint64_t GetBufferFullCount() const;
};
```

**Guarantees**:

- `Start()` returns false if already running (idempotent start prevention)
- `Stop()` blocks until decode thread exits (safe to call from any thread)
- `RequestTeardown()` initiates graceful shutdown with bounded timeout
- `ForceStop()` immediately stops producer (may drop frames)
- Destructor automatically calls `Stop()` if producer is still running
- Statistics (`GetFramesProduced()`, `GetBufferFullCount()`) are thread-safe
- All frames produced are decoded (YUV420 format, ready for renderer)

### 2. Internal Decoder Subsystem

**Purpose**: Encapsulated FFmpeg-based components that perform demuxing, decoding, scaling, and frame assembly.

**Components**:

- **Demuxer** (libavformat): Reads encoded packets from video file
- **Decoder** (libavcodec): Decodes packets to raw frames
- **Scaler** (libswscale): Converts frames to target resolution and YUV420 format
- **Frame Assembly**: Packages decoded frames with metadata (PTS, DTS, duration)

**Guarantees**:

- All decoding operations are internal to VideoFileProducer
- External components only interact with decoded Frame objects
- Decoder subsystem handles format detection, codec selection, and scaling
- Decoder subsystem reports EOF when file is exhausted
- Decoder subsystem reports decode errors for recovery handling

---

## Test Environment Setup

All Video File Producer contract tests must run in a controlled environment with the following prerequisites:

### Required Resources

| Resource              | Specification                                      | Purpose                            |
| --------------------- | -------------------------------------------------- | ---------------------------------- |
| Test Video Asset      | H.264 1080p30, 10s duration, monotonic PTS         | Standard frame source for testing  |
| FrameRingBuffer       | 60-frame capacity (default)                        | Frame staging buffer (decoded frames only) |
| MasterClock Mock      | Monotonic, microsecond precision                   | Controlled timing source           |
| Internal Decoder      | FFmpeg libavformat/libavcodec (optional for stub mode) | Real video decode (if available)   |

### Environment Variables

```bash
RETROVUE_PRODUCER_TARGET_FPS=30.0        # Target frame rate
RETROVUE_PRODUCER_TARGET_WIDTH=1920      # Target frame width
RETROVUE_PRODUCER_TARGET_HEIGHT=1080     # Target frame height
RETROVUE_PRODUCER_STUB_MODE=false        # Use real decode (true for stub mode)
RETROVUE_PRODUCER_BUFFER_SIZE=60         # FrameRingBuffer capacity
```

### Pre-Test Validation

Before running contract tests, verify:

1. ✅ FrameRingBuffer operations pass smoke tests
2. ✅ MasterClock advances monotonically
3. ✅ Test video asset decodes successfully (if using real decode mode)
4. ✅ Internal decoder subsystem initializes correctly (if using real decode mode)
5. ✅ Stub mode generates decoded frames correctly (fallback validation)

---

## Functional Expectations

The Video File Producer must satisfy the following behavioral guarantees:

### FE-001: Producer Lifecycle

**Rule**: Producer must support clean start, stop, and teardown operations.

**Expected Behavior**:

- Producer initializes in stopped state with zero decoded frames produced
- `Start()` returns true on first call, false if already running
- `Stop()` blocks until decode thread exits (no hanging threads)
- `Stop()` is idempotent (safe to call multiple times)
- Destructor automatically stops producer if still running

**Test Criteria**:

- ✅ Construction: `IsRunning() == false`, `GetFramesProduced() == 0`
- ✅ Start: `Start() == true`, `IsRunning() == true`
- ✅ Start twice: Second `Start()` returns false
- ✅ Stop: `Stop()` blocks until thread exits, `IsRunning() == false`
- ✅ Stop idempotent: Multiple `Stop()` calls are safe
- ✅ Destructor: Producer stops automatically on destruction

**Test Files**: `tests/test_decode.cpp` (Construction, StartStop, CannotStartTwice, StopIdempotent, DestructorStopsProducer)

---

### FE-002: Decoded Frame Production Rate

**Rule**: Producer must maintain decode rate aligned with `target_fps` configuration, producing decoded frames at the target rate.

**Expected Behavior**:

- **Stub mode**: Producer generates decoded frames at exactly `target_fps` using MasterClock pacing
- **Real decode mode**: Internal decoder reads and decodes frames as fast as possible from file, but PTS spacing reflects original file timing
- Frame interval: `1.0 / target_fps` seconds (e.g., 33,333 µs for 30 fps)
- Producer does not exceed target rate (no frame bursts)
- All frames produced are decoded (YUV420 format)

**Test Criteria**:

- ✅ Stub mode: Decoded frame generation rate matches `target_fps` within 5% tolerance
- ✅ Real decode mode: PTS spacing matches file timing (within 1ms tolerance)
- ✅ Frame interval: Duration between consecutive decoded frames ≈ `1.0 / target_fps`
- ✅ No frame bursts: Producer does not generate multiple decoded frames in rapid succession
- ✅ All frames decoded: Frame data is valid YUV420 format (not encoded packets)

**Test Files**: `tests/test_decode.cpp` (FillsBuffer), `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp`

---

### FE-003: Decoded Frame Metadata Validity

**Rule**: All produced decoded frames must have valid metadata (PTS, DTS, duration, asset_uri, dimensions).

**Expected Behavior**:

- `frame.metadata.pts`: Monotonically increasing int64_t (microseconds)
- `frame.metadata.dts`: int64_t ≤ pts (microseconds)
- `frame.metadata.duration`: Positive double (seconds), approximately `1.0 / target_fps`
- `frame.metadata.asset_uri`: Non-empty string matching `config.asset_uri`
- `frame.width`: Positive integer matching `config.target_width`
- `frame.height`: Positive integer matching `config.target_height`
- Frame data is decoded (YUV420 format, not encoded packets)

**Test Criteria**:

- ✅ PTS monotonicity: `frame[i].metadata.pts < frame[i+1].metadata.pts` for all decoded frames
- ✅ DTS ≤ PTS: `frame.metadata.dts <= frame.metadata.pts` for all decoded frames
- ✅ Duration: `frame.metadata.duration ≈ 1.0 / target_fps` (within 1% tolerance)
- ✅ Asset URI: `frame.metadata.asset_uri == config.asset_uri`
- ✅ Dimensions: `frame.width == config.target_width`, `frame.height == config.target_height`
- ✅ Decoded format: Frame data is decoded YUV420 (not encoded packets)

**Test Files**: `tests/test_decode.cpp` (FrameMetadata, FramePTSIncrementing)

---

### FE-004: Decoded Frame Format Validity

**Rule**: All produced frames must be valid YUV420 planar format (decoded) with correct data size.

**Expected Behavior**:

- Frame data format: YUV420 planar (decoded, not encoded packets)
- Y plane size: `width × height` bytes
- U plane size: `(width/2) × (height/2)` bytes
- V plane size: `(width/2) × (height/2)` bytes
- Total size: `width × height × 1.5` bytes
- Frame data is ready for renderer (no decoding required)

**Test Criteria**:

- ✅ Data size: `frame.data.size() == width × height × 1.5`
- ✅ Y plane: First `width × height` bytes are luminance data (decoded)
- ✅ U plane: Next `(width/2) × (height/2)` bytes are chrominance (U, decoded)
- ✅ V plane: Final `(width/2) × (height/2)` bytes are chrominance (V, decoded)
- ✅ Data non-empty: `frame.data.size() > 0` for all decoded frames
- ✅ Decoded format: Frame data is decoded YUV420 (not encoded packets)

**Test Files**: `tests/test_decode.cpp` (FrameMetadata)

---

### FE-005: Backpressure Handling

**Rule**: Producer must handle buffer full conditions gracefully by backing off and retrying.

**Expected Behavior**:

- When `FrameRingBuffer->Push()` returns false (buffer full):
  - Producer increments `buffer_full_count_`
  - Producer backs off for `kProducerBackoffUs` (10,000 µs = 10 ms)
  - Producer retries push on next iteration
- Producer never blocks waiting for buffer space
- Producer resumes normal operation when buffer space becomes available
- All frames pushed are decoded (ready for renderer)

**Test Criteria**:

- ✅ Buffer full detection: `GetBufferFullCount() > 0` when buffer is full
- ✅ Backoff timing: Producer waits ~10ms before retry (within 2ms tolerance)
- ✅ Non-blocking: Producer does not block on `Push()` failure
- ✅ Recovery: Producer resumes decoded frame production when buffer space available
- ✅ Statistics accuracy: `GetBufferFullCount()` accurately tracks backpressure events

**Test Files**: `tests/test_decode.cpp` (BufferFullHandling)

---

### FE-006: Buffer Filling

**Rule**: Producer must fill buffer to capacity with decoded frames when consumer is not pulling frames.

**Expected Behavior**:

- Producer continuously pushes decoded frames until buffer is full
- Buffer reaches capacity (60 decoded frames by default) when consumer is idle
- Producer backs off when buffer is full (see FE-005)
- Producer maintains buffer depth when consumer is pulling at target rate
- All frames in buffer are decoded (ready for renderer)

**Test Criteria**:

- ✅ Buffer fills: Buffer reaches capacity with decoded frames when consumer is not pulling
- ✅ Buffer depth: Buffer maintains depth ≥ 30 decoded frames during steady-state operation
- ✅ No frame drops: All decoded frames are successfully pushed (no silent failures)
- ✅ Decoded frames only: Buffer contains only decoded frames (no encoded packets)

**Test Files**: `tests/test_decode.cpp` (FillsBuffer)

---

### FE-007: Internal Decoder Fallback

**Rule**: Producer must fall back to stub mode if internal decoder subsystem fails to initialize.

**Expected Behavior**:

- If internal decoder subsystem initialization fails:
  - Producer logs error: `"Failed to initialize internal decoder, falling back to stub mode"`
  - Producer sets `config.stub_mode = true`
  - Producer continues with synthetic decoded frames
- Producer does not crash or stop on decoder initialization failure
- Stub mode generates decoded frames with correct metadata and format (YUV420)

**Test Criteria**:

- ✅ Decoder failure: Producer continues operation in stub mode
- ✅ Stub mode frames: Generated decoded frames have valid metadata and format (YUV420)
- ✅ Error logging: Error message is logged to stderr
- ✅ No crash: Producer does not crash on internal decoder initialization failure

**Test Files**: `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp`

---

### FE-008: Internal Decode Error Recovery

**Rule**: Producer must handle transient internal decode errors gracefully without stopping.

**Expected Behavior**:

- If internal decoder subsystem reports decode error (corrupt packet, codec error):
  - Producer logs error: `"Internal decode errors: N"`
  - Producer increments `stats.decode_errors`
  - Producer backs off 10ms and retries
  - Producer does not stop (allows recovery)
- Producer continues operation after transient errors
- Producer stops only on EOF or explicit stop request
- Producer continues producing decoded frames after recovery

**Test Criteria**:

- ✅ Transient errors: Producer continues operation after internal decode errors
- ✅ Error tracking: `stats.decode_errors` increments on decode failures
- ✅ Backoff: Producer backs off 10ms before retry
- ✅ Recovery: Producer resumes normal decoded frame production after error recovery

**Test Files**: `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp`

---

### FE-009: End of File Handling

**Rule**: Producer must stop gracefully when internal decoder subsystem reports end of file.

**Expected Behavior**:

- If internal decoder subsystem reports EOF:
  - Producer sets `stop_requested_` flag
  - Producer exits decode loop gracefully
  - Producer enters stopped state
- Producer does not generate decoded frames after EOF
- Producer logs: `"End of file reached"`

**Test Criteria**:

- ✅ EOF detection: Producer stops when internal decoder reports EOF
- ✅ Graceful stop: Producer exits decode loop without hanging
- ✅ No frames after EOF: Producer does not generate decoded frames after EOF
- ✅ State transition: Producer enters stopped state after EOF

**Test Files**: `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp`

---

### FE-010: Teardown Operation

**Rule**: Producer must support graceful teardown with bounded drain timeout.

**Expected Behavior**:

- `RequestTeardown(drain_timeout)` initiates graceful shutdown:
  - Producer stops generating new decoded frames
  - Producer waits for buffer to drain (consumer pulls remaining decoded frames)
  - Producer exits when buffer is empty or timeout is reached
- If timeout is reached:
  - Producer logs: `"Teardown timeout reached; forcing stop"`
  - Producer calls `ForceStop()`
- If buffer drains before timeout:
  - Producer logs: `"Buffer drained; completing teardown"`
  - Producer exits gracefully

**Test Criteria**:

- ✅ Teardown initiation: `RequestTeardown()` stops decoded frame generation
- ✅ Buffer drain: Producer waits for buffer to drain
- ✅ Timeout handling: Producer forces stop if timeout is reached
- ✅ Graceful exit: Producer exits cleanly when buffer drains

**Test Files**: `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp`

---

### FE-011: Statistics Accuracy

**Rule**: Producer statistics must accurately reflect operational state.

**Expected Behavior**:

- `GetFramesProduced()`: Counts only successfully pushed decoded frames
- `GetBufferFullCount()`: Counts each buffer full event (backpressure)
- Statistics are updated atomically (thread-safe)
- Statistics are safe to read from any thread

**Test Criteria**:

- ✅ Frame counting: `GetFramesProduced()` matches actual decoded frames in buffer
- ✅ Buffer full tracking: `GetBufferFullCount()` increments on each backpressure event
- ✅ Thread safety: Statistics can be read from any thread without race conditions
- ✅ Accuracy: Statistics reflect actual operational state (decoded frame production)

**Test Files**: `tests/test_decode.cpp` (BufferFullHandling, StartStop)

---

### FE-012: MasterClock Alignment (Stub Mode)

**Rule**: In stub mode, producer must align decoded frame production with MasterClock deadlines.

**Expected Behavior**:

- Producer queries `MasterClock::now_utc_us()` before generating each decoded frame
- Producer waits until `next_stub_deadline_utc_` before generating decoded frame
- Decoded frame production is aligned to MasterClock (deterministic timing)
- Producer does not generate decoded frames ahead of schedule

**Test Criteria**:

- ✅ Clock alignment: Decoded frame production aligns with MasterClock deadlines
- ✅ Timing accuracy: Frame interval matches `1.0 / target_fps` (within 1ms tolerance)
- ✅ No early frames: Producer does not generate decoded frames before deadline

**Test Files**: `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp`

---

## Performance Expectations

The Video File Producer must meet the following performance targets:

### PE-001: Decode Throughput

**Target**: Producer must decode frames at or above `target_fps` rate, producing decoded frames at target rate.

**Metrics**:

- **Stub mode**: ≥ `target_fps` decoded frames/second (e.g., ≥ 30 fps for 30 fps target)
- **Real decode mode**: ≥ `target_fps` decoded frames/second for H.264 1080p30 content
- **4K content**: ≥ 30 decoded frames/second with hardware acceleration enabled

**Measurement**:

- Run producer for 10 seconds
- Measure `GetFramesProduced() / elapsed_time`
- Verify throughput ≥ `target_fps × 0.95` (5% tolerance)
- Verify all frames are decoded (YUV420 format)

**Test Files**: `tests/test_performance.cpp`

---

### PE-002: Frame Production Latency

**Target**: Frame production latency (from file read to decoded frame push) must be bounded.

**Metrics**:

- **Stub mode**: Decoded frame generation latency < 1ms (p95)
- **Real decode mode**: Decoded frame production latency < 33ms (p95) for 1080p30 H.264
- **4K content**: Decoded frame production latency < 50ms (p95) with hardware acceleration

**Measurement**:

- Measure time from internal decoder read to decoded frame push completion
- Calculate p95 latency over 1000 decoded frames
- Verify p95 latency < target threshold

**Test Files**: `tests/test_performance.cpp`

---

### PE-003: Memory Utilization

**Target**: Producer memory usage must be bounded.

**Metrics**:

- **Per-channel memory**: < 200 MB (60 decoded frames × ~3 MB + internal decoder overhead)
- **Internal decoder overhead**: < 50 MB per channel
- **Total memory**: < 250 MB per channel

**Measurement**:

- Run producer for 60 seconds
- Measure peak memory usage (RSS)
- Verify memory < threshold

**Test Files**: `tests/test_performance.cpp`

---

### PE-004: CPU Utilization

**Target**: Producer CPU usage must be reasonable.

**Metrics**:

- **Stub mode**: < 5% CPU per channel (single core)
- **Real decode mode (1080p30)**: < 30% CPU per channel (single core)
- **Real decode mode (4K)**: < 50% CPU per channel with hardware acceleration

**Measurement**:

- Run producer for 60 seconds
- Measure average CPU usage (single core)
- Verify CPU usage < threshold

**Test Files**: `tests/test_performance.cpp`

---

## Error Handling

The Video File Producer must handle the following error conditions:

### EH-001: Internal Decoder Initialization Failure

**Condition**: Internal decoder subsystem initialization fails (file not found, codec not available, etc.).

**Expected Behavior**:

- Producer logs error: `"Failed to initialize internal decoder, falling back to stub mode"`
- Producer sets `config.stub_mode = true`
- Producer continues with synthetic decoded frames
- Producer does not crash or stop

**Test Criteria**:

- ✅ Producer continues operation in stub mode
- ✅ Error is logged to stderr
- ✅ No crash or exception thrown
- ✅ Stub mode produces decoded frames (YUV420 format)

**Test Files**: `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp`

---

### EH-002: Internal Decode Error (Corrupt Packet)

**Condition**: Internal decoder subsystem reports decode error due to corrupt packet.

**Expected Behavior**:

- Producer logs error: `"Internal decode errors: N"`
- Producer increments `stats.decode_errors`
- Producer backs off 10ms and retries
- Producer does not stop (allows recovery)
- Producer continues producing decoded frames after recovery

**Test Criteria**:

- ✅ Producer continues operation after internal decode error
- ✅ Error is logged to stderr
- ✅ Producer retries decode after backoff
- ✅ Producer resumes decoded frame production

**Test Files**: `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp`

---

### EH-003: Buffer Full (Backpressure)

**Condition**: `FrameRingBuffer->Push()` returns false (buffer is full).

**Expected Behavior**:

- Producer increments `buffer_full_count_`
- Producer backs off 10ms (`kProducerBackoffUs`)
- Producer retries push on next iteration
- Producer does not block or crash
- Producer continues producing decoded frames when buffer space available

**Test Criteria**:

- ✅ Producer backs off when buffer is full
- ✅ Producer retries push after backoff
- ✅ Producer does not block or crash
- ✅ Producer resumes decoded frame production when buffer space available

**Test Files**: `tests/test_decode.cpp` (BufferFullHandling)

---

### EH-004: Teardown Timeout

**Condition**: Buffer does not drain within `drain_timeout`.

**Expected Behavior**:

- Producer logs: `"Teardown timeout reached; forcing stop"`
- Producer calls `ForceStop()`
- Producer exits immediately (may drop decoded frames in buffer)

**Test Criteria**:

- ✅ Producer forces stop if timeout is reached
- ✅ Error is logged to stderr
- ✅ Producer exits within timeout + 100ms

**Test Files**: `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp`

---

## Test Coverage Requirements

All functional expectations (FE-001 through FE-012) must have corresponding test coverage.

### Test File Mapping

| Functional Expectation | Test File                                    | Test Case Name                    |
| ---------------------- | -------------------------------------------- | ---------------------------------- |
| FE-001                 | `tests/test_decode.cpp`                      | Construction, StartStop, CannotStartTwice, StopIdempotent, DestructorStopsProducer |
| FE-002                 | `tests/test_decode.cpp`                      | FillsBuffer                        |
| FE-003                 | `tests/test_decode.cpp`                      | FrameMetadata, FramePTSIncrementing |
| FE-004                 | `tests/test_decode.cpp`                      | FrameMetadata                      |
| FE-005                 | `tests/test_decode.cpp`                      | BufferFullHandling                 |
| FE-006                 | `tests/test_decode.cpp`                      | FillsBuffer                        |
| FE-007                 | `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp` | FE_007_InternalDecoderFallback |
| FE-008                 | `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp` | FE_008_InternalDecodeErrorRecovery |
| FE-009                 | `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp` | FE_009_EndOfFileHandling |
| FE-010                 | `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp` | FE_010_TeardownOperation |
| FE-011                 | `tests/test_decode.cpp`                      | BufferFullHandling, StartStop      |
| FE-012                 | `tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp` | FE_012_MasterClockAlignment |

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
- Pass in both stub mode and real decode mode (where applicable)
- Verify all frames are decoded (YUV420 format, not encoded packets)

---

## CI Enforcement

The following rules are enforced in continuous integration:

### Pre-Merge Requirements

1. ✅ All unit tests pass (`tests/test_decode.cpp`)
2. ✅ All contract tests pass (`tests/contracts/VideoFileProducer/VideoFileProducerContractTests.cpp`)
3. ✅ Code coverage ≥ 90% for VideoFileProducer domain
4. ✅ No memory leaks (valgrind or AddressSanitizer)
5. ✅ No undefined behavior (UB sanitizer)

### Performance Gates

1. ✅ Decode throughput ≥ `target_fps × 0.95` (5% tolerance)
2. ✅ Frame production latency < 33ms (p95) for 1080p30
3. ✅ Memory usage < 250 MB per channel
4. ✅ CPU usage < 30% per channel (1080p30)

### Quality Gates

1. ✅ No compiler warnings (treat warnings as errors)
2. ✅ Static analysis passes (clang-tidy, cppcheck)
3. ✅ Documentation is up-to-date (domain doc matches implementation)
4. ✅ All frames verified as decoded (YUV420 format, not encoded packets)

---

## See Also

- [Video File Producer Domain](../domain/VideoFileProducerDomain.md) — Domain model and architecture
- [Playout Engine Contract](PlayoutEngineContract.md) — Overall playout engine contract
- [Renderer Contract](RendererContract.md) — Frame consumption contract
- [Architecture Overview](../architecture/ArchitectureOverview.md) — System-wide architecture

---

## Design Change Notes (2025 Revision)

### Architectural Clarification

This contract document has been updated to reflect a clarified architectural decision:

**Core Rule**: A Producer in RetroVue outputs *decoded frames*, ready for the renderer. A Producer does *not* output encoded packets. Any decoding required is done inside the concrete Producer.

### Changes Made

1. **Unified Producer Model**: All FE tests now describe behavior of a full decode pipeline inside VideoFileProducer. There is no separate FFmpegDecoder stage. Instead, VideoFileProducer contains the FFmpeg-based decoder internally.

2. **Internal Decoder Subsystem**: References to a standalone FFmpegDecoder module have been replaced with:
   - "Internal decoder subsystem"
   - "Internal decode failure"
   - "Internal decode error recovery"

3. **Decoded Frame Production**: All FE tests (FE-002, FE-003, FE-004) now explicitly reflect decoded frame production:
   - FE-002: "Decoded Frame Production Rate"
   - FE-003: "Decoded Frame Metadata Validity"
   - FE-004: "Decoded Frame Format Validity"
   - All tests verify frames are decoded (YUV420 format, not encoded packets)

4. **Backpressure Behavior**: Backpressure behavior remains accurate, but now explicitly references decoded frames.

5. **EOF Behavior**: EOF behavior, teardown, and stats remain the same, but now reference internal decoder subsystem.

6. **Stub Mode**: Stub mode is still supported, but now explicitly confirms that stub mode simulates *decoded* frames (YUV420 format).

7. **Test Descriptions**: All test descriptions that previously referenced "decode stage separate from producer" now reflect integrated decode inside VideoFileProducer.

### Benefits

- **Consistent Architecture**: All tests align with the unified Producer model
- **Clear Contracts**: Tests explicitly verify decoded frame production
- **Simplified Testing**: No need to test separate decode stage
- **Better Encapsulation**: Internal decoder is implementation detail, not external contract
