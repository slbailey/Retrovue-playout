// Repository: Retrovue-playout
// Component: MPEG-TS Playout Sink
// Purpose: Encodes decoded frames to H.264, muxes to MPEG-TS, streams over TCP.
// Copyright (c) 2025 RetroVue

#ifndef RETROVUE_SINKS_MPEGTS_MPEGTS_PLAYOUT_SINK_H_
#define RETROVUE_SINKS_MPEGTS_MPEGTS_PLAYOUT_SINK_H_

#include "retrovue/sinks/IPlayoutSink.h"
#include "retrovue/sinks/mpegts/SinkConfig.h"
#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/timing/MasterClock.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdint>

namespace retrovue::sinks::mpegts {

// MpegTSPlayoutSink consumes decoded frames from FrameRingBuffer,
// encodes them to H.264, muxes to MPEG-TS, and streams over TCP socket.
// The sink owns its timing loop and continuously queries MasterClock
// to determine when to output frames.
class MpegTSPlayoutSink : public IPlayoutSink {
 public:
  MpegTSPlayoutSink(
      const SinkConfig& config,
      buffer::FrameRingBuffer& input_buffer,
      std::shared_ptr<timing::MasterClock> master_clock
  );
  
  ~MpegTSPlayoutSink() override;

  // IPlayoutSink interface
  bool start() override;
  void stop() override;
  bool isRunning() const override;

  // Statistics
  uint64_t getFramesSent() const { return frames_sent_.load(); }
  uint64_t getFramesDropped() const { return frames_dropped_.load(); }
  uint64_t getLateFrames() const { return late_frames_.load(); }
  uint64_t getEncodingErrors() const { return encoding_errors_.load(); }
  uint64_t getNetworkErrors() const { return network_errors_.load(); }
  uint64_t getBufferEmptyCount() const { return buffer_empty_count_.load(); }

 private:
  // Worker thread that owns the timing loop
  void WorkerLoop();

  // Process a single frame (encode, mux, send)
  void ProcessFrame(const buffer::Frame& frame, int64_t master_time_us);

  // Handle buffer underflow
  void HandleBufferUnderflow(int64_t master_time_us);

  // Handle buffer overflow (drop late frames)
  void HandleBufferOverflow(int64_t master_time_us);

  // Initialize encoder (stub or real)
  bool InitializeEncoder();
  void CleanupEncoder();

  // Initialize muxer (stub or real)
  bool InitializeMuxer();
  void CleanupMuxer();

  // Initialize TCP socket
  bool InitializeSocket();
  void CleanupSocket();

  // Send encoded data to TCP socket (non-blocking)
  bool SendToSocket(const uint8_t* data, size_t size);

  // Configuration
  SinkConfig config_;
  buffer::FrameRingBuffer& buffer_;
  std::shared_ptr<timing::MasterClock> master_clock_;

  // Threading
  std::atomic<bool> is_running_{false};
  std::atomic<bool> stop_requested_{false};
  std::thread worker_thread_;
  std::mutex state_mutex_;

  // TCP socket
  int listen_fd_{-1};
  int client_fd_{-1};
  std::atomic<bool> client_connected_{false};
  std::thread accept_thread_;

  // Encoder/Muxer state (forward declarations for FFmpeg types)
  struct EncoderState;
  struct MuxerState;
  std::unique_ptr<EncoderState> encoder_state_;
  std::unique_ptr<MuxerState> muxer_state_;

  // Last encoded frame (for frame freeze)
  std::vector<uint8_t> last_encoded_frame_;

  // Statistics
  std::atomic<uint64_t> frames_sent_{0};
  std::atomic<uint64_t> frames_dropped_{0};
  std::atomic<uint64_t> late_frames_{0};
  std::atomic<uint64_t> encoding_errors_{0};
  std::atomic<uint64_t> network_errors_{0};
  std::atomic<uint64_t> buffer_empty_count_{0};

  // Accept thread function
  void AcceptThread();
};

}  // namespace retrovue::sinks::mpegts

#endif  // RETROVUE_SINKS_MPEGTS_MPEGTS_PLAYOUT_SINK_H_

