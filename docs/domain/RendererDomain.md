# 🎬 Renderer Domain

_Related: [Playout Engine Domain](PlayoutEngineDomain.md) • [Renderer Contract](../contracts/RendererContract.md) • [Metrics and Timing Domain](MetricsAndTimingDomain.md) • [Phase 3 Plan](../milestones/Phase3_Plan.md)_

---

## 📋 Purpose & Role

The **Renderer** is the final stage in the RetroVue Playout Engine's media pipeline, responsible for consuming decoded video frames and preparing them for output. It acts as the bridge between the frame staging buffer and the ultimate destination—whether that's a display device, broadcast hardware, or validation pipeline.

### Pipeline Position

```
┌──────────────┐     ┌────────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   Video      │     │   Live         │     │   Frame      │     │    Frame     │     │   Renderer   │
│   Asset      │────▶│   Producer     │────▶│   Router     │────▶│  RingBuffer  │────▶│  (Consumer)  │
│  (MP4/MKV)   │     │ (Decode only)  │     │ (Pull+Push)  │     │  (60 frames) │     │              │
└──────────────┘     └────────────────┘     └──────────────┘     └──────────────┘     └──────────────┘
      Source         Exposes Pull API         Writer              Staging              Consumer
                                                                                        (ONLY reads
                                                                                         from buffer)

┌──────────────┐     ┌────────────────┐
│   Preview    │     │   Preview      │
│   Asset      │────▶│   Producer     │  (Shadow decode: decodes, frames available but not pulled)
│  (MP4/MKV)   │     │ (Decode only)  │
└──────────────┘     └────────────────┘
      Source         (Preview Slot - isolated until switch)
```

**Key Points**:
- **Live Producer**: Decodes frames and exposes them via pull API (e.g., `nextFrame()`)
- **Preview Producer**: Decodes frames in shadow mode (frames available but not pulled by router)
- **FrameRouter**: Pulls frames from live producer and writes to ring buffer (single writer)
- **Renderer**: Consumes frames from ring buffer ONLY (single consumer, never reads from producers)
- **Seamless Switch**: FrameRouter switches which producer it pulls from; ring buffer contains frames from both producers across switch boundary

### Core Responsibilities

1. **Frame Consumption**: Pop frames from the `FrameRingBuffer` in real-time (seamlessly across producer switches)
2. **Output Delivery**: Render frames to the appropriate destination (display, hardware, validation)
3. **Timing Control**: Maintain frame pacing based on metadata timestamps (continuous across switches)
4. **Statistics Tracking**: Monitor render performance (FPS, gaps, skips)
5. **Graceful Degradation**: Handle empty buffers and transient errors without crashing
6. **Format Change Handling**: Detect resolution/format changes mid-stream and handle as metadata change, NOT pipeline reset

### Why Separate Renderer?

The renderer exists as a distinct component (rather than being part of the decoder or buffer) for several architectural reasons:

- **Separation of Concerns**: Decoding and rendering are independent operations with different performance characteristics
- **Thread Safety**: Dedicated render thread prevents blocking the decode pipeline
- **Flexibility**: Multiple renderer implementations (headless, preview, SDI output) without changing upstream code
- **Testing**: Headless mode enables full pipeline testing without display hardware
- **Scalability**: Different channels can use different renderer types based on requirements

---

## 🎭 Renderer Variants

The RetroVue Playout Engine provides two renderer implementations, selected at channel initialization based on operational requirements.

### HeadlessRenderer

**Purpose**: Production playout without visual output

**Use Cases**:

- Broadcasting to SDI/NDI hardware
- Network streaming (RTMP, SRT)
- Background recording pipelines
- Automated testing and CI/CD
- Headless server deployments

**Characteristics**:

```
┌─────────────────────────────────────┐
│       HeadlessRenderer              │
│                                     │
│  ┌───────────────────────────────┐ │
│  │ Pop frame from buffer         │ │
│  └───────────────────────────────┘ │
│              ↓                      │
│  ┌───────────────────────────────┐ │
│  │ Validate metadata             │ │
│  │  • PTS present                │ │
│  │  • Data size correct          │ │
│  │  • Dimensions match           │ │
│  └───────────────────────────────┘ │
│              ↓                      │
│  ┌───────────────────────────────┐ │
│  │ Update statistics             │ │
│  │  • frames_rendered++          │ │
│  │  • Calculate frame gap        │ │
│  │  • Update render FPS          │ │
│  └───────────────────────────────┘ │
│              ↓                      │
│  ┌───────────────────────────────┐ │
│  │ Frame consumed                │ │
│  │ (no display output)           │ │
│  └───────────────────────────────┘ │
└─────────────────────────────────────┘

Performance: ~0.1ms per frame
Memory: <1MB
CPU: <1% per channel
Dependencies: None
```

**Key Features**:

- ✅ Zero external dependencies
- ✅ Ultra-low latency (<1ms per frame)
- ✅ Minimal CPU and memory footprint
- ✅ Always available (no compilation flags needed)
- ✅ Validates pipeline operation without display

### PreviewRenderer

**Purpose**: Visual debugging and development

**Use Cases**:

- Development and testing
- Operator preview monitors
- Quality assurance validation
- Frame-accurate inspection
- Demo and presentation scenarios

**Characteristics**:

```
┌─────────────────────────────────────┐
│       PreviewRenderer               │
│        (SDL2 Window)                │
│                                     │
│  ┌───────────────────────────────┐ │
│  │ Pop frame from buffer         │ │
│  └───────────────────────────────┘ │
│              ↓                      │
│  ┌───────────────────────────────┐ │
│  │ Poll SDL events               │ │
│  │  • Window close               │ │
│  │  • Resize                     │ │
│  │  • Key press                  │ │
│  └───────────────────────────────┘ │
│              ↓                      │
│  ┌───────────────────────────────┐ │
│  │ Update YUV texture            │ │
│  │  • Y plane (1920x1080)        │ │
│  │  • U plane (960x540)          │ │
│  │  • V plane (960x540)          │ │
│  └───────────────────────────────┘ │
│              ↓                      │
│  ┌───────────────────────────────┐ │
│  │ Render to window              │ │
│  │  • Clear                      │ │
│  │  • Copy texture               │ │
│  │  • Present (with VSYNC)       │ │
│  └───────────────────────────────┘ │
└─────────────────────────────────────┘

Performance: ~2-5ms per frame
Memory: ~10MB (SDL textures)
CPU: ~5% per channel
Dependencies: SDL2 library
```

**Key Features**:

- ✅ Real-time visual feedback
- ✅ Native YUV420 rendering (no conversion)
- ✅ VSYNC support for smooth playback
- ✅ Window controls (close, resize)
- ✅ Conditional compilation (falls back to headless if SDL2 unavailable)

### Variant Comparison

| Aspect           | HeadlessRenderer       | PreviewRenderer                    |
| ---------------- | ---------------------- | ---------------------------------- |
| **Output**       | None (validation only) | SDL2 window                        |
| **Latency**      | ~0.1ms                 | ~2-5ms                             |
| **CPU**          | <1%                    | ~5%                                |
| **Memory**       | <1MB                   | ~10MB                              |
| **Dependencies** | None                   | SDL2                               |
| **Use Case**     | Production             | Debug/QA                           |
| **Compilation**  | Always available       | Requires `RETROVUE_SDL2_AVAILABLE` |
| **Fallback**     | N/A                    | HeadlessRenderer if SDL2 missing   |

---

## ⚡ Design Principles

### 1. Real-Time Frame Pacing

The renderer operates in real-time, consuming frames as they become available without artificial delays (except for VSYNC in preview mode).

**Non-Blocking Consumption**:

```cpp
while (!stop_requested_) {
    Frame frame;
    if (!input_buffer_.Pop(frame)) {
        // Buffer empty - short sleep, don't block
        std::this_thread::sleep_for(5ms);
        stats_.frames_skipped++;
        continue;  // Never deadlock waiting for frames
    }

    RenderFrame(frame);  // Consume immediately
}
```

**Guarantees**:

- Renderer NEVER blocks indefinitely on buffer
- 5ms sleep prevents busy-wait CPU waste
- Tracks skipped frames when buffer empty
- Continues immediately when frames available

### 2. Low Latency

The renderer minimizes latency between frame availability and consumption to maintain real-time responsiveness.

**Latency Budget**:

```
Frame Available (buffer) → Pop → Render → Complete
     0ms                    <0.1ms  <5ms    <5.1ms total

Components:
  - Buffer pop:     <0.1ms  (lock-free atomic operation)
  - Frame render:   0.1-5ms (headless: 0.1ms, preview: 2-5ms)
  - Stats update:   <0.1ms  (atomic counters, EMA calculation)
```

**Optimization Techniques**:

- Lock-free buffer operations (atomic indices)
- Single render thread per channel (no context switching)
- Zero-copy frame data (reference semantics)
- Minimal statistics overhead (EMA instead of full history)

### 3. Graceful Degradation

The renderer handles adverse conditions without crashing or blocking the pipeline.

**Failure Modes & Responses**:

| Condition            | Detection               | Response                               | Impact               |
| -------------------- | ----------------------- | -------------------------------------- | -------------------- |
| **Buffer Empty**     | Pop returns false       | Sleep 5ms, increment `frames_skipped`  | Temporary output gap |
| **SDL2 Unavailable** | Initialize fails        | Fallback to HeadlessRenderer           | No visual output     |
| **Window Closed**    | SDL_QUIT event          | Set `stop_requested_`, exit gracefully | Renderer stops       |
| **Slow Render**      | Render time > threshold | Log warning, continue                  | Possible frame drops |

**Non-Fatal Renderer**:

```cpp
// In PlayoutService::StartChannel():
if (!worker->renderer->Start()) {
    std::cerr << "WARNING: Renderer failed, continuing without it" << std::endl;
    // Producer continues filling buffer
    // Pipeline operates, just no output consumption
}
```

The renderer is intentionally **non-critical**: decode and buffering continue even if rendering fails, ensuring maximum uptime.

---

## 🔗 Integration Points

### FrameRingBuffer

**Relationship**: Consumer

The renderer is the **sole consumer** of frames from the `FrameRingBuffer`. The ring buffer has a **single writer**: **FrameRouter** writes frames to the buffer. FrameRouter pulls frames from the live slot producer (preview slot producer runs in shadow decode mode and is not pulled by router until switched to live).

```
FrameRouter                        FrameRingBuffer                    FrameRenderer
┌─────────────────┐               ┌─────────────────┐               ┌──────────────────┐
│ Pull from Live  │               │ write_index (W) │               │  RenderLoop()    │
│ Producer        │───Write()────▶│                 │               │                  │
│ (nextFrame())   │               │  [Frame][Frame] │               │  while running:  │
│                 │               │  [Frame][Frame] │  ──Pop()────▶ │    frame = Pop() │
│ (Preview in     │               │  [Frame][Frame] │               │    RenderFrame() │
│  shadow mode)   │               │                 │               │    UpdateStats() │
└─────────────────┘               │ read_index (R)  │               │                  │
   Writer                          └─────────────────┘               └──────────────────┘
   (Pulls, then writes)                                              Consumer: Render
                                                                      (Read-only, never
                                                                       interacts with producers)
```

**Frame Flow Model (Pull-Based Architecture)**:

- **Live Producer**: **Only decodes when `nextFrame()` is called** - exposes pull-based API, does NOT push frames
- **Preview Producer**: Decodes frames in shadow mode (frames available but not pulled by router until switch)
- **FrameRouter**: **Owns the clock** - calls `producer->nextFrame()` and writes result to ring buffer
- **Renderer**: Consumes frames from ring buffer via `buffer.Pop(frame)` - **ONLY reads from buffer, never from producers**
- **Seamless Switch**: FrameRouter switches which producer it calls `nextFrame()` on; ring buffer persists
- **Ring Buffer Contains Frames from Both Producers**: During a switch, FrameRouter writes the final frame of the LIVE producer followed immediately by the first frame of the PREVIEW producer into the FrameRingBuffer, ensuring the sink receives an uninterrupted sequence with no glitch, no discontinuity, and no timestamp reset. This is the heart of the switching system.

**Why Pull Model?**
- **Engine owns the clock**: FrameRouter controls timing, ensuring deterministic frame delivery
- **No race conditions**: Producers decode only when requested, eliminating overlap/collision
- **Perfect for FFmpeg → MPEG-TS**: Stable, deterministic timing required for broadcast output
- **Seamless switching**: FrameRouter can atomically switch which producer it pulls from

**Contract**:

- Renderer calls `buffer.Pop(frame)` in tight loop
- Returns false when buffer empty (non-blocking)
- Never modifies buffer state except read index
- Single consumer guarantee (one render thread per buffer)
- **Single writer guarantee**: Only FrameRouter writes to buffer; FrameRouter pulls from live producer via `nextFrame()`; preview is isolated until switch
- **Renderer isolation**: Renderer ONLY reads from FrameRingBuffer; never interacts with producers or FrameRouter directly
- **Pull-based decoding**: Producers decode frames only when `nextFrame()` is called by FrameRouter; no autonomous pushing

---

## 🔄 Switch Boundary Behavior

This section documents the exact mechanics of seamless producer switching, which is the core of Phase 9's dual-producer architecture.

### Architecture: Pull-Based Model

The switching system uses a **pull-based model** where:

1. **Producers decode frames only** - they do NOT write to buffers
2. **FrameRouter pulls frames** from the active producer via `nextFrame()` API
3. **FrameRouter writes frames** into FrameRingBuffer at its own controlled pace
4. **Renderer reads from FrameRingBuffer only** - never interacts with producers

This model ensures:
- **Engine owns the clock**: FrameRouter controls timing deterministically
- **No race conditions**: Producers decode only when requested
- **Perfect synchronization**: FrameRouter can atomically switch which producer it pulls from
- **Stable for broadcast**: Deterministic timing required for FFmpeg → MPEG-TS output

### Switch Sequence

When `SwitchToLive(asset_id)` is called, the following sequence occurs:

```
Time    FrameRouter              Live Producer          Preview Producer         FrameRingBuffer
─────────────────────────────────────────────────────────────────────────────────────────────────
T0      Pull frame N             Decode frame N        (shadow decode)          [Frame N-1][Frame N]
        from live                (on nextFrame call)    (ready, aligned)         [Frame N+1]...
        
T1      Pull last frame          Decode frame N+1      (shadow decode)          [Frame N][Frame N+1]
        from live                (on nextFrame call)    (first frame cached)     [Frame N+2]...
        
T2      Switch producer          (stops)               (exits shadow mode)      [Frame N+1][Frame N+2]
        router.active_producer                         (ready for pull)         [Preview Frame 0]...
        = preview
        
T3      Pull first frame         (stopped)             Decode Preview Frame 0   [Frame N+2][Preview 0]
        from preview                                    (on nextFrame call)      [Preview Frame 1]...
        
T4      Continue pulling         (stopped)             Decode Preview Frame 1   [Preview 0][Preview 1]
        from preview                                    (on nextFrame call)      [Preview Frame 2]...
```

### Critical Requirements

1. **PTS Continuity**: 
   - Preview producer's first frame PTS = Live producer's last frame PTS + frame_duration
   - No PTS jumps, no resets to zero, no negative deltas
   - Achieved via `preview.alignPTS(live_last_pts + frame_duration)` before switch

2. **Frame Sequence in Buffer**:
   - FrameRingBuffer contains: `[last_live_frame][first_preview_frame][...]`
   - Both frames appear back-to-back with no gap
   - Renderer sees uninterrupted sequence: final frame from live producer immediately followed by first frame from preview producer

3. **No Visual Discontinuity**:
   - No glitch, no black frame, no stutter
   - No timestamp reset
   - No pipeline restart
   - Renderer continues reading seamlessly

4. **Atomic Switch**:
   - FrameRouter switches which producer it calls `nextFrame()` on atomically
   - Ring buffer is never flushed during switch
   - Switch completes within 100ms for seamless playout

### Implementation Details

**Producer API (Pull-Based)**:
```cpp
class IProducer {
    // Producer decodes frame ONLY when this is called
    // Returns frame with aligned PTS
    virtual Frame* nextFrame() = 0;
    
    // Shadow decode support
    virtual bool isShadowDecodeReady() const = 0;
    virtual void alignPTS(int64_t target_pts) = 0;
    virtual int64_t getNextPTS() const = 0;
};
```

**FrameRouter Logic**:
```cpp
void FrameRouter::RouteLoop() {
    while (running_) {
        // Pull frame from active producer (only decodes when called)
        Frame* frame = active_producer_->nextFrame();
        
        // Write to ring buffer at controlled pace
        if (ring_buffer_.Push(*frame)) {
            // Success - continue
        } else {
            // Buffer full - drop frame, increment counter
        }
        
        // Sleep until next frame tick (engine owns clock)
        WaitForFrameInterval();
    }
}
```

**Switch Logic**:
```cpp
void FrameRouter::SwitchToPreview(Producer* preview) {
    // 1. Pull last frame from live producer
    Frame* last_live = live_producer_->nextFrame();
    ring_buffer_.Push(*last_live);
    
    // 2. Align preview PTS
    int64_t target_pts = live_producer_->getNextPTS() + frame_duration;
    preview->alignPTS(target_pts);
    
    // 3. Atomic switch
    active_producer_ = preview;  // Now pulls from preview
    
    // 4. Pull first frame from preview (already aligned)
    Frame* first_preview = preview->nextFrame();
    ring_buffer_.Push(*first_preview);
    
    // 5. Stop live producer gracefully
    live_producer_->stop();
}
```

### Benefits of Pull Model

1. **Deterministic Timing**: Engine controls when frames are decoded and written
2. **No Race Conditions**: Producers cannot overlap or collide
3. **Perfect Synchronization**: FrameRouter can switch producers atomically
4. **Broadcast Quality**: Stable timing required for MPEG-TS output
5. **Seamless Switching**: Last live frame and first preview frame appear back-to-back in buffer

---

### MetricsExporter

**Relationship**: Statistics Source

The renderer reports performance metrics to the `MetricsExporter` for telemetry.

```cpp
// Renderer → Metrics flow
void FrameRenderer::UpdateStats(double render_time_ms, double frame_gap_ms) {
    stats_.frames_rendered++;
    stats_.frame_gap_ms = frame_gap_ms;
    stats_.average_render_time_ms = EMA(render_time_ms);
    stats_.current_render_fps = 1000.0 / frame_gap_ms;
}

// PlayoutService reads these stats:
const auto& render_stats = worker->renderer->GetStats();

// Updates channel metrics:
channel_metrics.buffer_depth_frames = worker->ring_buffer->Size();
channel_metrics.frame_gap_seconds = render_stats.frame_gap_ms / 1000.0;
```

**Metrics Provided**:

- `frames_rendered`: Total frames consumed
- `frames_skipped`: Buffer empty events
- `average_render_time_ms`: Rendering latency
- `current_render_fps`: Real-time render rate
- `frame_gap_ms`: Time between frames

### PlayoutService

**Relationship**: Lifecycle Manager

The `PlayoutService` creates, starts, updates, and stops renderers as part of channel lifecycle management.

```cpp
// Channel Lifecycle with Renderer
StartChannel(plan_handle):
    1. Create FrameRingBuffer
    2. Load initial asset into preview slot
    3. Activate preview as live (backward compatibility)
    4. Start live producer (decode thread)
    5. Create & start FrameRenderer (render thread)  ← Renderer enters here
    6. Update metrics

LoadPreview(path, asset_id):
    1. Load producer into preview slot (not started)
    2. Preview slot ready for switching

SwitchToLive(asset_id):
    1. Preview producer decodes until ready (shadow mode)
    2. Align preview PTS to continue from live: `preview_pts = live_last_pts + frame_duration`
    3. FrameRouter calls `live_producer->nextFrame()` to get last frame and writes to buffer (if needed)
    4. **FrameRouter switches producer**: `router.active_producer = preview` (atomic)
    5. Preview exits shadow mode, begins exposing frames via pull API
    6. Live producer stops decoding gracefully
    7. Move preview producer to live slot
    8. **Renderer continues seamlessly** - no reset, no flush, no timing change
    9. **FrameRingBuffer contains**: `[last_live_frame][first_preview_frame][...]` with continuous PTS
    10. Renderer sees uninterrupted sequence: final frame from live producer immediately followed by first frame from preview producer, with no glitch, no discontinuity, no timestamp reset

UpdatePlan(new_plan_handle):
    1. Stop FrameRenderer                           ← Stop consumer first
    2. Stop FrameProducer
    3. Clear buffer
    4. Restart FrameProducer with new plan
    5. Restart FrameRenderer                        ← Restart consumer

StopChannel():
    1. Stop FrameRenderer                           ← Stop consumer first
    2. Stop FrameProducer
    3. Remove metrics
```

**Integration Contract**:

- Renderer created AFTER buffer and FrameRouter
- Renderer stopped BEFORE FrameRouter (consumer before writer)
- Renderer failure non-fatal (warning logged, FrameRouter continues)
- Renderer lifecycle independent of decode errors
- **Renderer does NOT reset pipeline on producer switch** (continues reading seamlessly)
- Renderer must handle format/resolution changes mid-stream as metadata changes, not pipeline resets
- **Renderer ONLY reads from FrameRingBuffer** - never interacts with producers or FrameRouter directly
- **FrameRingBuffer contains frames from both producers across switch boundary**: During a switch, FrameRouter writes the final frame of the LIVE producer followed immediately by the first frame of the PREVIEW producer into the FrameRingBuffer, ensuring the renderer receives an uninterrupted sequence
- Renderer sees continuous frame stream: `[last_live_frame][first_preview_frame][...]` with no gap, no glitch, no discontinuity, no timestamp reset
- PTS continuity is maintained across switches (renderer sees continuous PTS sequence)
- No timing renegotiation, no pipeline restart, no discontinuity flags

---

## 🧵 Thread Model

### Dedicated Render Thread

Each channel's renderer runs in its own dedicated thread, independent of the decode thread and main gRPC service thread.

```
┌────────────────────────────────────────────────────────────────┐
│                       Process Space                            │
│                                                                │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ gRPC Thread  │  │ Decode Thread│  │ Render Thread│       │
│  │              │  │              │  │              │       │
│  │ StartChannel │  │ while (1) {  │  │ while (1) {  │       │
│  │ UpdatePlan   │  │   nextFrame()│  │   Pop()      │       │
│  │ StopChannel  │  │   Write()    │  │   Render()   │       │
│  │ GetVersion   │  │ }            │  │   Stats()    │       │
│  │              │  │              │  │ }            │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
│         │                  │                  │               │
│         └──────────────────┴──────────────────┘               │
│                           │                                    │
│                    Shared Resources                            │
│                  (protected by atomics)                        │
│                                                                │
│         ┌─────────────────────────────────────┐               │
│         │      FrameRingBuffer                │               │
│         │  • write_index (atomic)             │               │
│         │  • read_index (atomic)              │               │
│         │  • buffer (fixed array)             │               │
│         └─────────────────────────────────────┘               │
└────────────────────────────────────────────────────────────────┘
```

### Thread Characteristics

| Aspect           | Specification                                    |
| ---------------- | ------------------------------------------------ |
| **Creation**     | `std::thread` in `FrameRenderer::Start()`        |
| **Entry Point**  | `FrameRenderer::RenderLoop()` (protected method) |
| **Lifetime**     | Created on Start(), joined on Stop()             |
| **Priority**     | Normal (OS default)                              |
| **CPU Affinity** | None (OS scheduler decides)                      |
| **Stack Size**   | OS default (~1MB on most platforms)              |

### Synchronization Logic

The renderer uses minimal synchronization to maximize performance:

**Atomic Operations**:

```cpp
// Buffer read/write coordination (lock-free)
std::atomic<uint32_t> FrameRingBuffer::read_index_;
std::atomic<uint32_t> FrameRingBuffer::write_index_;

// Renderer state flags
std::atomic<bool> FrameRenderer::running_;
std::atomic<bool> FrameRenderer::stop_requested_;

// Statistics counters
std::atomic<uint64_t> FrameRenderer::frames_rendered_;
std::atomic<uint64_t> FrameRenderer::frames_skipped_;
```

**No Mutexes in Hot Path**:

- Buffer Pop() uses only atomic compare-and-swap
- Statistics use atomic increments
- No condition variables or locks in render loop
- Thread join only at shutdown (not in steady-state)

**Memory Ordering**:

```cpp
// Stop signal (sequentially consistent)
stop_requested_.store(true, std::memory_order_release);
// ... on render thread ...
if (stop_requested_.load(std::memory_order_acquire)) break;

// Statistics (relaxed for performance)
frames_rendered_.fetch_add(1, std::memory_order_relaxed);
```

### Timing Source

The renderer derives timing from **frame metadata** (PTS) rather than wall-clock time.

```cpp
// Frame timing from metadata
void FrameRenderer::RenderLoop() {
    auto last_frame_time = steady_clock::now();

    while (!stop_requested_) {
        Frame frame;
        if (buffer.Pop(frame)) {
            auto now = steady_clock::now();
            double frame_gap_ms = duration<ms>(now - last_frame_time);
            last_frame_time = now;

            // Render frame (timing controlled by buffer availability)
            RenderFrame(frame);

            // Stats based on actual intervals, not PTS
            UpdateStats(render_time_ms, frame_gap_ms);
        }
    }
}
```

**Timing Philosophy**:

- **Source of Truth**: Decoder PTS (from source media)
- **Pacing**: Buffer availability (back-pressure from renderer)
- **Measurement**: Wall-clock intervals (for stats)
- **No Sleep**: Renderer never artificially delays (except 5ms on empty buffer)

This design allows the renderer to adapt to varying decode rates and buffer conditions while maintaining accurate performance measurement.

---

## 🚀 Future Extensions

The current renderer implementation provides a solid foundation for advanced features planned for future phases.

### GPU Acceleration

**Goal**: Offload rendering to GPU for improved performance and capability.

**Potential Technologies**:

- **Vulkan**: Modern, cross-platform, explicit control
- **OpenGL**: Mature, widely supported, easier integration
- **DirectX 12** (Windows): Native Windows acceleration
- **Metal** (macOS): Native Apple silicon support

**Benefits**:

```
Current: CPU Render (~2-5ms per frame, 1080p)
    ↓
With GPU: GPU Render (~0.5-1ms per frame, 4K possible)
    ↓
Enables:
  • 4K/8K rendering
  • Multiple simultaneous channels
  • Real-time effects
  • Lower CPU utilization
```

**Architecture Sketch**:

```cpp
class GPURenderer : public FrameRenderer {
 protected:
    bool Initialize() override {
        // Initialize Vulkan/OpenGL context
        // Create textures for YUV planes
        // Set up render pipeline
    }

    void RenderFrame(const Frame& frame) override {
        // Upload frame to GPU texture
        // Execute shader pipeline
        // Present to window or framebuffer
    }
};
```

### Shader-Based Compositing

**Goal**: Real-time graphics overlay and compositing.

**Use Cases**:

- Station logos and bugs
- Lower-thirds and tickers
- Transition effects
- Multi-source compositing
- Real-time color grading

**Example Pipeline**:

```
Frame (YUV420)
    ↓
GPU Upload
    ↓
YUV → RGB Shader
    ↓
Composition Shader
  • Video layer (base)
  • Graphics layer (overlay)
  • Text layer (titles)
    ↓
RGB → YUV Shader (if needed)
    ↓
Output (Display or Encoder)
```

### Multi-Output Rendering

**Goal**: Single decode, multiple output destinations.

**Architecture**:

```
                ┌─────────────────┐
                │  FrameProducer  │
                │   (Decode 1x)   │
                └────────┬────────┘
                         │
                    Single Decode
                         │
                ┌────────▼─────────┐
                │  FrameRingBuffer │
                └────────┬─────────┘
                         │
            ┌────────────┼────────────┐
            │            │            │
     ┌──────▼─────┐ ┌───▼──────┐ ┌──▼───────┐
     │ SDI Output │ │ Preview  │ │ Network  │
     │  Renderer  │ │ Renderer │ │ Renderer │
     └────────────┘ └──────────┘ └──────────┘
```

**Benefits**:

- One decode feeds multiple outputs
- Reduced CPU/bandwidth usage
- Synchronized output streams
- Independent failure domains

### Hardware-Accelerated Output

**Goal**: Direct integration with broadcast hardware.

**Potential Targets**:

- **SDI Cards**: Blackmagic DeckLink, AJA Kona
- **NDI**: Network Device Interface (NewTek)
- **RTMP/SRT**: Direct network streaming
- **V4L2**: Linux video output devices

**Example Integration**:

```cpp
class SDIRenderer : public FrameRenderer {
 protected:
    void RenderFrame(const Frame& frame) override {
        // Convert YUV420 to SDI format (4:2:2, 10-bit)
        // Write to DeckLink card via SDK
        // Handle output timing (genlock)
    }
};
```

### Frame-Accurate Playout

**Goal**: Precise frame timing for broadcast compliance.

**Requirements**:

- Genlock synchronization
- Black burst or tri-level sync
- Sub-frame accurate output
- Jitter compensation

**Technique**:

```cpp
void FrameRenderer::RenderLoop() {
    // Wait for genlock pulse
    WaitForGenlockPulse();

    // Pop frame exactly on boundary
    Frame frame;
    buffer.Pop(frame);

    // Render with zero jitter
    RenderFrameImmediate(frame);
}
```

---

## 📚 Related Documentation

- **[Renderer Contract](../contracts/RendererContract.md)** — Detailed API contract and specifications
- **[Playout Engine Domain](PlayoutEngineDomain.md)** — Overall playout engine domain model
- **[Metrics and Timing Domain](MetricsAndTimingDomain.md)** — Time synchronization and telemetry
- **[Phase 3 Complete](../milestones/Phase3_Complete.md)** — Implementation completion summary
- **[README](../../README.md)** — Quick start and usage guide

---

## 🎯 Summary

The **Renderer Domain** completes the RetroVue Playout Engine's media pipeline, providing flexible, high-performance frame consumption with multiple output modes:

**Key Capabilities**:

- ✅ Dual renderer modes (headless production + preview debug)
- ✅ Real-time frame pacing with low latency
- ✅ Lock-free buffer integration
- ✅ Comprehensive performance statistics
- ✅ Graceful degradation under adverse conditions
- ✅ Non-critical operation (pipeline continues on renderer failure)

**Design Philosophy**:

- **Simplicity**: Minimal synchronization, clear responsibilities
- **Performance**: Lock-free, low-latency, efficient
- **Reliability**: Non-blocking, graceful errors, fail-safe
- **Extensibility**: Abstract interface, factory pattern, future-ready

**Future-Ready**:
The current architecture provides a solid foundation for advanced features including GPU acceleration, shader compositing, and hardware integration while maintaining backward compatibility and operational stability.

---

_Last Updated: 2025-11-08 | Phase 3 Part 2 Complete_
