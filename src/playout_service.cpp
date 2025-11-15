// Repository: Retrovue-playout
// Component: PlayoutControl gRPC Service Implementation
// Purpose: Implements the PlayoutControl service interface for channel lifecycle management.
// Copyright (c) 2025 RetroVue

#include "playout_service.h"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "retrovue/producers/video_file/VideoFileProducer.h"
#include "retrovue/runtime/ProducerSlot.h"

namespace retrovue
{
  namespace playout
  {

    namespace
    {
      constexpr char kApiVersion[] = "1.0.0";
      constexpr size_t kDefaultBufferSize = 60; // 60 frames (~2 seconds at 30fps)
      constexpr double kPpmDivisor = 1'000'000.0;

      int64_t NowUtc(const std::shared_ptr<timing::MasterClock> &clock)
      {
        if (clock)
        {
          return clock->now_utc_us();
        }

        const auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
      }

      std::string MakeCommandId(const char *prefix, int32_t channel_id)
      {
        return std::string(prefix) + "-" + std::to_string(channel_id);
      }

      telemetry::ChannelState ToChannelState(runtime::PlayoutControlStateMachine::State state)
      {
        using State = runtime::PlayoutControlStateMachine::State;
        switch (state)
        {
        case State::kIdle:
          return telemetry::ChannelState::STOPPED;
        case State::kBuffering:
          return telemetry::ChannelState::BUFFERING;
        case State::kReady:
        case State::kPlaying:
        case State::kPaused:
          return telemetry::ChannelState::READY;
        case State::kStopping:
          return telemetry::ChannelState::BUFFERING;
        case State::kError:
          return telemetry::ChannelState::ERROR_STATE;
        }
        return telemetry::ChannelState::STOPPED;
      }
    } // namespace

    PlayoutControlImpl::PlayoutControlImpl(
        std::shared_ptr<telemetry::MetricsExporter> metrics_exporter,
        std::shared_ptr<timing::MasterClock> master_clock)
        : metrics_exporter_(std::move(metrics_exporter)),
          master_clock_(std::move(master_clock))
    {
      std::cout << "[PlayoutControlImpl] Service initialized (API version: " << kApiVersion
                << ", drift ppm: "
                << (master_clock_ ? master_clock_->drift_ppm() : 0.0) << ")" << std::endl;
    }

    PlayoutControlImpl::~PlayoutControlImpl()
    {
      std::cout << "[PlayoutControlImpl] Service shutting down" << std::endl;

      // Stop all active channels
      std::lock_guard<std::mutex> lock(channels_mutex_);
      for (auto &[channel_id, worker] : active_channels_)
      {
        std::cout << "[PlayoutControlImpl] Stopping channel " << channel_id << std::endl;
        if (worker->renderer)
        {
          worker->renderer->Stop();
        }
        if (worker->control)
        {
          const int64_t now = NowUtc(master_clock_);
          worker->control->Stop(MakeCommandId("stop", channel_id), now, now);
        }
        if (worker->orchestration_loop)
        {
          worker->orchestration_loop->Stop();
        }
        if (worker->teardown_thread_active.load(std::memory_order_acquire) &&
            worker->teardown_thread.joinable())
        {
          worker->teardown_thread.join();
        }
        worker->teardown_thread_active.store(false, std::memory_order_release);
        if (worker->producer)
        {
          worker->producer->ForceStop();
          worker->producer->Stop();
        }
        if (metrics_exporter_)
        {
          metrics_exporter_->SubmitChannelRemoval(channel_id);
        }
      }
      active_channels_.clear();
    }

    grpc::Status PlayoutControlImpl::StartChannel(grpc::ServerContext *context,
                                                  const StartChannelRequest *request,
                                                  StartChannelResponse *response)
    {
      std::lock_guard<std::mutex> lock(channels_mutex_);

      const int32_t channel_id = request->channel_id();
      const int64_t request_time = NowUtc(master_clock_);
      const std::string &plan_handle = request->plan_handle();
      const int32_t port = request->port();

      std::cout << "[StartChannel] Request received: channel_id=" << channel_id
                << ", plan_handle=" << plan_handle << ", port=" << port << std::endl;

      // Check if channel is already active
      if (active_channels_.find(channel_id) != active_channels_.end())
      {
        response->set_success(false);
        response->set_message("Channel already active");
        return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                            "Channel is already running");
      }

      // Create channel worker
      auto worker = std::make_unique<ChannelWorker>(channel_id, plan_handle, port);

      // Initialize ring buffer
      worker->ring_buffer = std::make_unique<buffer::FrameRingBuffer>(kDefaultBufferSize);

      // Initialize playout control state machine
      worker->control = std::make_unique<runtime::PlayoutControlStateMachine>();
      worker->control->BeginSession(MakeCommandId("begin", channel_id), request_time);

      // Set producer factory for creating VideoFileProducer instances
      // Check for fake video mode
      const char *fake_video = std::getenv("AIR_FAKE_VIDEO");
      bool stub_mode = (fake_video != nullptr && std::string(fake_video) == "1");

      // Create condition variable and flag for shadow decode readiness
      auto shadow_decode_ready = std::make_shared<std::atomic<bool>>(false);
      auto shadow_decode_cv = std::make_shared<std::condition_variable>();
      auto shadow_decode_mutex = std::make_shared<std::mutex>();

      worker->control->setProducerFactory(
          [stub_mode, shadow_decode_ready, shadow_decode_cv, shadow_decode_mutex, channel_id](
              const std::string &p, const std::string &aid,
              buffer::FrameRingBuffer &rb, std::shared_ptr<timing::MasterClock> clk)
              -> std::unique_ptr<producers::IProducer> {
            producers::video_file::ProducerConfig config;
            config.asset_uri = p;
            config.target_width = 1920;
            config.target_height = 1080;
            config.target_fps = 30.0;
            config.stub_mode = stub_mode;

            // Set up event callback to listen for ShadowDecodeReady
            producers::video_file::ProducerEventCallback event_callback =
                [shadow_decode_ready, shadow_decode_cv, shadow_decode_mutex, channel_id](
                    const std::string &event_type, const std::string &message) {
                  if (event_type == "ShadowDecodeReady")
                  {
                    std::lock_guard<std::mutex> lock(*shadow_decode_mutex);
                    shadow_decode_ready->store(true, std::memory_order_release);
                    shadow_decode_cv->notify_all();
                    std::cout << "[PlayoutControlImpl] ProducerEvent::ShadowDecodeReady(channelId=" 
                              << channel_id << ", slotId=preview)" << std::endl;
                  }
                };

            return std::make_unique<producers::video_file::VideoFileProducer>(
                config, rb, clk, event_callback);
          });

      // For backward compatibility: load first asset directly into live slot
      // (If upstream uses LoadPreview/SwitchToLive, this path is not used)
      std::string asset_id = "start-" + std::to_string(channel_id);
      
      // Reset the flag before loading (in case event fires during load)
      shadow_decode_ready->store(false, std::memory_order_release);
      
      if (!worker->control->loadPreviewAsset(plan_handle, asset_id, *worker->ring_buffer, master_clock_))
      {
        response->set_success(false);
        response->set_message("Failed to load initial asset");
        return grpc::Status(grpc::StatusCode::INTERNAL, "Asset load failed");
      }

      // Check if shadow decode is already ready (may happen in stub mode)
      const auto &preview_slot = worker->control->getPreviewSlot();
      if (preview_slot.loaded && preview_slot.producer)
      {
        auto* video_producer = dynamic_cast<producers::video_file::VideoFileProducer*>(
            preview_slot.producer.get());
        if (video_producer && video_producer->IsShadowDecodeReady())
        {
          // Already ready, no need to wait
          shadow_decode_ready->store(true, std::memory_order_release);
        }
      }

      // Wait for shadow decode readiness before activating
      {
        std::cout << "[StartChannel] Waiting for shadow decode readiness..." << std::endl;
        std::unique_lock<std::mutex> lock(*shadow_decode_mutex);
        const auto timeout = std::chrono::seconds(5); // 5 second timeout
        bool ready = shadow_decode_cv->wait_for(
            lock, timeout,
            [shadow_decode_ready] { return shadow_decode_ready->load(std::memory_order_acquire); });

        if (!ready)
        {
          // Double-check using IsShadowDecodeReady as fallback
          const auto &preview_slot_check = worker->control->getPreviewSlot();
          if (preview_slot_check.loaded && preview_slot_check.producer)
          {
            auto* video_producer = dynamic_cast<producers::video_file::VideoFileProducer*>(
                preview_slot_check.producer.get());
            if (video_producer && video_producer->IsShadowDecodeReady())
            {
              // Ready but event didn't fire, proceed anyway
              ready = true;
            }
          }
          
          if (!ready)
          {
            response->set_success(false);
            response->set_message("Timeout waiting for shadow decode readiness");
            return grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED, "Shadow decode timeout");
          }
        }
      } // Lock released here

      // Activate preview as live (backward compatibility: first asset goes live immediately)
      if (!worker->control->activatePreviewAsLive())
      {
        response->set_success(false);
        response->set_message("Failed to activate initial asset");
        return grpc::Status(grpc::StatusCode::INTERNAL, "Activation failed");
      }

      // Start the live producer (if not already running)
      // Note: Producer is already started when loaded into preview slot
      const auto &live_slot = worker->control->getLiveSlot();
      if (live_slot.loaded && live_slot.producer)
      {
        if (!live_slot.producer->isRunning())
        {
          if (!live_slot.producer->start())
          {
            response->set_success(false);
            response->set_message("Failed to start frame producer");
            return grpc::Status(grpc::StatusCode::INTERNAL, "Producer start failed");
          }
        }
      }

      // Configure and create renderer (Phase 3)
      renderer::RenderConfig render_config;
      render_config.mode = renderer::RenderMode::HEADLESS; // Default to headless mode
      render_config.window_width = 1920;
      render_config.window_height = 1080;
      render_config.window_title = "RetroVue Channel " + std::to_string(channel_id);

      worker->renderer = renderer::FrameRenderer::Create(
          render_config, *worker->ring_buffer, master_clock_, metrics_exporter_, channel_id);

      // Start render thread
      if (!worker->renderer->Start())
      {
        std::cerr << "[StartChannel] WARNING: Failed to start renderer, continuing without it"
                  << std::endl;
        // Don't fail the entire StartChannel operation if renderer fails
        // Producer will fill buffer, it just won't be consumed
      }

      // Initialize orchestration loop to monitor cadence and back-pressure.
      worker->underrun_active = std::make_shared<std::atomic<bool>>(false);
      worker->overrun_active = std::make_shared<std::atomic<bool>>(false);

      runtime::OrchestrationLoop::Config loop_config;
      loop_config.target_fps = 30.0; // Default FPS
      loop_config.max_tick_skew_ms = 1.5; // Allow slight tolerance in production

      auto loop = std::make_unique<runtime::OrchestrationLoop>(
          loop_config, master_clock_,
          nullptr);

      auto *loop_ptr = loop.get();
      auto *ring_buffer_ptr = worker->ring_buffer.get();
      auto *control_ptr = worker->control.get();
      auto underrun_flag = worker->underrun_active;
      auto overrun_flag = worker->overrun_active;
      auto metrics = metrics_exporter_;
      auto clock = master_clock_;

      loop->SetTickCallback(
          [loop_ptr, ring_buffer_ptr, control_ptr, underrun_flag, overrun_flag, metrics, channel_id, clock](
              const runtime::OrchestrationLoop::TickContext &context)
          {
            runtime::OrchestrationLoop::TickResult result;

            if (ring_buffer_ptr != nullptr)
            {
              const size_t depth = ring_buffer_ptr->Size();
              const size_t capacity = ring_buffer_ptr->Capacity();
              const int64_t now_utc = NowUtc(clock);

              if (control_ptr != nullptr)
              {
                control_ptr->OnBufferDepth(depth, capacity, now_utc);
              }

              if (depth == 0)
              {
                if (!underrun_flag->exchange(true))
                {
                  loop_ptr->ReportBackPressureEvent(
                      runtime::OrchestrationLoop::BackPressureEvent::kUnderrun);
                  if (control_ptr != nullptr)
                  {
                    control_ptr->OnBackPressureEvent(
                        runtime::OrchestrationLoop::BackPressureEvent::kUnderrun,
                        now_utc);
                  }
                }
              }
              else if (underrun_flag->exchange(false))
              {
                result.backpressure_cleared = true;
                if (control_ptr != nullptr)
                {
                  control_ptr->OnBackPressureCleared(now_utc);
                }
              }

              if (capacity > 0 && depth + 1 >= capacity)
              {
                if (!overrun_flag->exchange(true))
                {
                  loop_ptr->ReportBackPressureEvent(
                      runtime::OrchestrationLoop::BackPressureEvent::kOverrun);
                  if (control_ptr != nullptr)
                  {
                    control_ptr->OnBackPressureEvent(
                        runtime::OrchestrationLoop::BackPressureEvent::kOverrun,
                        now_utc);
                  }
                }
              }
              else if (overrun_flag->exchange(false))
              {
                result.backpressure_cleared = true;
                if (control_ptr != nullptr)
                {
                  control_ptr->OnBackPressureCleared(now_utc);
                }
              }

              // Approximate producer→renderer latency using buffer occupancy.
              if (capacity > 0)
              {
                const double ratio = static_cast<double>(depth) / static_cast<double>(capacity);
                result.producer_to_renderer_latency_ms = ratio * 20.0; // heuristic estimate
              }
            }

            if (metrics)
            {
              telemetry::ChannelMetrics snapshot{};
              snapshot.state = control_ptr ? ToChannelState(control_ptr->state())
                                           : telemetry::ChannelState::READY;
              snapshot.buffer_depth_frames = ring_buffer_ptr ? ring_buffer_ptr->Size() : 0;
              metrics->SubmitChannelMetrics(channel_id, snapshot);
            }

            return result;
          });

      loop->Start();
      worker->orchestration_loop = std::move(loop);

      // Update metrics
      if (metrics_exporter_)
      {
        telemetry::ChannelMetrics metrics;
        metrics.state = ToChannelState(worker->control ? worker->control->state()
                                                       : runtime::PlayoutControlStateMachine::State::kBuffering);
        metrics.buffer_depth_frames = 0;
        metrics.frame_gap_seconds = 0.0;
        metrics.decode_failure_count = 0;
        metrics.corrections_total = 0;
        metrics_exporter_->SubmitChannelMetrics(channel_id, metrics);
      }

      // Store worker
      active_channels_[channel_id] = std::move(worker);

      response->set_success(true);
      response->set_message("Channel started with frame production");

      std::cout << "[StartChannel] Channel " << channel_id << " started successfully" << std::endl;
      return grpc::Status::OK;
    }

    grpc::Status PlayoutControlImpl::UpdatePlan(grpc::ServerContext *context,
                                                const UpdatePlanRequest *request,
                                                UpdatePlanResponse *response)
    {
      std::lock_guard<std::mutex> lock(channels_mutex_);

      const int32_t channel_id = request->channel_id();
      const std::string &plan_handle = request->plan_handle();
      const int64_t request_time = NowUtc(master_clock_);

      std::cout << "[UpdatePlan] Request received: channel_id=" << channel_id
                << ", plan_handle=" << plan_handle << std::endl;

      // Check if channel is active
      auto it = active_channels_.find(channel_id);
      if (it == active_channels_.end())
      {
        response->set_success(false);
        response->set_message("Channel not found");
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Channel is not running");
      }

      auto &worker = it->second;

      // Phase 3: Hot-swap by stopping and restarting producer and renderer
      // Future optimization: seamless transition without stopping
      std::cout << "[UpdatePlan] Stopping current producer and renderer..." << std::endl;

      if (worker->renderer)
      {
        worker->renderer->Stop();
      }
      if (worker->orchestration_loop)
      {
        worker->orchestration_loop->Stop();
        worker->orchestration_loop.reset();
      }

      if (worker->producer)
      {
        worker->producer->Stop();
      }

      // Clear ring buffer
      if (worker->ring_buffer)
      {
        worker->ring_buffer->Clear();
      }

      // Update plan handle
      worker->plan_handle = plan_handle;

      // Reconfigure and restart producer
      decode::ProducerConfig producer_config;
      producer_config.asset_uri = plan_handle;
      producer_config.target_width = 1920;
      producer_config.target_height = 1080;
      producer_config.target_fps = 30.0;
      producer_config.stub_mode = false; // Phase 3: real decode

      worker->producer = std::make_unique<decode::FrameProducer>(
          producer_config, *worker->ring_buffer, master_clock_);

      if (!worker->producer->Start())
      {
        response->set_success(false);
        response->set_message("Failed to restart frame producer");

        // Update metrics to error state
        if (metrics_exporter_)
        {
          telemetry::ChannelMetrics metrics;
          metrics.state = telemetry::ChannelState::ERROR_STATE;
          metrics.corrections_total = 0;
          metrics_exporter_->SubmitChannelMetrics(channel_id, metrics);
        }

        return grpc::Status(grpc::StatusCode::INTERNAL, "Producer restart failed");
      }

      // Restart renderer
      if (worker->renderer)
      {
        if (!worker->renderer->Start())
        {
          std::cerr << "[UpdatePlan] WARNING: Failed to restart renderer" << std::endl;
        }

        worker->underrun_active = std::make_shared<std::atomic<bool>>(false);
        worker->overrun_active = std::make_shared<std::atomic<bool>>(false);

        runtime::OrchestrationLoop::Config loop_config;
        loop_config.target_fps = producer_config.target_fps;

        auto loop = std::make_unique<runtime::OrchestrationLoop>(
            loop_config, master_clock_,
            nullptr);

        auto *loop_ptr = loop.get();
        auto *ring_buffer_ptr = worker->ring_buffer.get();
        auto *control_ptr = worker->control.get();
        auto underrun_flag = worker->underrun_active;
        auto overrun_flag = worker->overrun_active;
        auto metrics = metrics_exporter_;
        auto clock = master_clock_;

        loop->SetTickCallback(
            [loop_ptr, ring_buffer_ptr, control_ptr, underrun_flag, overrun_flag, metrics, channel_id, clock](
                const runtime::OrchestrationLoop::TickContext &)
            {
              runtime::OrchestrationLoop::TickResult result;

              if (ring_buffer_ptr != nullptr)
              {
                const size_t depth = ring_buffer_ptr->Size();
                const size_t capacity = ring_buffer_ptr->Capacity();
                const int64_t now_utc = NowUtc(clock);

                if (control_ptr != nullptr)
                {
                  control_ptr->OnBufferDepth(depth, capacity, now_utc);
                }

                if (depth == 0)
                {
                  if (!underrun_flag->exchange(true))
                  {
                    loop_ptr->ReportBackPressureEvent(
                        runtime::OrchestrationLoop::BackPressureEvent::kUnderrun);
                    if (control_ptr != nullptr)
                    {
                      control_ptr->OnBackPressureEvent(
                          runtime::OrchestrationLoop::BackPressureEvent::kUnderrun,
                          now_utc);
                    }
                  }
                }
                else if (underrun_flag->exchange(false))
                {
                  result.backpressure_cleared = true;
                  if (control_ptr != nullptr)
                  {
                    control_ptr->OnBackPressureCleared(now_utc);
                  }
                }

                if (capacity > 0 && depth + 1 >= capacity)
                {
                  if (!overrun_flag->exchange(true))
                  {
                    loop_ptr->ReportBackPressureEvent(
                        runtime::OrchestrationLoop::BackPressureEvent::kOverrun);
                    if (control_ptr != nullptr)
                    {
                      control_ptr->OnBackPressureEvent(
                          runtime::OrchestrationLoop::BackPressureEvent::kOverrun,
                          now_utc);
                    }
                  }
                }
                else if (overrun_flag->exchange(false))
                {
                  result.backpressure_cleared = true;
                  if (control_ptr != nullptr)
                  {
                    control_ptr->OnBackPressureCleared(now_utc);
                  }
                }

                if (capacity > 0)
                {
                  const double ratio = static_cast<double>(depth) / static_cast<double>(capacity);
                  result.producer_to_renderer_latency_ms = ratio * 20.0;
                }
              }

              if (metrics)
              {
                telemetry::ChannelMetrics snapshot{};
                snapshot.state = control_ptr ? ToChannelState(control_ptr->state())
                                             : telemetry::ChannelState::READY;
                snapshot.buffer_depth_frames = ring_buffer_ptr ? ring_buffer_ptr->Size() : 0;
                metrics->SubmitChannelMetrics(channel_id, snapshot);
              }

              return result;
            });

        loop->Start();
        worker->orchestration_loop = std::move(loop);
      }

      if (worker->control)
      {
        const int64_t effective_time = NowUtc(master_clock_);
        worker->control->Seek(MakeCommandId("seek", channel_id),
                              request_time,
                              effective_time,
                              effective_time);
      }

      // Update metrics
      UpdateChannelMetrics(channel_id);

      response->set_success(true);
      response->set_message("Plan updated with producer restart");

      std::cout << "[UpdatePlan] Channel " << channel_id << " plan updated successfully" << std::endl;
      return grpc::Status::OK;
    }

    grpc::Status PlayoutControlImpl::StopChannel(grpc::ServerContext *context,
                                                 const StopChannelRequest *request,
                                                 StopChannelResponse *response)
    {
      const int32_t channel_id = request->channel_id();
      const int64_t request_time = NowUtc(master_clock_);
      std::cout << "[StopChannel] Request received: channel_id=" << channel_id << std::endl;
      return StopChannelShared(channel_id, response, request_time, /*forced_teardown=*/false);
    }

    grpc::Status PlayoutControlImpl::GetVersion(grpc::ServerContext *context,
                                                const ApiVersionRequest *request,
                                                ApiVersion *response)
    {
      std::cout << "[GetVersion] Request received" << std::endl;

      response->set_version(kApiVersion);

      std::cout << "[GetVersion] Returning version: " << kApiVersion << std::endl;
      return grpc::Status::OK;
    }

    grpc::Status PlayoutControlImpl::LoadPreview(grpc::ServerContext *context,
                                                 const LoadPreviewRequest *request,
                                                 LoadPreviewResponse *response)
    {
      std::lock_guard<std::mutex> lock(channels_mutex_);

      const int32_t channel_id = request->channel_id();
      const std::string &path = request->path();
      const std::string &asset_id = request->asset_id();

      std::cout << "[LoadPreview] Request received: channel_id=" << channel_id
                << ", path=" << path << ", asset_id=" << asset_id << std::endl;

      // Check if channel exists
      auto it = active_channels_.find(channel_id);
      if (it == active_channels_.end())
      {
        response->set_success(false);
        response->set_message("Channel not found");
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Channel is not running");
      }

      auto &worker = it->second;

      // Check if state machine has producer factory set
      if (!worker->control)
      {
        response->set_success(false);
        response->set_message("State machine not initialized");
        return grpc::Status(grpc::StatusCode::INTERNAL, "State machine not available");
      }

      // Set producer factory if not already set
      // Factory creates VideoFileProducer instances
      worker->control->setProducerFactory(
          [](const std::string &p, const std::string &aid,
             buffer::FrameRingBuffer &rb, std::shared_ptr<timing::MasterClock> clk)
              -> std::unique_ptr<producers::IProducer> {
            // Check for fake video mode
            const char *fake_video = std::getenv("AIR_FAKE_VIDEO");
            bool stub_mode = (fake_video != nullptr && std::string(fake_video) == "1");

            producers::video_file::ProducerConfig config;
            config.asset_uri = p;
            config.target_width = 1920;
            config.target_height = 1080;
            config.target_fps = 30.0;
            config.stub_mode = stub_mode;

            return std::make_unique<producers::video_file::VideoFileProducer>(
                config, rb, clk, nullptr);
          });

      // Load preview asset
      if (!worker->control->loadPreviewAsset(path, asset_id, *worker->ring_buffer, master_clock_))
      {
        response->set_success(false);
        response->set_message("Failed to load preview asset");
        return grpc::Status(grpc::StatusCode::INTERNAL, "Preview load failed");
      }

      response->set_success(true);
      response->set_message("Preview asset loaded successfully");
      std::cout << "[LoadPreview] Preview asset loaded: " << asset_id << std::endl;
      return grpc::Status::OK;
    }

    grpc::Status PlayoutControlImpl::SwitchToLive(grpc::ServerContext *context,
                                                  const SwitchToLiveRequest *request,
                                                  SwitchToLiveResponse *response)
    {
      std::lock_guard<std::mutex> lock(channels_mutex_);

      const int32_t channel_id = request->channel_id();
      const std::string &asset_id = request->asset_id();

      std::cout << "[SwitchToLive] Request received: channel_id=" << channel_id
                << ", asset_id=" << asset_id << std::endl;

      // Check if channel exists
      auto it = active_channels_.find(channel_id);
      if (it == active_channels_.end())
      {
        response->set_success(false);
        response->set_message("Channel not found");
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Channel is not running");
      }

      auto &worker = it->second;

      if (!worker->control)
      {
        response->set_success(false);
        response->set_message("State machine not initialized");
        return grpc::Status(grpc::StatusCode::INTERNAL, "State machine not available");
      }

      // Verify asset_id matches preview slot
      const auto &preview_slot = worker->control->getPreviewSlot();
      if (!preview_slot.loaded || preview_slot.asset_id != asset_id)
      {
        response->set_success(false);
        response->set_message("Preview asset ID mismatch or no preview loaded");
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Asset ID mismatch");
      }

      // Activate preview as live (seamless switch - no renderer reset)
      // The switch algorithm handles PTS alignment and shadow mode exit
      // Renderer continues reading seamlessly from ring buffer
      // Note: Preview producer is already running (in shadow mode), so no need to start it again
      if (!worker->control->activatePreviewAsLive(worker->renderer.get()))
      {
        response->set_success(false);
        response->set_message("Failed to activate preview as live");
        return grpc::Status(grpc::StatusCode::INTERNAL, "Switch failed");
      }

      // Producer is already running (was in shadow mode, now writing to buffer)
      // No need to start it again

      response->set_success(true);
      response->set_message("Switched to live successfully");
      std::cout << "[SwitchToLive] Switched to live: " << asset_id << std::endl;
      return grpc::Status::OK;
    }

    void PlayoutControlImpl::RequestTeardown(int32_t channel_id, const std::string& reason)
    {
      std::lock_guard<std::mutex> lock(channels_mutex_);

      auto it = active_channels_.find(channel_id);
      if (it == active_channels_.end())
      {
        std::cerr << "[PlayoutControlImpl] Teardown requested for unknown channel "
                  << channel_id << std::endl;
        return;
      }

      auto& worker = it->second;
      if (worker->teardown_requested.exchange(true))
      {
        std::cout << "[PlayoutControlImpl] Teardown already in flight for channel "
                  << channel_id << std::endl;
        return;
      }

      worker->teardown_reason = reason;
      worker->teardown_started = std::chrono::steady_clock::now();
      std::cout << "[PlayoutControlImpl] Channel " << channel_id
                << " teardown requested: " << reason << std::endl;

      if (worker->producer)
      {
        worker->producer->RequestTeardown(worker->teardown_timeout);
      }

      worker->teardown_thread_active.store(true, std::memory_order_release);
      worker->teardown_thread = std::thread(&PlayoutControlImpl::MonitorTeardown, this, channel_id);
    }

    void PlayoutControlImpl::MonitorTeardown(int32_t channel_id)
    {
      while (true)
      {
        bool finalize = false;
        bool forced = false;
        {
          std::lock_guard<std::mutex> lock(channels_mutex_);
          auto it = active_channels_.find(channel_id);
          if (it == active_channels_.end())
          {
            return;
          }

          auto& worker = it->second;
          if (!worker->teardown_requested.load())
          {
            return;
          }

          const auto now = std::chrono::steady_clock::now();
          if (!worker->producer || !worker->producer->IsRunning())
          {
            finalize = true;
          }
          else if (now - worker->teardown_started > worker->teardown_timeout)
          {
            std::cerr << "[PlayoutControlImpl] Channel " << channel_id
                      << " teardown exceeded timeout; forcing producer stop" << std::endl;
            forced = true;
            worker->producer->ForceStop();
            finalize = true;
          }
        }

        if (finalize)
        {
          FinalizeTeardown(channel_id, forced);
          return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }

    void PlayoutControlImpl::FinalizeTeardown(int32_t channel_id, bool forced)
    {
      StopChannelResponse ignored;
      auto status = StopChannelShared(channel_id, &ignored, std::nullopt, forced);
      if (!status.ok())
      {
        std::cerr << "[PlayoutControlImpl] FinalizeTeardown failed for channel "
                  << channel_id << ": " << status.error_message() << std::endl;
      }
    }

    grpc::Status PlayoutControlImpl::StopChannelShared(int32_t channel_id,
                                                       StopChannelResponse* response,
                                                       const std::optional<int64_t>& request_time,
                                                       bool forced_teardown)
    {
      std::lock_guard<std::mutex> lock(channels_mutex_);
      return StopChannelLocked(channel_id, response, request_time, forced_teardown);
    }

    grpc::Status PlayoutControlImpl::StopChannelLocked(int32_t channel_id,
                                                       StopChannelResponse* response,
                                                       const std::optional<int64_t>& request_time,
                                                       bool forced_teardown)
    {
      auto it = active_channels_.find(channel_id);
      if (it == active_channels_.end())
      {
        if (response)
        {
          response->set_success(false);
          response->set_message("Channel not found");
        }
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Channel is not running");
      }

      auto& worker = it->second;

      if (worker->teardown_thread_active.load(std::memory_order_acquire) &&
          worker->teardown_thread.joinable())
      {
        if (worker->teardown_thread.get_id() == std::this_thread::get_id())
        {
          worker->teardown_thread.detach();
        }
        else
        {
          worker->teardown_thread.join();
        }
      }
      worker->teardown_thread_active.store(false, std::memory_order_release);
      if (worker->teardown_thread.joinable())
      {
        worker->teardown_thread = std::thread();
      }

      // Stop producers FIRST (before renderer) to prevent new frames from being produced
      // Stop producers in preview and live slots (new dual-producer architecture)
      if (worker->control)
      {
        // Stop preview slot producer if loaded (stop even if not yet running, in case thread is starting)
        const auto &preview_slot = worker->control->getPreviewSlot();
        if (preview_slot.loaded && preview_slot.producer)
        {
          std::cout << "[StopChannel] Stopping preview producer for channel " << channel_id << std::endl;
          auto* video_producer = dynamic_cast<producers::video_file::VideoFileProducer*>(
              preview_slot.producer.get());
          if (video_producer)
          {
            // Force stop to ensure quick shutdown (even if thread hasn't started yet)
            video_producer->ForceStop();
          }
          // Always call stop() to ensure producer is stopped, even if isRunning() is false
          preview_slot.producer->stop();
          
          // Wait briefly for thread to join (with timeout)
          int wait_count = 0;
          while (preview_slot.producer->isRunning() && wait_count < 50)
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            wait_count++;
          }
        }

        // Stop live slot producer if loaded
        const auto &live_slot = worker->control->getLiveSlot();
        if (live_slot.loaded && live_slot.producer)
        {
          std::cout << "[StopChannel] Stopping live producer for channel " << channel_id << std::endl;
          auto* video_producer = dynamic_cast<producers::video_file::VideoFileProducer*>(
              live_slot.producer.get());
          if (video_producer)
          {
            if (forced_teardown)
            {
              video_producer->ForceStop();
            }
          }
          // Always call stop() to ensure producer is stopped
          live_slot.producer->stop();
          
          // Wait briefly for thread to join (with timeout)
          int wait_count = 0;
          while (live_slot.producer->isRunning() && wait_count < 50)
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            wait_count++;
          }
        }
      }

      if (worker->renderer)
      {
        std::cout << "[StopChannel] Stopping renderer for channel " << channel_id << std::endl;
        worker->renderer->Stop();
      }

      if (worker->orchestration_loop)
      {
        worker->orchestration_loop->Stop();
        worker->orchestration_loop.reset();
      }

      // Stop legacy producer if it exists (backward compatibility)
      if (worker->producer)
      {
        if (forced_teardown)
        {
          worker->producer->ForceStop();
        }
        std::cout << "[StopChannel] Stopping legacy producer for channel " << channel_id << std::endl;
        worker->producer->Stop();
      }

      if (worker->control)
      {
        const int64_t effective_time = NowUtc(master_clock_);
        const int64_t request_utc = request_time.value_or(effective_time);
        worker->control->Stop(MakeCommandId("stop", channel_id), request_utc, effective_time);
      }

      if (worker->teardown_requested.load())
      {
        const auto duration = std::chrono::steady_clock::now() - worker->teardown_started;
        const auto duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        std::cout << "[PlayoutControlImpl] Channel " << channel_id
                  << " teardown duration: " << duration_ms << " ms"
                  << " (forced=" << std::boolalpha << forced_teardown
                  << ", reason=\"" << worker->teardown_reason << "\")" << std::endl;
        worker->teardown_requested.store(false);
      }

      if (metrics_exporter_)
      {
        telemetry::ChannelMetrics metrics;
        metrics.state = telemetry::ChannelState::STOPPED;
        metrics.buffer_depth_frames = 0;
        metrics.frame_gap_seconds = 0.0;
        metrics.decode_failure_count = 0;
        metrics.corrections_total = 0;
        metrics_exporter_->SubmitChannelMetrics(channel_id, metrics);
        metrics_exporter_->SubmitChannelRemoval(channel_id);
      }

      active_channels_.erase(it);

      if (response)
      {
        response->set_success(true);
        response->set_message(forced_teardown
                                  ? "Channel stopped (teardown forced after timeout)"
                                  : "Channel stopped and resources released");
      }

      std::cout << "[StopChannel] Channel " << channel_id << " stopped successfully"
                << (forced_teardown ? " [forced]" : "") << std::endl;
      return grpc::Status::OK;
    }

    void PlayoutControlImpl::UpdateChannelMetrics(int32_t channel_id)
    {
      if (!metrics_exporter_)
      {
        return;
      }

      auto it = active_channels_.find(channel_id);
      if (it == active_channels_.end())
      {
        return;
      }

      auto &worker = it->second;

      telemetry::ChannelMetrics metrics;
      metrics.state = worker->control ? ToChannelState(worker->control->state())
                                      : telemetry::ChannelState::READY;

      if (worker->ring_buffer)
      {
        metrics.buffer_depth_frames = worker->ring_buffer->Size();
      }

      if (worker->producer)
      {
        // Update decode failure count (stub for now)
        metrics.decode_failure_count = worker->producer->GetBufferFullCount();
      }

      // Frame gap calculation would go here (requires MasterClock integration)
      metrics.frame_gap_seconds =
          master_clock_ ? master_clock_->drift_ppm() / kPpmDivisor : 0.0;

      metrics_exporter_->SubmitChannelMetrics(channel_id, metrics);
    }

  } // namespace playout
} // namespace retrovue
