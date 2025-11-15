#include "retrovue/runtime/PlayoutControlStateMachine.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "retrovue/producers/video_file/VideoFileProducer.h"

namespace retrovue::runtime
{

  namespace
  {

    double MicrosecondsToMilliseconds(int64_t delta_us)
    {
      return static_cast<double>(delta_us) / 1'000.0;
    }

  } // namespace

  PlayoutControlStateMachine::PlayoutControlStateMachine()
      : state_(State::kIdle),
        current_pts_us_(0),
        illegal_transition_total_(0),
        latency_violation_total_(0),
        timeout_total_(0),
        queue_overflow_total_(0),
        recover_total_(0),
        consistency_failure_total_(0),
        late_seek_total_(0) {}

  bool PlayoutControlStateMachine::BeginSession(const std::string &command_id,
                                                int64_t request_utc_us)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!RegisterCommandLocked(command_id))
    {
      return true; // Duplicate command is acknowledged.
    }

    if (state_ != State::kIdle)
    {
      RecordIllegalTransitionLocked(state_, State::kBuffering);
      return false;
    }

    TransitionLocked(State::kBuffering, request_utc_us);
    return true;
  }

  bool PlayoutControlStateMachine::Pause(const std::string &command_id,
                                         int64_t request_utc_us,
                                         int64_t effective_utc_us,
                                         double boundary_deviation_ms)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!RegisterCommandLocked(command_id))
    {
      return true;
    }

    if (state_ != State::kPlaying)
    {
      RecordIllegalTransitionLocked(state_, State::kPaused);
      return false;
    }

    const double latency_ms =
        MicrosecondsToMilliseconds(effective_utc_us - request_utc_us);
    RecordLatencyLocked(pause_latencies_ms_, latency_ms);
    pause_deviation_ms_.push_back(boundary_deviation_ms);

    if (latency_ms > kPauseLatencyThresholdMs)
    {
      ++latency_violation_total_;
    }

    TransitionLocked(State::kPaused, effective_utc_us);
    return true;
  }

  bool PlayoutControlStateMachine::Resume(const std::string &command_id,
                                          int64_t request_utc_us,
                                          int64_t effective_utc_us)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!RegisterCommandLocked(command_id))
    {
      return true;
    }

    if (state_ != State::kPaused)
    {
      RecordIllegalTransitionLocked(state_, State::kPlaying);
      return false;
    }

    const double latency_ms =
        MicrosecondsToMilliseconds(effective_utc_us - request_utc_us);
    RecordLatencyLocked(resume_latencies_ms_, latency_ms);
    if (latency_ms > kResumeLatencyThresholdMs)
    {
      ++latency_violation_total_;
    }

    TransitionLocked(State::kPlaying, effective_utc_us);
    return true;
  }

  bool PlayoutControlStateMachine::Seek(const std::string &command_id,
                                        int64_t request_utc_us,
                                        int64_t target_pts_us,
                                        int64_t effective_utc_us)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!RegisterCommandLocked(command_id))
    {
      return true;
    }

    if (target_pts_us < current_pts_us_)
    {
      ++late_seek_total_;
      return false;
    }

    if (state_ != State::kPlaying && state_ != State::kPaused)
    {
      RecordIllegalTransitionLocked(state_, State::kBuffering);
      return false;
    }

    const double latency_ms =
        MicrosecondsToMilliseconds(effective_utc_us - request_utc_us);
    RecordLatencyLocked(seek_latencies_ms_, latency_ms);
    if (latency_ms > kSeekLatencyThresholdMs)
    {
      ++latency_violation_total_;
    }

    TransitionLocked(State::kBuffering, request_utc_us);
    TransitionLocked(State::kReady, effective_utc_us);
    TransitionLocked(State::kPlaying, effective_utc_us);
    current_pts_us_ = target_pts_us;
    return true;
  }

  bool PlayoutControlStateMachine::Stop(const std::string &command_id,
                                        int64_t request_utc_us,
                                        int64_t effective_utc_us)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!RegisterCommandLocked(command_id))
    {
      return true;
    }

    if (state_ == State::kIdle)
    {
      RecordIllegalTransitionLocked(state_, State::kStopping);
      return false;
    }

    const double latency_ms =
        MicrosecondsToMilliseconds(effective_utc_us - request_utc_us);
    RecordLatencyLocked(stop_latencies_ms_, latency_ms);
    if (latency_ms > kStopLatencyThresholdMs)
    {
      ++latency_violation_total_;
    }

    TransitionLocked(State::kStopping, request_utc_us);
    TransitionLocked(State::kIdle, effective_utc_us);
    return true;
  }

  bool PlayoutControlStateMachine::Recover(const std::string &command_id,
                                           int64_t request_utc_us)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!RegisterCommandLocked(command_id))
    {
      return true;
    }

    if (state_ != State::kError)
    {
      RecordIllegalTransitionLocked(state_, State::kBuffering);
      return false;
    }

    TransitionLocked(State::kBuffering, request_utc_us);
    ++recover_total_;
    return true;
  }

  void PlayoutControlStateMachine::OnBufferDepth(std::size_t depth,
                                                 std::size_t capacity,
                                                 int64_t event_utc_us)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (capacity == 0)
    {
      return;
    }

    if (state_ == State::kBuffering && depth >= kReadinessThresholdFrames)
    {
      TransitionLocked(State::kReady, event_utc_us);
      TransitionLocked(State::kPlaying, event_utc_us);
    }
    else if (state_ == State::kPlaying && depth == 0)
    {
      TransitionLocked(State::kBuffering, event_utc_us);
    }
  }

  void PlayoutControlStateMachine::OnBackPressureEvent(
      OrchestrationLoop::BackPressureEvent event,
      int64_t event_utc_us)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event == OrchestrationLoop::BackPressureEvent::kUnderrun)
    {
      if (state_ == State::kPlaying)
      {
        TransitionLocked(State::kBuffering, event_utc_us);
      }
    }
    else if (event == OrchestrationLoop::BackPressureEvent::kOverrun)
    {
      // Currently treated as informational; no state change but recorded.
      ++queue_overflow_total_;
    }
  }

  void PlayoutControlStateMachine::OnBackPressureCleared(int64_t event_utc_us)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == State::kBuffering)
    {
      TransitionLocked(State::kReady, event_utc_us);
      TransitionLocked(State::kPlaying, event_utc_us);
    }
  }

  void PlayoutControlStateMachine::OnExternalTimeout(int64_t event_utc_us)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++timeout_total_;
    TransitionLocked(State::kError, event_utc_us);
  }

  void PlayoutControlStateMachine::OnQueueOverflow()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++queue_overflow_total_;
  }

  PlayoutControlStateMachine::State PlayoutControlStateMachine::state() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

  PlayoutControlStateMachine::MetricsSnapshot PlayoutControlStateMachine::Snapshot()
      const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    MetricsSnapshot snapshot;
    snapshot.transitions = transitions_;
    snapshot.illegal_transition_total = illegal_transition_total_;
    snapshot.latency_violation_total = latency_violation_total_;
    snapshot.timeout_total = timeout_total_;
    snapshot.queue_overflow_total = queue_overflow_total_;
    snapshot.recover_total = recover_total_;
    snapshot.consistency_failure_total = consistency_failure_total_;
    snapshot.late_seek_total = late_seek_total_;
    snapshot.pause_latency_p95_ms = PercentileLocked(pause_latencies_ms_, 0.95);
    snapshot.resume_latency_p95_ms = PercentileLocked(resume_latencies_ms_, 0.95);
    snapshot.seek_latency_p95_ms = PercentileLocked(seek_latencies_ms_, 0.95);
    snapshot.stop_latency_p95_ms = PercentileLocked(stop_latencies_ms_, 0.95);
    snapshot.pause_deviation_p95_ms = PercentileLocked(pause_deviation_ms_, 0.95);
    if (!pause_latencies_ms_.empty())
    {
      snapshot.last_pause_latency_ms = pause_latencies_ms_.back();
    }
    if (!resume_latencies_ms_.empty())
    {
      snapshot.last_resume_latency_ms = resume_latencies_ms_.back();
    }
    if (!seek_latencies_ms_.empty())
    {
      snapshot.last_seek_latency_ms = seek_latencies_ms_.back();
    }
    if (!stop_latencies_ms_.empty())
    {
      snapshot.last_stop_latency_ms = stop_latencies_ms_.back();
    }
    if (!pause_deviation_ms_.empty())
    {
      snapshot.last_pause_deviation_ms = pause_deviation_ms_.back();
    }
    snapshot.state = state_;
    return snapshot;
  }

  void PlayoutControlStateMachine::TransitionLocked(State to, int64_t event_utc_us)
  {
    if (state_ == to)
    {
      return;
    }
    RecordTransitionLocked(state_, to);
    state_ = to;
    (void)event_utc_us;
  }

  void PlayoutControlStateMachine::RecordTransitionLocked(State from, State to)
  {
    transitions_[{from, to}]++;
  }

  void PlayoutControlStateMachine::RecordLatencyLocked(std::vector<double> &samples,
                                                       double value_ms)
  {
    samples.push_back(value_ms);
  }

  double PlayoutControlStateMachine::PercentileLocked(
      const std::vector<double> &samples,
      double percentile) const
  {
    if (samples.empty())
    {
      return 0.0;
    }

    std::vector<double> copy = samples;
    const double rank = percentile * static_cast<double>(copy.size() - 1);
    const std::size_t lower_index = static_cast<std::size_t>(std::floor(rank));
    const std::size_t upper_index = static_cast<std::size_t>(std::ceil(rank));
    std::nth_element(copy.begin(), copy.begin() + lower_index, copy.end());
    const double lower = copy[lower_index];
    if (upper_index == lower_index)
    {
      return lower;
    }
    std::nth_element(copy.begin(), copy.begin() + upper_index, copy.end());
    const double upper = copy[upper_index];
    const double fraction = rank - static_cast<double>(lower_index);
    return lower + (upper - lower) * fraction;
  }

  bool PlayoutControlStateMachine::RegisterCommandLocked(
      const std::string &command_id)
  {
    if (command_id.empty())
    {
      return true;
    }
    const auto [it, inserted] = processed_commands_.emplace(command_id, 1);
    if (!inserted)
    {
      return false;
    }
    return true;
  }

  void PlayoutControlStateMachine::RecordIllegalTransitionLocked(State from,
                                                                 State attempted_to)
  {
    ++illegal_transition_total_;
    transitions_[{from, attempted_to}]++;
  }

  void PlayoutControlStateMachine::setProducerFactory(ProducerFactory factory)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    producer_factory_ = std::move(factory);
  }

  bool PlayoutControlStateMachine::loadPreviewAsset(const std::string& path,
                                                   const std::string& assetId,
                                                   buffer::FrameRingBuffer& ringBuffer,
                                                   std::shared_ptr<timing::MasterClock> clock)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Destroy any existing preview producer before loading a new one
    // This resets shadow decode readiness state (ProducerSlot::reset() stops producer)
    if (previewSlot.loaded)
    {
      // Reset shadow decode readiness when clearing preview slot
      if (previewSlot.producer)
      {
        auto* video_producer = dynamic_cast<producers::video_file::VideoFileProducer*>(
            previewSlot.producer.get());
        if (video_producer)
        {
          // Reset shadow decode state before destroying producer
          video_producer->SetShadowDecodeMode(false); // Clear readiness state
        }
      }
      previewSlot.reset();
    }

    // Check if factory is set
    if (!producer_factory_)
    {
      std::cerr << "[PlayoutControlStateMachine] Producer factory not set" << std::endl;
      return false;
    }

    // Create producer using factory
    auto producer = producer_factory_(path, assetId, ringBuffer, clock);
    if (!producer)
    {
      std::cerr << "[PlayoutControlStateMachine] Failed to create producer for path: " << path << std::endl;
      return false;
    }

    // Enable shadow decode mode for preview producer
    auto* video_producer = dynamic_cast<producers::video_file::VideoFileProducer*>(producer.get());
    if (video_producer)
    {
      video_producer->SetShadowDecodeMode(true);
      std::cout << "[PlayoutControlStateMachine] Enabled shadow decode mode for preview producer" << std::endl;
    }

    // Start producer in shadow mode (decodes but doesn't write to buffer)
    if (!producer->start())
    {
      std::cerr << "[PlayoutControlStateMachine] Failed to start preview producer" << std::endl;
      return false;
    }

    // Store in preview slot
    previewSlot.producer = std::move(producer);
    previewSlot.loaded = true;
    previewSlot.asset_id = assetId;
    previewSlot.file_path = path;

    std::cout << "[PlayoutControlStateMachine] Loaded preview asset: " << assetId 
              << " from path: " << path << " (shadow decode mode)" << std::endl;
    return true;
  }

  bool PlayoutControlStateMachine::activatePreviewAsLive(renderer::FrameRenderer* renderer)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if preview is loaded
    if (!previewSlot.loaded || !previewSlot.producer)
    {
      std::cerr << "[PlayoutControlStateMachine] No preview asset loaded to activate" << std::endl;
      return false;
    }

    auto* preview_video_producer = dynamic_cast<producers::video_file::VideoFileProducer*>(previewSlot.producer.get());
    if (!preview_video_producer)
    {
      std::cerr << "[PlayoutControlStateMachine] Preview producer is not a VideoFileProducer" << std::endl;
      return false;
    }

    // Check if shadow decode is ready (first frame decoded and cached)
    if (!preview_video_producer->IsShadowDecodeReady())
    {
      std::cerr << "[PlayoutControlStateMachine] Preview producer shadow decode not ready" << std::endl;
      return false;
    }

    std::cout << "[PlayoutControlStateMachine] Seamless switch: preview to live..." << std::endl;

    // Seamless Switch Algorithm:
    // 1. Get last PTS from live producer
    int64_t last_live_pts = 0;
    int64_t frame_duration_us = 0;
    if (liveSlot.loaded && liveSlot.producer && liveSlot.producer->isRunning())
    {
      auto* live_video_producer = dynamic_cast<producers::video_file::VideoFileProducer*>(liveSlot.producer.get());
      if (live_video_producer)
      {
        last_live_pts = live_video_producer->GetNextPTS();
        // Calculate frame duration (assuming 30fps for now, should get from producer config)
        frame_duration_us = 33'366; // ~30fps in microseconds
        std::cout << "[PlayoutControlStateMachine] Live producer last PTS: " << last_live_pts << std::endl;
      }
    }

    // 2. Align preview producer PTS to continue from live
    int64_t target_pts = last_live_pts + frame_duration_us;
    preview_video_producer->AlignPTS(target_pts);
    std::cout << "[PlayoutControlStateMachine] Aligned preview PTS to: " << target_pts << std::endl;

    // 3. Exit shadow mode (preview producer will now write to buffer)
    preview_video_producer->SetShadowDecodeMode(false);
    std::cout << "[PlayoutControlStateMachine] Preview producer exited shadow mode" << std::endl;

    // 4. Stop the live producer gracefully (wind down)
    if (liveSlot.loaded && liveSlot.producer && liveSlot.producer->isRunning())
    {
      std::cout << "[PlayoutControlStateMachine] Stopping current live producer gracefully" << std::endl;
      liveSlot.producer->stop();
    }

    // 5. Move preview → live (ring buffer writer swap is implicit - preview now writes)
    liveSlot.producer = std::move(previewSlot.producer);
    liveSlot.loaded = true;
    liveSlot.asset_id = previewSlot.asset_id;
    liveSlot.file_path = previewSlot.file_path;

    // 6. Reset preview slot as empty
    previewSlot.reset();

    // Note: Renderer does NOT reset - it continues reading seamlessly
    // Ring buffer persists through switch with continuous frame stream

    std::cout << "[PlayoutControlStateMachine] Seamless switch complete. Asset: " 
              << liveSlot.asset_id << " (PTS continuous)" << std::endl;
    return true;
  }

  const ProducerSlot& PlayoutControlStateMachine::getPreviewSlot() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return previewSlot;
  }

  const ProducerSlot& PlayoutControlStateMachine::getLiveSlot() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return liveSlot;
  }

} // namespace retrovue::runtime
