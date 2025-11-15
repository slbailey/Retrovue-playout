#ifndef RETROVUE_RUNTIME_PLAYOUT_CONTROL_STATE_MACHINE_H_
#define RETROVUE_RUNTIME_PLAYOUT_CONTROL_STATE_MACHINE_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mutex>

#include <functional>

#include "retrovue/runtime/OrchestrationLoop.h"
#include "retrovue/runtime/ProducerSlot.h"

namespace retrovue {
namespace buffer {
  class FrameRingBuffer;
}
namespace timing {
  class MasterClock;
}
namespace producers {
  class IProducer;
}
namespace renderer {
  class FrameRenderer;
}
}

namespace retrovue::runtime {

class PlayoutControlStateMachine {
 public:
  enum class State {
    kIdle = 0,
    kBuffering = 1,
    kReady = 2,
    kPlaying = 3,
    kPaused = 4,
    kStopping = 5,
    kError = 6,
  };

  struct MetricsSnapshot {
    std::map<std::pair<State, State>, uint64_t> transitions;
    uint64_t illegal_transition_total = 0;
    uint64_t latency_violation_total = 0;
    uint64_t timeout_total = 0;
    uint64_t queue_overflow_total = 0;
    uint64_t recover_total = 0;
    uint64_t consistency_failure_total = 0;
    uint64_t late_seek_total = 0;
    double pause_latency_p95_ms = 0.0;
    double resume_latency_p95_ms = 0.0;
    double seek_latency_p95_ms = 0.0;
    double stop_latency_p95_ms = 0.0;
    double pause_deviation_p95_ms = 0.0;
    double last_pause_latency_ms = 0.0;
    double last_resume_latency_ms = 0.0;
    double last_seek_latency_ms = 0.0;
    double last_stop_latency_ms = 0.0;
    double last_pause_deviation_ms = 0.0;
    State state = State::kIdle;
  };

  PlayoutControlStateMachine();

  PlayoutControlStateMachine(const PlayoutControlStateMachine&) = delete;
  PlayoutControlStateMachine& operator=(const PlayoutControlStateMachine&) = delete;

  bool BeginSession(const std::string& command_id, int64_t request_utc_us);
  bool Pause(const std::string& command_id,
             int64_t request_utc_us,
             int64_t effective_utc_us,
             double boundary_deviation_ms);
  bool Resume(const std::string& command_id,
              int64_t request_utc_us,
              int64_t effective_utc_us);
  bool Seek(const std::string& command_id,
            int64_t request_utc_us,
            int64_t target_pts_us,
            int64_t effective_utc_us);
  bool Stop(const std::string& command_id,
            int64_t request_utc_us,
            int64_t effective_utc_us);
  bool Recover(const std::string& command_id, int64_t request_utc_us);

  void OnBufferDepth(std::size_t depth,
                     std::size_t capacity,
                     int64_t event_utc_us);
  void OnBackPressureEvent(OrchestrationLoop::BackPressureEvent event,
                           int64_t event_utc_us);
  void OnBackPressureCleared(int64_t event_utc_us);

  void OnExternalTimeout(int64_t event_utc_us);
  void OnQueueOverflow();

  [[nodiscard]] State state() const;
  [[nodiscard]] MetricsSnapshot Snapshot() const;

  // Dual-producer slot management
  // Sets a factory function for creating producers (must be called before loadPreviewAsset).
  // The factory receives (path, assetId, ringBuffer, clock) and returns a producer.
  using ProducerFactory = std::function<std::unique_ptr<producers::IProducer>(
      const std::string& path,
      const std::string& assetId,
      buffer::FrameRingBuffer& ringBuffer,
      std::shared_ptr<timing::MasterClock> clock)>;
  
  void setProducerFactory(ProducerFactory factory);

  // Loads a producer into the preview slot.
  // Requires: setProducerFactory() must be called first, and ringBuffer/clock must be provided.
  // Returns true on success, false on failure.
  bool loadPreviewAsset(const std::string& path,
                       const std::string& assetId,
                       buffer::FrameRingBuffer& ringBuffer,
                       std::shared_ptr<timing::MasterClock> clock);

  // Switches preview slot to live slot.
  // Stops live producer, flushes renderer, resets timestamps, and swaps producers.
  // Requires: renderer pointer for flushing (can be nullptr if not available).
  // Returns true on success, false on failure.
  bool activatePreviewAsLive(renderer::FrameRenderer* renderer = nullptr);

  // Gets the preview slot (const access).
  const ProducerSlot& getPreviewSlot() const;

  // Gets the live slot (const access).
  const ProducerSlot& getLiveSlot() const;

 private:
  void TransitionLocked(State to, int64_t event_utc_us);
  void RecordTransitionLocked(State from, State to);
  void RecordLatencyLocked(std::vector<double>& samples, double value_ms);
  double PercentileLocked(const std::vector<double>& samples, double percentile) const;
  bool RegisterCommandLocked(const std::string& command_id);
  void RecordIllegalTransitionLocked(State from, State attempted_to);

  constexpr static double kPauseLatencyThresholdMs = 33.0;
  constexpr static double kResumeLatencyThresholdMs = 50.0;
  constexpr static double kSeekLatencyThresholdMs = 250.0;
  constexpr static double kStopLatencyThresholdMs = 500.0;
  constexpr static std::size_t kReadinessThresholdFrames = 3;

  mutable std::mutex mutex_;

  State state_;
  std::unordered_map<std::string, int64_t> processed_commands_;
  int64_t current_pts_us_;
  std::map<std::pair<State, State>, uint64_t> transitions_;
  uint64_t illegal_transition_total_;
  uint64_t latency_violation_total_;
  uint64_t timeout_total_;
  uint64_t queue_overflow_total_;
  uint64_t recover_total_;
  uint64_t consistency_failure_total_;
  uint64_t late_seek_total_;
  std::vector<double> pause_latencies_ms_;
  std::vector<double> resume_latencies_ms_;
  std::vector<double> seek_latencies_ms_;
  std::vector<double> stop_latencies_ms_;
  std::vector<double> pause_deviation_ms_;

  // Dual-producer slots
  ProducerSlot previewSlot;
  ProducerSlot liveSlot;

  // Producer factory (set by playout_service)
  ProducerFactory producer_factory_;
};

}  // namespace retrovue::runtime

#endif  // RETROVUE_RUNTIME_PLAYOUT_CONTROL_STATE_MACHINE_H_

