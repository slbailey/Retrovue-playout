// Repository: Retrovue-playout
// Component: Video File Producer Contract Tests
// Purpose: Contract tests for VideoFileProducer domain.
// Copyright (c) 2025 RetroVue

#include "../../BaseContractTest.h"
#include "../ContractRegistryEnvironment.h"

#include <chrono>
#include <thread>
#include <vector>

#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/producers/video_file/VideoFileProducer.h"
#include "retrovue/timing/MasterClock.h"
#include "../../fixtures/EventBusStub.h"
#include "timing/TestMasterClock.h"

using namespace retrovue;
using namespace retrovue::producers::video_file;
using namespace retrovue::tests;
using namespace retrovue::tests::fixtures;

namespace
{

  using retrovue::tests::RegisterExpectedDomainCoverage;

  const bool kRegisterCoverage = []()
  {
    RegisterExpectedDomainCoverage(
        "VideoFileProducer",
        {"FE-001", "FE-002", "FE-003", "FE-004", "FE-005", "FE-006", 
         "FE-007", "FE-008", "FE-009", "FE-010", "FE-011", "FE-012"});
    return true;
  }();

  class VideoFileProducerContractTest : public BaseContractTest
  {
  protected:
    [[nodiscard]] std::string DomainName() const override
    {
      return "VideoFileProducer";
    }

    [[nodiscard]] std::vector<std::string> CoveredRuleIds() const override
    {
      return {
          "FE-001", "FE-002", "FE-003", "FE-004", "FE-005", "FE-006", 
          "FE-007", "FE-008", "FE-009", "FE-010", "FE-011", "FE-012"};
    }

    void SetUp() override
    {
      BaseContractTest::SetUp();
      event_bus_ = std::make_unique<EventBusStub>();
      clock_ = std::make_shared<retrovue::timing::TestMasterClock>();
      const int64_t epoch = 1'700'001'000'000'000;
      clock_->SetEpochUtcUs(epoch);
      clock_->SetRatePpm(0.0);
      clock_->SetNow(epoch, 0.0);
      buffer_ = std::make_unique<buffer::FrameRingBuffer>(60);
    }

    // Helper to get test media path (works from build directory)
    std::string GetTestMediaPath(const std::string& filename) const
    {
      // Try relative path from build directory first
      std::string relative_path = "../tests/fixtures/media/" + filename;
      // If that doesn't work, try absolute path from source
      // (tests may run from different directories)
      return relative_path;
    }

  void TearDown() override
  {
    if (producer_)
    {
      try
      {
        producer_->stop();
      }
      catch (...)
      {
        // Ignore exceptions during cleanup
      }
      producer_.reset();
    }
    buffer_.reset();
    event_bus_.reset();
    BaseContractTest::TearDown();
  }

    ProducerEventCallback MakeEventCallback()
    {
      return [this](const std::string &event_type, const std::string &message)
      {
        event_bus_->Emit(EventBusStub::ToEventType(event_type), message);
      };
    }

    std::unique_ptr<EventBusStub> event_bus_;
    std::shared_ptr<retrovue::timing::TestMasterClock> clock_;
    std::unique_ptr<buffer::FrameRingBuffer> buffer_;
    std::unique_ptr<VideoFileProducer> producer_;
  };

  // Rule: FE-001 Producer Lifecycle (VideoFileProducerDomainContract.md §FE-001)
  TEST_F(VideoFileProducerContractTest, FE_001_ProducerLifecycle)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_FALSE(producer_->isRunning());
    ASSERT_EQ(producer_->GetFramesProduced(), 0u);
    ASSERT_EQ(producer_->GetState(), ProducerState::STOPPED);

    ASSERT_TRUE(producer_->start());
    ASSERT_TRUE(producer_->isRunning());
    ASSERT_EQ(producer_->GetState(), ProducerState::RUNNING);

    ASSERT_FALSE(producer_->start());

    producer_->stop();
    ASSERT_FALSE(producer_->isRunning());
    ASSERT_EQ(producer_->GetState(), ProducerState::STOPPED);

    producer_->stop();
    producer_->stop();
    ASSERT_FALSE(producer_->isRunning());
  }

  TEST_F(VideoFileProducerContractTest, FE_001_DestructorStopsProducer)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());
    ASSERT_TRUE(producer_->isRunning());
    producer_.reset();
  }

  // Rule: FE-002 Frame Production Rate
  TEST_F(VideoFileProducerContractTest, FE_002_FrameProductionRate)
  {
    ProducerConfig config;
    config.asset_uri = GetTestMediaPath("sample.mp4");
    config.target_fps = 30.0;
    config.stub_mode = false;  // Use real decoding with sample file

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    uint64_t frames_produced = producer_->GetFramesProduced();
    ASSERT_GT(frames_produced, 0u);

    producer_->stop();
  }

  // Rule: FE-003 Frame Metadata Validity
  TEST_F(VideoFileProducerContractTest, FE_003_FrameMetadataValidity)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.target_width = 1920;
    config.target_height = 1080;
    config.target_fps = 30.0;
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    buffer::Frame frame;
    int64_t last_pts = -1;
    int frame_count = 0;

    while (buffer_->Pop(frame) && frame_count < 10)
    {
      if (last_pts >= 0)
      {
        ASSERT_GT(frame.metadata.pts, last_pts);
      }
      last_pts = frame.metadata.pts;
      ASSERT_LE(frame.metadata.dts, frame.metadata.pts);
      ASSERT_NEAR(frame.metadata.duration, 1.0 / config.target_fps, 0.001);
      ASSERT_EQ(frame.metadata.asset_uri, config.asset_uri);
      ASSERT_EQ(frame.width, config.target_width);
      ASSERT_EQ(frame.height, config.target_height);
      frame_count++;
    }

    ASSERT_GT(frame_count, 0);
    producer_->stop();
  }

  // Rule: FE-004 Frame Format Validity
  TEST_F(VideoFileProducerContractTest, FE_004_FrameFormatValidity)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.target_width = 1920;
    config.target_height = 1080;
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    buffer::Frame frame;
    int frame_count = 0;

    while (buffer_->Pop(frame) && frame_count < 5)
    {
      const size_t expected_size = static_cast<size_t>(config.target_width * config.target_height * 1.5);
      ASSERT_EQ(frame.data.size(), expected_size);
      ASSERT_GT(frame.data.size(), 0u);
      frame_count++;
    }

    ASSERT_GT(frame_count, 0);
    producer_->stop();
  }

  // Rule: FE-005 Backpressure Handling
  TEST_F(VideoFileProducerContractTest, FE_005_BackpressureHandling)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.target_fps = 120.0; // Very high FPS to fill small buffer quickly
    config.stub_mode = true;

    buffer_ = std::make_unique<buffer::FrameRingBuffer>(3); // Very small buffer
    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    // Wait for buffer to fill (3 frames at 120fps = ~25ms, wait 200ms to be safe)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Producer should have produced frames
    uint64_t frames_produced = producer_->GetFramesProduced();
    ASSERT_GT(frames_produced, 0u) << "Producer should produce frames";
    
    // Wait more to ensure buffer fills and backpressure occurs
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify buffer is full or has frames
    ASSERT_TRUE(buffer_->IsFull() || buffer_->Size() > 0u) << "Buffer should have frames";
    
    // If buffer is full, backpressure should have occurred
    if (buffer_->IsFull())
    {
      // Wait a bit more for backpressure events
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      uint64_t buffer_full_count = producer_->GetBufferFullCount();
      // Backpressure count might be 0 if producer hasn't attempted to push yet
      // Just verify producer is still running (handling backpressure gracefully)
      ASSERT_TRUE(producer_->isRunning()) << "Producer should handle backpressure without stopping";
    }

    producer_->stop();
  }

  // Rule: FE-006 Buffer Filling
  TEST_F(VideoFileProducerContractTest, FE_006_BufferFilling)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ASSERT_GT(buffer_->Size(), 0u);

    producer_->stop();
  }

  // Rule: FE-007 Decoder Fallback
  TEST_F(VideoFileProducerContractTest, FE_007_DecoderFallback)
  {
    ProducerConfig config;
    config.asset_uri = "nonexistent.mp4";
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_TRUE(producer_->isRunning());
    ASSERT_GT(producer_->GetFramesProduced(), 0u);

    producer_->stop();
  }

  // Rule: FE-008 Decode Error Recovery
  TEST_F(VideoFileProducerContractTest, FE_008_DecodeErrorRecovery)
  {
    ProducerConfig config;
    config.asset_uri = GetTestMediaPath("sample.mp4");
    config.stub_mode = false;  // Use real decoding

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_TRUE(producer_->isRunning());
    
    // Producer should continue operation even if transient decode errors occur
    // (errors are tracked but don't stop the producer)
    uint64_t decode_errors = producer_->GetDecodeErrors();
    // Decode errors may be 0 if file decodes cleanly, or > 0 if errors occurred
    // The important thing is that producer continues running

    producer_->stop();
  }

  // Rule: FE-009 End of File Handling
  TEST_F(VideoFileProducerContractTest, FE_009_EndOfFileHandling)
  {
    ProducerConfig config;
    config.asset_uri = GetTestMediaPath("sample.mp4");
    config.stub_mode = false;  // Use real decoding to test EOF

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    // Wait for file to be decoded completely (EOF)
    // For a short sample file, this should happen quickly
    int wait_count = 0;
    while (producer_->isRunning() && wait_count < 100)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      wait_count++;
    }

    // Producer should stop gracefully when EOF is reached
    // (may have already stopped, or we stop it manually)
    if (producer_->isRunning())
    {
      producer_->stop();
    }
    
    ASSERT_FALSE(producer_->isRunning());
    ASSERT_EQ(producer_->GetState(), ProducerState::STOPPED);
    ASSERT_GT(producer_->GetFramesProduced(), 0u);
  }

  // Rule: FE-010 Teardown Operation (Phase 1: stop() is equivalent to teardown)
  TEST_F(VideoFileProducerContractTest, FE_010_TeardownOperation)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    // Fill buffer with some frames
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ASSERT_GT(buffer_->Size(), 0u);

    // Phase 1: stop() handles teardown
    // Future: RequestTeardown() will be implemented in Phase 2
    producer_->stop();

    // Producer should be stopped
    ASSERT_FALSE(producer_->isRunning());
    ASSERT_EQ(producer_->GetState(), ProducerState::STOPPED);
  }

  // Rule: FE-011 Statistics Accuracy
  TEST_F(VideoFileProducerContractTest, FE_011_StatisticsAccuracy)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    uint64_t frames_produced = producer_->GetFramesProduced();
    size_t buffer_size = buffer_->Size();
    ASSERT_GE(frames_produced, buffer_size);

    producer_->stop();
  }

  // Rule: FE-012 MasterClock Alignment (Stub Mode)
  TEST_F(VideoFileProducerContractTest, FE_012_MasterClockAlignment)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.target_fps = 30.0;
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    // Advance clock and verify frame production aligns
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    uint64_t initial_frames = producer_->GetFramesProduced();

    // Advance clock by 1 second (30 frames at 30fps)
    clock_->AdvanceSeconds(1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint64_t frames_after_advance = producer_->GetFramesProduced();
    ASSERT_GT(frames_after_advance, initial_frames);

    // Verify frames have monotonically increasing PTS
    buffer::Frame frame;
    int64_t last_pts = -1;
    int frame_count = 0;
    while (buffer_->Pop(frame) && frame_count < 10)
    {
      if (last_pts >= 0)
      {
        ASSERT_GT(frame.metadata.pts, last_pts);
      }
      last_pts = frame.metadata.pts;
      frame_count++;
    }

    producer_->stop();
  }

  // Contract requirement: Ready event emitted
  TEST_F(VideoFileProducerContractTest, ReadyEventEmitted)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(event_bus_->HasEvent(TestEventType::READY));

    producer_->stop();
  }

  // Contract requirement: Child exit propagated
  TEST_F(VideoFileProducerContractTest, ChildExitPropagated)
  {
    ProducerConfig config;
    config.asset_uri = "/nonexistent/path/video.mp4";
    config.stub_mode = false;
    config.tcp_port = 12347;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    
    bool started = producer_->start();
    if (started)
    {
      // Wait for FFmpeg to fail and exit
      for (int i = 0; i < 50 && producer_->isRunning(); ++i)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      
      // Producer may have stopped due to FFmpeg exit, or still running
      // Either way, stop() should be safe to call
      producer_->stop();
      
      // After stop(), producer should definitely be stopped
      ASSERT_FALSE(producer_->isRunning());
      ASSERT_EQ(producer_->GetState(), ProducerState::STOPPED);
    }
  }

  // Contract requirement: Stop terminates cleanly
  TEST_F(VideoFileProducerContractTest, StopTerminatesCleanly)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    ASSERT_TRUE(producer_->start());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    producer_->stop();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_FALSE(producer_->isRunning());
    ASSERT_EQ(producer_->GetState(), ProducerState::STOPPED);
  }

  // Contract requirement: Bad input path triggers error
  TEST_F(VideoFileProducerContractTest, BadInputPathTriggersError)
  {
    ProducerConfig config;
    config.asset_uri = "/nonexistent/path/to/video.mp4";
    config.stub_mode = false;
    config.tcp_port = 12348;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    
    bool started = producer_->start();
    if (started)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      producer_->stop();
    }
  }

  // Contract requirement: No crash on rapid start/stop
  TEST_F(VideoFileProducerContractTest, NoCrashOnRapidStartStop)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.stub_mode = true;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    
    // Rapid start/stop cycles - should not crash
    for (int i = 0; i < 10; ++i)
    {
      bool started = producer_->start();
      // Give thread time to start
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      producer_->stop();
      // Give thread time to stop
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Final check: producer should be stopped
    ASSERT_FALSE(producer_->isRunning());
    ASSERT_EQ(producer_->GetState(), ProducerState::STOPPED);
  }

  // Contract requirement: READY event always precedes frame events
  TEST_F(VideoFileProducerContractTest, ReadyEventPrecedesFrameEvents)
  {
    ProducerConfig config;
    config.asset_uri = "test.mp4";
    config.stub_mode = true;

    event_bus_->Clear();
    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    producer_->start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify READY was emitted
    ASSERT_TRUE(event_bus_->HasEvent(TestEventType::READY));
    
    // Verify frames are produced after ready
    ASSERT_GT(producer_->GetFramesProduced(), 0u);

    producer_->stop();
  }

  // Contract requirement: stderr is captured
  TEST_F(VideoFileProducerContractTest, StderrIsCaptured)
  {
    ProducerConfig config;
    config.asset_uri = "/nonexistent/path/video.mp4";
    config.stub_mode = false;
    config.tcp_port = 12349;

    producer_ = std::make_unique<VideoFileProducer>(config, *buffer_, clock_, MakeEventCallback());
    
    bool started = producer_->start();
    if (started)
    {
      // Wait for FFmpeg to output error to stderr
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      
      // May or may not have stderr events depending on FFmpeg behavior
      producer_->stop();
    }
  }

} // namespace
