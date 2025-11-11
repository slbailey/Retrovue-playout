#include "tests/BaseContractTest.h"
#include "tests/contracts/ContractRegistryEnvironment.h"

#include <cmath>
#include <thread>

#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/decode/FrameProducer.h"
#include "retrovue/telemetry/MetricsExporter.h"
#include "tests/fixtures/ChannelManagerStub.h"
#include "tests/fixtures/MasterClockStub.h"

using namespace retrovue;
using namespace retrovue::tests;
using namespace retrovue::tests::fixtures;

namespace
{

using retrovue::tests::RegisterExpectedDomainCoverage;

const bool kRegisterCoverage = []() {
  RegisterExpectedDomainCoverage("MetricsAndTiming", {"MT-001", "MT-003", "MT-005"});
  return true;
}();

class MetricsAndTimingContractTest : public BaseContractTest
{
protected:
  [[nodiscard]] std::string DomainName() const override
  {
    return "MetricsAndTiming";
  }

  [[nodiscard]] std::vector<std::string> CoveredRuleIds() const override
  {
    return {
        "MT-001",
        "MT-003",
        "MT-005"};
  }
};

// Rule: MT-001 MasterClock Authority (MetricsAndTimingContract.md §MT-001)
TEST_F(MetricsAndTimingContractTest, MT_001_MasterClockAuthority)
{
  MasterClockStub clock;
  const auto initial_time = clock.now_utc_us();
  clock.Advance(10'000);
  const auto advanced_time = clock.now_utc_us();

  EXPECT_GT(advanced_time, initial_time);

  clock.SetDrift(1'500);
  const int64_t scheduled_pts = advanced_time - 5'000;
  const int64_t offset = clock.offset_from_schedule(scheduled_pts);
  EXPECT_EQ(offset, (advanced_time - scheduled_pts) + 1'500);
  EXPECT_EQ(clock.frequency(), 1'000'000);
}

// Rule: MT-003 Frame Cadence (MetricsAndTimingContract.md §MT-003)
TEST_F(MetricsAndTimingContractTest, MT_003_FrameCadenceMaintainsMonotonicPts)
{
  buffer::FrameRingBuffer buffer(12);
  decode::ProducerConfig config;
  config.stub_mode = true;
  config.asset_uri = "contract://metrics/frame_cadence";
  config.target_fps = 30.0;

  decode::FrameProducer producer(config, buffer);
  ASSERT_TRUE(producer.Start());

  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  producer.Stop();

  EXPECT_GE(producer.GetFramesProduced(), 3u);

  buffer::Frame previous;
  bool has_previous = false;

  buffer::Frame frame;
  while (buffer.Pop(frame))
  {
    EXPECT_GT(frame.metadata.duration, 0.0);
    AssertWithinTolerance(frame.metadata.duration,
                          1.0 / config.target_fps,
                          1e-6,
                          "Duration must align with target FPS");

    if (has_previous)
    {
      EXPECT_GE(frame.metadata.pts, previous.metadata.pts);
    }
    previous = frame;
    has_previous = true;
  }
}

// Rule: MT-005 Prometheus Metrics (MetricsAndTimingContract.md §MT-005)
TEST_F(MetricsAndTimingContractTest, MT_005_MetricsExporterReflectsChannelState)
{
  telemetry::MetricsExporter exporter(/*port=*/0);
  ChannelManagerStub manager;

  decode::ProducerConfig config;
  config.stub_mode = true;
  config.asset_uri = "contract://metrics/telemetry";
  config.target_fps = 24.0;

  auto runtime = manager.StartChannel(101, config, exporter, /*buffer_capacity=*/8);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  telemetry::ChannelMetrics metrics{};
  ASSERT_TRUE(exporter.GetChannelMetrics(101, metrics));
  EXPECT_NE(metrics.state, telemetry::ChannelState::STOPPED);
  EXPECT_GE(metrics.buffer_depth_frames, 1u);

  manager.StopChannel(runtime, exporter);
  ASSERT_TRUE(exporter.GetChannelMetrics(101, metrics));
  EXPECT_EQ(metrics.state, telemetry::ChannelState::STOPPED);
  EXPECT_EQ(metrics.buffer_depth_frames, 0u);
}

} // namespace

