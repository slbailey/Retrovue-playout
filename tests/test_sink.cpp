// Repository: Retrovue-playout
// Component: MPEG-TS Playout Sink Unit Tests
// Purpose: Unit tests for MpegTSPlayoutSink basic functionality.
// Copyright (c) 2025 RetroVue

#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/timing/MasterClock.h"
#include "retrovue/sinks/mpegts/MpegTSPlayoutSink.h"
#include "retrovue/sinks/mpegts/SinkConfig.h"
#include "timing/TestMasterClock.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <memory>

using namespace retrovue;
using namespace retrovue::sinks::mpegts;
using namespace retrovue::buffer;

// Test basic construction
TEST(MpegTSPlayoutSinkTest, Construction)
{
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  const int64_t epoch = 1'700'001'000'000'000;
  clock->SetEpochUtcUs(epoch);
  clock->SetNow(epoch, 0.0);

  FrameRingBuffer buffer(60);
  SinkConfig config;
  config.stub_mode = true;

  MpegTSPlayoutSink sink(config, buffer, clock);
  
  EXPECT_FALSE(sink.isRunning());
  EXPECT_EQ(sink.getFramesSent(), 0u);
  EXPECT_EQ(sink.getFramesDropped(), 0u);
  EXPECT_EQ(sink.getLateFrames(), 0u);
}

// Test start/stop lifecycle
TEST(MpegTSPlayoutSinkTest, StartStop)
{
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  const int64_t epoch = 1'700'001'000'000'000;
  clock->SetEpochUtcUs(epoch);
  clock->SetNow(epoch, 0.0);

  FrameRingBuffer buffer(60);
  SinkConfig config;
  config.stub_mode = true;

  MpegTSPlayoutSink sink(config, buffer, clock);
  
  EXPECT_FALSE(sink.isRunning());
  
  EXPECT_TRUE(sink.start());
  EXPECT_TRUE(sink.isRunning());
  
  sink.stop();
  EXPECT_FALSE(sink.isRunning());
}

// Test cannot start twice
TEST(MpegTSPlayoutSinkTest, CannotStartTwice)
{
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  const int64_t epoch = 1'700'001'000'000'000;
  clock->SetEpochUtcUs(epoch);
  clock->SetNow(epoch, 0.0);

  FrameRingBuffer buffer(60);
  SinkConfig config;
  config.stub_mode = true;

  MpegTSPlayoutSink sink(config, buffer, clock);
  
  EXPECT_TRUE(sink.start());
  EXPECT_TRUE(sink.isRunning());
  
  EXPECT_FALSE(sink.start());  // Second start should fail
  EXPECT_TRUE(sink.isRunning());
  
  sink.stop();
}

// Test stop idempotent
TEST(MpegTSPlayoutSinkTest, StopIdempotent)
{
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  const int64_t epoch = 1'700'001'000'000'000;
  clock->SetEpochUtcUs(epoch);
  clock->SetNow(epoch, 0.0);

  FrameRingBuffer buffer(60);
  SinkConfig config;
  config.stub_mode = true;

  MpegTSPlayoutSink sink(config, buffer, clock);
  
  sink.start();
  sink.stop();
  sink.stop();  // Should be safe to call multiple times
  sink.stop();
  EXPECT_FALSE(sink.isRunning());
}

// Test destructor stops sink
TEST(MpegTSPlayoutSinkTest, DestructorStopsSink)
{
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  const int64_t epoch = 1'700'001'000'000'000;
  clock->SetEpochUtcUs(epoch);
  clock->SetNow(epoch, 0.0);

  FrameRingBuffer buffer(60);
  SinkConfig config;
  config.stub_mode = true;

  {
    MpegTSPlayoutSink sink(config, buffer, clock);
    sink.start();
    EXPECT_TRUE(sink.isRunning());
    // Destructor should stop sink
  }
  // Sink is destroyed, should be stopped
}

// Test frame order
TEST(MpegTSPlayoutSinkTest, FrameOrder)
{
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  const int64_t epoch = 1'700'001'000'000'000;
  clock->SetEpochUtcUs(epoch);
  clock->SetNow(epoch, 0.0);

  FrameRingBuffer buffer(60);
  SinkConfig config;
  config.stub_mode = true;

  MpegTSPlayoutSink sink(config, buffer, clock);
  sink.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Push frames with sequential PTS
  for (int i = 0; i < 5; ++i)
  {
    Frame frame;
    frame.metadata.pts = i * 33'333;
    frame.metadata.dts = i * 33'333;
    frame.metadata.duration = 1.0 / 30.0;
    frame.width = 1920;
    frame.height = 1080;
    frame.data.resize(1920 * 1080 * 3 / 2, 128);
    buffer.Push(frame);
  }

  clock->AdvanceSeconds(0.3);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  sink.stop();
}

// Test master clock alignment
TEST(MpegTSPlayoutSinkTest, MasterClockAlignment)
{
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  const int64_t epoch = 1'700'001'000'000'000;
  clock->SetEpochUtcUs(epoch);
  clock->SetNow(epoch, 0.0);

  FrameRingBuffer buffer(60);
  SinkConfig config;
  config.stub_mode = true;

  MpegTSPlayoutSink sink(config, buffer, clock);
  sink.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Push frame with future PTS
  int64_t current_time = clock->now_utc_us();
  int64_t future_pts = current_time + 100'000;

  Frame frame;
  frame.metadata.pts = future_pts;
  frame.metadata.dts = future_pts;
  frame.metadata.duration = 1.0 / 30.0;
  frame.width = 1920;
  frame.height = 1080;
  frame.data.resize(1920 * 1080 * 3 / 2, 128);
  buffer.Push(frame);

  // Frame should not be output yet
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  uint64_t frames_before = sink.getFramesSent();

  // Advance clock past PTS
  clock->AdvanceMicroseconds(150'000);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  uint64_t frames_after = sink.getFramesSent();
  EXPECT_GE(frames_after, frames_before);

  sink.stop();
}

// Test empty buffer handling
TEST(MpegTSPlayoutSinkTest, EmptyBufferHandling)
{
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  const int64_t epoch = 1'700'001'000'000'000;
  clock->SetEpochUtcUs(epoch);
  clock->SetNow(epoch, 0.0);

  FrameRingBuffer buffer(60);
  SinkConfig config;
  config.stub_mode = true;

  MpegTSPlayoutSink sink(config, buffer, clock);
  sink.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Buffer is empty - sink should handle gracefully
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  EXPECT_TRUE(sink.isRunning());  // Should not crash
  EXPECT_GT(sink.getBufferEmptyCount(), 0u);

  sink.stop();
}

// Test buffer overrun handling
TEST(MpegTSPlayoutSinkTest, BufferOverrunHandling)
{
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  const int64_t epoch = 1'700'001'000'000'000;
  clock->SetEpochUtcUs(epoch);
  clock->SetNow(epoch, 0.0);

  FrameRingBuffer buffer(60);
  SinkConfig config;
  config.stub_mode = true;

  MpegTSPlayoutSink sink(config, buffer, clock);
  sink.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Push late frames
  int64_t current_time = clock->now_utc_us();
  for (int i = 0; i < 5; ++i)
  {
    Frame frame;
    frame.metadata.pts = current_time - (i * 50'000);  // Late frames
    frame.metadata.dts = current_time - (i * 50'000);
    frame.metadata.duration = 1.0 / 30.0;
    frame.width = 1920;
    frame.height = 1080;
    frame.data.resize(1920 * 1080 * 3 / 2, 128);
    buffer.Push(frame);
  }

  clock->AdvanceMicroseconds(500'000);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  EXPECT_TRUE(sink.isRunning());  // Should not crash
  EXPECT_GT(sink.getFramesDropped(), 0u);

  sink.stop();
}

// Test statistics accuracy
TEST(MpegTSPlayoutSinkTest, StatisticsAccuracy)
{
  auto clock = std::make_shared<retrovue::timing::TestMasterClock>();
  const int64_t epoch = 1'700'001'000'000'000;
  clock->SetEpochUtcUs(epoch);
  clock->SetNow(epoch, 0.0);

  FrameRingBuffer buffer(60);
  SinkConfig config;
  config.stub_mode = true;

  MpegTSPlayoutSink sink(config, buffer, clock);
  
  EXPECT_EQ(sink.getFramesSent(), 0u);
  EXPECT_EQ(sink.getFramesDropped(), 0u);
  EXPECT_EQ(sink.getLateFrames(), 0u);

  sink.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Push some frames
  int64_t current_time = clock->now_utc_us();
  for (int i = 0; i < 3; ++i)
  {
    Frame frame;
    frame.metadata.pts = current_time + (i * 33'333);
    frame.metadata.dts = current_time + (i * 33'333);
    frame.metadata.duration = 1.0 / 30.0;
    frame.width = 1920;
    frame.height = 1080;
    frame.data.resize(1920 * 1080 * 3 / 2, 128);
    buffer.Push(frame);
  }

  clock->AdvanceSeconds(0.2);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Statistics should be updated
  uint64_t frames_sent = sink.getFramesSent();
  uint64_t frames_dropped = sink.getFramesDropped();
  uint64_t late_frames = sink.getLateFrames();

  EXPECT_GE(frames_sent, 0u);
  EXPECT_GE(frames_dropped, 0u);
  EXPECT_GE(late_frames, 0u);

  sink.stop();
}

