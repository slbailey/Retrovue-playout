#ifndef RETROVUE_TESTS_FIXTURES_CHANNEL_MANAGER_STUB_H_
#define RETROVUE_TESTS_FIXTURES_CHANNEL_MANAGER_STUB_H_

#include <chrono>
#include <iostream>
#include <cstdint>
#include <memory>
#include <thread>

#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/decode/FrameProducer.h"
#include "retrovue/telemetry/MetricsExporter.h"

namespace retrovue::tests::fixtures
{

struct ChannelRuntime
{
  int32_t channel_id;
  std::unique_ptr<retrovue::buffer::FrameRingBuffer> buffer;
  std::unique_ptr<retrovue::decode::FrameProducer> producer;
  retrovue::telemetry::ChannelState state;
};

class ChannelManagerStub
{
public:
  ChannelRuntime StartChannel(int32_t channel_id,
                              const retrovue::decode::ProducerConfig& config,
                              retrovue::telemetry::MetricsExporter& exporter,
                              std::size_t buffer_capacity = 30)
  {
    ChannelRuntime runtime{};
    runtime.channel_id = channel_id;
    runtime.buffer = std::make_unique<retrovue::buffer::FrameRingBuffer>(buffer_capacity);
    runtime.producer = std::make_unique<retrovue::decode::FrameProducer>(config, *runtime.buffer);
    runtime.state = retrovue::telemetry::ChannelState::BUFFERING;

    exporter.SubmitChannelMetrics(channel_id, ToMetrics(runtime));
    runtime.producer->Start();
    WaitForMinimumDepth(*runtime.buffer, exporter, runtime.channel_id, runtime.state);
    return runtime;
  }

  void RequestTeardown(ChannelRuntime& runtime,
                       retrovue::telemetry::MetricsExporter& exporter,
                       const std::string& reason,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
                       bool remove_metrics = true)
  {
    if (runtime.producer)
    {
      runtime.producer->RequestTeardown(timeout);
    }

    // Wait for producer to stop and drain buffer
    const auto start = std::chrono::steady_clock::now();
    while (runtime.producer && runtime.producer->IsRunning())
    {
      // During teardown, drain the buffer periodically to help it empty
      if (runtime.buffer && !runtime.buffer->IsEmpty())
      {
        retrovue::buffer::Frame frame;
        // Pop a few frames to help drainage
        for (int i = 0; i < 5 && runtime.buffer->Pop(frame); ++i)
        {
          // Drain frames
        }
      }
      
      if (std::chrono::steady_clock::now() - start > timeout)
      {
        std::cerr << "[ChannelManagerStub] Teardown timed out for channel "
                  << runtime.channel_id << ", forcing stop" << std::endl;
        runtime.producer->ForceStop();
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (runtime.producer)
    {
      runtime.producer->Stop();
    }

    // Final drain of any remaining frames in buffer
    if (runtime.buffer)
    {
      retrovue::buffer::Frame frame;
      while (runtime.buffer->Pop(frame))
      {
        // Drain all remaining frames
      }
    }

    runtime.state = retrovue::telemetry::ChannelState::STOPPED;
    exporter.SubmitChannelMetrics(runtime.channel_id, ToMetrics(runtime));
    
    if (remove_metrics)
    {
      exporter.SubmitChannelRemoval(runtime.channel_id);
    }
  }

  void StopChannel(ChannelRuntime& runtime,
                   retrovue::telemetry::MetricsExporter& exporter)
  {
    // StopChannel should keep metrics with STOPPED state (for BC-005, BC-003)
    RequestTeardown(runtime, exporter, "ChannelManagerStub::StopChannel", 
                    std::chrono::milliseconds(500), /*remove_metrics=*/false);
    if (runtime.buffer)
    {
      runtime.buffer->Clear();
    }
  }

private:
  static void WaitForMinimumDepth(retrovue::buffer::FrameRingBuffer& buffer,
                                  retrovue::telemetry::MetricsExporter& exporter,
                                  int32_t channel_id,
                                  retrovue::telemetry::ChannelState& state)
  {
    constexpr std::size_t kReadyDepth = 3;
    constexpr auto kTimeout = std::chrono::seconds(2);
    const auto start = std::chrono::steady_clock::now();

    while (buffer.Size() < kReadyDepth)
    {
      if (std::chrono::steady_clock::now() - start > kTimeout)
      {
        state = retrovue::telemetry::ChannelState::BUFFERING;
        exporter.SubmitChannelMetrics(channel_id,
                                      ToMetrics(buffer.Size(),
                                                state));
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    state = retrovue::telemetry::ChannelState::READY;
    exporter.SubmitChannelMetrics(channel_id,
                                  ToMetrics(buffer.Size(),
                                            state));
  }

  static retrovue::telemetry::ChannelMetrics ToMetrics(const ChannelRuntime& runtime)
  {
    return ToMetrics(runtime.buffer ? runtime.buffer->Size() : 0,
                     runtime.state);
  }

  static retrovue::telemetry::ChannelMetrics ToMetrics(std::size_t buffer_depth,
                                                       retrovue::telemetry::ChannelState state)
  {
    retrovue::telemetry::ChannelMetrics metrics{};
    metrics.state = state;
    metrics.buffer_depth_frames = state == retrovue::telemetry::ChannelState::STOPPED
                                      ? 0
                                      : static_cast<uint64_t>(buffer_depth);
    metrics.frame_gap_seconds = 0.0;
    metrics.decode_failure_count = 0;
    return metrics;
  }
};

} // namespace retrovue::tests::fixtures

#endif // RETROVUE_TESTS_FIXTURES_CHANNEL_MANAGER_STUB_H_

