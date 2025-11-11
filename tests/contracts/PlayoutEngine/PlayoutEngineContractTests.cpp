#include "tests/BaseContractTest.h"
#include "tests/contracts/ContractRegistryEnvironment.h"

#include <thread>

#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/decode/FrameProducer.h"
#include "retrovue/telemetry/MetricsExporter.h"
#include "tests/fixtures/ChannelManagerStub.h"

using namespace retrovue;
using namespace retrovue::tests;
using namespace retrovue::tests::fixtures;

namespace
{

using retrovue::tests::RegisterExpectedDomainCoverage;

const bool kRegisterCoverage = []() {
  RegisterExpectedDomainCoverage("PlayoutEngine", {"BC-002", "BC-005", "BC-006"});
  return true;
}();

class PlayoutEngineContractTest : public BaseContractTest
{
protected:
  [[nodiscard]] std::string DomainName() const override
  {
    return "PlayoutEngine";
  }

  [[nodiscard]] std::vector<std::string> CoveredRuleIds() const override
  {
    return {
        "BC-002",
        "BC-005",
        "BC-006"};
  }
};

// Rule: BC-005 Resource Cleanup (PlayoutEngineDomain.md §BC-005)
TEST_F(PlayoutEngineContractTest, BC_005_ChannelStopReleasesResources)
{
  telemetry::MetricsExporter exporter(/*port=*/0);
  ChannelManagerStub manager;

  decode::ProducerConfig config;
  config.stub_mode = true;
  config.asset_uri = "contract://playout/channel";
  config.target_fps = 29.97;

  auto runtime = manager.StartChannel(201, config, exporter, /*buffer_capacity=*/12);

  manager.StopChannel(runtime, exporter);

  telemetry::ChannelMetrics metrics{};
  ASSERT_TRUE(exporter.GetChannelMetrics(201, metrics));
  EXPECT_EQ(metrics.state, telemetry::ChannelState::STOPPED);
  ASSERT_NE(runtime.buffer, nullptr);
  EXPECT_TRUE(runtime.buffer->IsEmpty());
}

// Rule: BC-002 Buffer Depth Guarantees (PlayoutEngineDomain.md §BC-002)
TEST_F(PlayoutEngineContractTest, BC_002_BufferDepthRemainsWithinCapacity)
{
  telemetry::MetricsExporter exporter(/*port=*/0);
  ChannelManagerStub manager;

  decode::ProducerConfig config;
  config.stub_mode = true;
  config.asset_uri = "contract://playout/buffer";
  config.target_fps = 30.0;

  constexpr std::size_t kCapacity = 10;
  auto runtime = manager.StartChannel(202, config, exporter, kCapacity);

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  const auto depth = runtime.buffer->Size();
  EXPECT_LE(depth, kCapacity);
  EXPECT_GE(depth, 1u);

  manager.StopChannel(runtime, exporter);
}

// Rule: BC-006 Monotonic PTS (PlayoutEngineDomain.md §BC-006)
TEST_F(PlayoutEngineContractTest, BC_006_FramePtsRemainMonotonic)
{
  buffer::FrameRingBuffer buffer(/*capacity=*/8);
  decode::ProducerConfig config;
  config.stub_mode = true;
  config.asset_uri = "contract://playout/pts";
  config.target_fps = 30.0;

  decode::FrameProducer producer(config, buffer);
  ASSERT_TRUE(producer.Start());

  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  producer.Stop();

  buffer::Frame previous_frame;
  bool has_previous = false;
  buffer::Frame frame;
  while (buffer.Pop(frame))
  {
    if (has_previous)
    {
      EXPECT_GT(frame.metadata.pts, previous_frame.metadata.pts);
    }
    has_previous = true;
    previous_frame = frame;
  }
  EXPECT_TRUE(has_previous);
}

} // namespace

