// Repository: Retrovue-playout
// Component: MPEG-TS Playout Sink Implementation
// Purpose: Encodes decoded frames to H.264, muxes to MPEG-TS, streams over TCP.
// Copyright (c) 2025 RetroVue

#include "retrovue/sinks/mpegts/MpegTSPlayoutSink.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <iostream>

#ifdef RETROVUE_FFMPEG_AVAILABLE
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
}
#endif

namespace retrovue::sinks::mpegts {

// Constants
constexpr int64_t kLateThresholdUs = 33'333;  // 33ms at 30fps
constexpr int64_t kSoftWaitThresholdUs = 5'000;  // 5ms
constexpr int64_t kWaitFudgeUs = 500;  // 500µs
constexpr int64_t kSinkWaitUs = 10'000;  // 10ms

#ifdef RETROVUE_FFMPEG_AVAILABLE
// Encoder state for FFmpeg
struct MpegTSPlayoutSink::EncoderState {
  AVCodecContext* codec_ctx = nullptr;
  AVFrame* frame = nullptr;
  AVPacket* packet = nullptr;
  SwsContext* sws_ctx = nullptr;
  int width = 0;
  int height = 0;
};

// Muxer state for FFmpeg
struct MpegTSPlayoutSink::MuxerState {
  AVFormatContext* format_ctx = nullptr;
  AVStream* video_stream = nullptr;
  AVStream* audio_stream = nullptr;
  int64_t video_pts = 0;
  int64_t audio_pts = 0;
};
#endif

MpegTSPlayoutSink::MpegTSPlayoutSink(
    const SinkConfig& config,
    buffer::FrameRingBuffer& input_buffer,
    std::shared_ptr<timing::MasterClock> master_clock)
    : config_(config),
      buffer_(input_buffer),
      master_clock_(master_clock) {
}

MpegTSPlayoutSink::~MpegTSPlayoutSink() {
  stop();
}

bool MpegTSPlayoutSink::start() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  
  if (is_running_.load()) {
    return false;  // Already running
  }

  // Initialize socket
  if (!InitializeSocket()) {
    std::cerr << "[MpegTSPlayoutSink] Failed to initialize socket" << std::endl;
    return false;
  }

  // Start accept thread (only if not in stub mode)
  stop_requested_ = false;
  if (!config_.stub_mode) {
    accept_thread_ = std::thread(&MpegTSPlayoutSink::AcceptThread, this);

    // Wait for client connection (with timeout)
    auto start = std::chrono::steady_clock::now();
    while (!client_connected_.load() && 
           std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!client_connected_.load()) {
      stop();
      return false;
    }
  } else {
    // In stub mode, mark as connected immediately
    client_connected_ = true;
  }

  // Initialize encoder
  if (!InitializeEncoder()) {
    std::cerr << "[MpegTSPlayoutSink] Failed to initialize encoder" << std::endl;
    stop();
    return false;
  }

  // Initialize muxer
  if (!InitializeMuxer()) {
    std::cerr << "[MpegTSPlayoutSink] Failed to initialize muxer" << std::endl;
    stop();
    return false;
  }

  // Start worker thread
  is_running_ = true;
  worker_thread_ = std::thread(&MpegTSPlayoutSink::WorkerLoop, this);

  return true;
}

void MpegTSPlayoutSink::stop() {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (!is_running_.load() && !client_connected_.load()) {
      return;  // Already stopped
    }

    stop_requested_ = true;
  }

  // Wait for worker thread to exit
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }

  // Cleanup
  CleanupMuxer();
  CleanupEncoder();
  CleanupSocket();

  // Wait for accept thread
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }

  is_running_ = false;
  client_connected_ = false;
}

bool MpegTSPlayoutSink::isRunning() const {
  if (config_.stub_mode) {
    return is_running_.load();
  }
  return is_running_.load() && client_connected_.load();
}

void MpegTSPlayoutSink::WorkerLoop() {
  while (!stop_requested_.load()) {
    if (!client_connected_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // Query MasterClock for current time
    int64_t master_time_us = master_clock_->now_utc_us();

    // Peek next frame (non-destructive)
    const buffer::Frame* next_frame = buffer_.Peek();

    if (!next_frame) {
      // Buffer empty - apply underflow policy
      HandleBufferUnderflow(master_time_us);
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    // Compare frame PTS with master time
    int64_t frame_pts_us = next_frame->metadata.pts;
    
    if (frame_pts_us <= master_time_us) {
      // Frame PTS is due (or overdue) - pop and process it
      HandleBufferOverflow(master_time_us);
      
      buffer::Frame frame;
      if (buffer_.Pop(frame)) {
        ProcessFrame(frame, master_time_us);
      }
    } else {
      // Frame is early - calculate wait time
      int64_t wait_us = frame_pts_us - master_time_us;
      
      if (wait_us > kSoftWaitThresholdUs) {
        // Sleep for half the wait time to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::microseconds(wait_us / 2));
      }
      // Otherwise, continue loop (small sleep at end)
    }

    // Small sleep to avoid 100% CPU
    std::this_thread::sleep_for(std::chrono::microseconds(1000));
  }
}

void MpegTSPlayoutSink::ProcessFrame(const buffer::Frame& frame, int64_t master_time_us) {
  if (config_.stub_mode) {
    // Stub mode: just count frames
    frames_sent_++;
    return;
  }

#ifdef RETROVUE_FFMPEG_AVAILABLE
  // Real encoding mode
  if (!encoder_state_ || !muxer_state_) {
    encoding_errors_++;
    return;
  }

  // TODO: Implement full encoding pipeline
  // 1. Convert pixel format if needed (RGBA -> YUV420P)
  // 2. Encode frame to H.264
  // 3. Mux to MPEG-TS
  // 4. Send to TCP socket

  frames_sent_++;
#else
  // No FFmpeg available - stub mode
  frames_sent_++;
#endif
}

void MpegTSPlayoutSink::HandleBufferUnderflow(int64_t master_time_us) {
  buffer_empty_count_++;

  switch (config_.underflow_policy) {
    case UnderflowPolicy::FRAME_FREEZE:
      if (!last_encoded_frame_.empty()) {
        // Send last encoded frame again
        SendToSocket(last_encoded_frame_.data(), last_encoded_frame_.size());
      }
      break;
    case UnderflowPolicy::BLACK_FRAME:
      // TODO: Generate and send black frame
      break;
    case UnderflowPolicy::SKIP:
      // Do nothing
      break;
  }
}

void MpegTSPlayoutSink::HandleBufferOverflow(int64_t master_time_us) {
  // Drop all late frames (peek and drop if late)
  int dropped = 0;
  
  while (true) {
    const buffer::Frame* next_frame = buffer_.Peek();
    if (!next_frame) {
      break;  // Buffer empty
    }
    
    int64_t frame_pts_us = next_frame->metadata.pts;
    int64_t gap_us = master_time_us - frame_pts_us;
    
    if (gap_us > kLateThresholdUs) {
      // Frame is late - drop it
      buffer::Frame frame;
      buffer_.Pop(frame);  // Remove late frame
      dropped++;
      late_frames_++;
    } else {
      // Next frame is not late - stop dropping
      break;
    }
  }
  
  frames_dropped_ += dropped;
}

bool MpegTSPlayoutSink::InitializeEncoder() {
  if (config_.stub_mode) {
    return true;  // No encoder needed in stub mode
  }

#ifdef RETROVUE_FFMPEG_AVAILABLE
  encoder_state_ = std::make_unique<EncoderState>();
  
  // Find H.264 encoder
  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!codec) {
    std::cerr << "[MpegTSPlayoutSink] H.264 encoder not found" << std::endl;
    return false;
  }

  encoder_state_->codec_ctx = avcodec_alloc_context3(codec);
  if (!encoder_state_->codec_ctx) {
    return false;
  }

  // Set encoder parameters
  encoder_state_->codec_ctx->bit_rate = config_.bitrate;
  encoder_state_->codec_ctx->width = 1920;  // TODO: Get from frame
  encoder_state_->codec_ctx->height = 1080;  // TODO: Get from frame
  encoder_state_->codec_ctx->time_base = {1, 90000};  // MPEG-TS timebase
  encoder_state_->codec_ctx->framerate = {static_cast<int>(config_.target_fps), 1};
  encoder_state_->codec_ctx->gop_size = config_.gop_size;
  encoder_state_->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  encoder_state_->codec_ctx->max_b_frames = 0;  // No B-frames for determinism

  // Open codec
  int ret = avcodec_open2(encoder_state_->codec_ctx, codec, nullptr);
  if (ret < 0) {
    char errbuf[256];
    av_strerror(ret, errbuf, sizeof(errbuf));
    std::cerr << "[MpegTSPlayoutSink] Failed to open encoder: " << errbuf << std::endl;
    return false;
  }

  encoder_state_->frame = av_frame_alloc();
  encoder_state_->packet = av_packet_alloc();
  
  encoder_state_->width = encoder_state_->codec_ctx->width;
  encoder_state_->height = encoder_state_->codec_ctx->height;

  return true;
#else
  return false;  // FFmpeg not available
#endif
}

void MpegTSPlayoutSink::CleanupEncoder() {
#ifdef RETROVUE_FFMPEG_AVAILABLE
  if (encoder_state_) {
    if (encoder_state_->sws_ctx) {
      sws_freeContext(encoder_state_->sws_ctx);
    }
    if (encoder_state_->frame) {
      av_frame_free(&encoder_state_->frame);
    }
    if (encoder_state_->packet) {
      av_packet_free(&encoder_state_->packet);
    }
    if (encoder_state_->codec_ctx) {
      avcodec_free_context(&encoder_state_->codec_ctx);
    }
    encoder_state_.reset();
  }
#endif
}

bool MpegTSPlayoutSink::InitializeMuxer() {
  if (config_.stub_mode) {
    return true;  // No muxer needed in stub mode
  }

#ifdef RETROVUE_FFMPEG_AVAILABLE
  muxer_state_ = std::make_unique<MuxerState>();

  // Allocate output context for MPEG-TS
  int ret = avformat_alloc_output_context2(
      &muxer_state_->format_ctx, nullptr, "mpegts", nullptr);
  if (ret < 0 || !muxer_state_->format_ctx) {
    char errbuf[256];
    av_strerror(ret, errbuf, sizeof(errbuf));
    std::cerr << "[MpegTSPlayoutSink] Failed to allocate muxer context: " << errbuf << std::endl;
    return false;
  }

  // Set non-blocking flag
  muxer_state_->format_ctx->flags |= AVFMT_FLAG_NONBLOCK;

  // Add video stream (encoder must be initialized first)
  if (!encoder_state_ || !encoder_state_->codec_ctx) {
    return false;  // Encoder not initialized
  }

  muxer_state_->video_stream = avformat_new_stream(
      muxer_state_->format_ctx, encoder_state_->codec_ctx->codec);
  if (!muxer_state_->video_stream) {
    return false;
  }

  avcodec_parameters_from_context(
      muxer_state_->video_stream->codecpar, encoder_state_->codec_ctx);

  // TODO: Add audio stream if enable_audio is true

  // Open output (TCP socket)
  // Note: We'll write directly to socket, not use avio
  // For now, we'll set up the format context

  return true;
#else
  return false;  // FFmpeg not available
#endif
}

void MpegTSPlayoutSink::CleanupMuxer() {
#ifdef RETROVUE_FFMPEG_AVAILABLE
  if (muxer_state_) {
    if (muxer_state_->format_ctx) {
      avformat_free_context(muxer_state_->format_ctx);
    }
    muxer_state_.reset();
  }
#endif
}

bool MpegTSPlayoutSink::InitializeSocket() {
  if (config_.stub_mode) {
    // In stub mode, skip socket initialization
    return true;
  }

  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    return false;
  }

  // Set socket options
  int opt = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Bind to port
  struct sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(config_.port);

  if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  // Listen
  if (listen(listen_fd_, 1) < 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  // Set non-blocking
  int flags = fcntl(listen_fd_, F_GETFL, 0);
  fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);

  return true;
}

void MpegTSPlayoutSink::CleanupSocket() {
  if (client_fd_ >= 0) {
    close(client_fd_);
    client_fd_ = -1;
  }
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    listen_fd_ = -1;
  }
  client_connected_ = false;
}

void MpegTSPlayoutSink::AcceptThread() {
  while (!stop_requested_.load() && listen_fd_ >= 0) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    client_fd_ = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
    
    if (client_fd_ >= 0) {
      // Set non-blocking
      int flags = fcntl(client_fd_, F_GETFL, 0);
      fcntl(client_fd_, F_SETFL, flags | O_NONBLOCK);
      
      client_connected_ = true;
      
      // Wait for disconnect
      char buf[1];
      while (client_connected_.load() && !stop_requested_.load()) {
        ssize_t n = recv(client_fd_, buf, 1, MSG_PEEK);
        if (n <= 0) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Client disconnected
            break;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      
      // Client disconnected - tear down muxer
      CleanupMuxer();
      close(client_fd_);
      client_fd_ = -1;
      client_connected_ = false;
      
      // Wait for new connection
      if (!stop_requested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        break;  // Error
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}

bool MpegTSPlayoutSink::SendToSocket(const uint8_t* data, size_t size) {
  if (client_fd_ < 0 || !client_connected_.load()) {
    return false;
  }

  ssize_t sent = send(client_fd_, data, size, MSG_NOSIGNAL);
  
  if (sent < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // Socket buffer full - drop frame
      network_errors_++;
      return false;
    } else {
      // Error - client may have disconnected
      network_errors_++;
      client_connected_ = false;
      return false;
    }
  }

  return true;
}

}  // namespace retrovue::sinks::mpegts

