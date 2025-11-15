#include "../../BaseContractTest.h"
#include "../ContractRegistryEnvironment.h"

#include <memory>
#include <thread>

#include "retrovue/buffer/FrameRingBuffer.h"
#include "retrovue/renderer/FrameRenderer.h"

using namespace retrovue;
using namespace retrovue::tests;

namespace
{

  using retrovue::tests::RegisterExpectedDomainCoverage;

  const bool kRegisterCoverage = []()
  {
    RegisterExpectedDomainCoverage("Renderer", {"FE-001", "FE-002"});
    return true;
  }();

  class RendererContractTest : public BaseContractTest
  {
  protected:
    [[nodiscard]] std::string DomainName() const override
    {
      return "Renderer";
    }

    [[nodiscard]] std::vector<std::string> CoveredRuleIds() const override
    {
      return {
          "FE-001",
          "FE-002",
          "FE-003"};
    }
  };

  // Rule: FE-001 Frame Consumption Timing (RendererContract.md §FE-001)
  TEST_F(RendererContractTest, FE_001_HeadlessRendererConsumesFramesInOrder)
  {
    buffer::FrameRingBuffer buffer(6);

    for (int i = 0; i < 3; ++i)
    {
      buffer::Frame frame;
      frame.metadata.pts = i;
      frame.metadata.dts = i;
      frame.metadata.duration = 1.0 / 30.0;
      frame.width = 1280;
      frame.height = 720;
      ASSERT_TRUE(buffer.Push(frame));
    }

    renderer::RenderConfig config;
    config.mode = renderer::RenderMode::HEADLESS;

    std::shared_ptr<timing::MasterClock> clock;
    std::shared_ptr<telemetry::MetricsExporter> metrics;
    auto renderer = renderer::FrameRenderer::Create(config, buffer, clock, metrics, /*channel_id=*/0);
    ASSERT_TRUE(renderer->Start());

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    renderer->Stop();

    const auto &stats = renderer->GetStats();
    EXPECT_GE(stats.frames_rendered, 3u);
  }

  // Rule: FE-002 Empty Buffer Handling (RendererContract.md §FE-002)
  TEST_F(RendererContractTest, FE_002_HeadlessRendererHandlesEmptyBufferGracefully)
  {
    buffer::FrameRingBuffer buffer(4);
    renderer::RenderConfig config;
    config.mode = renderer::RenderMode::HEADLESS;

    std::shared_ptr<timing::MasterClock> clock;
    std::shared_ptr<telemetry::MetricsExporter> metrics;
    auto renderer = renderer::FrameRenderer::Create(config, buffer, clock, metrics, /*channel_id=*/0);
    ASSERT_TRUE(renderer->Start());

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    renderer->Stop();

    const auto &stats = renderer->GetStats();
    EXPECT_GT(stats.frames_skipped, 0u);
  }

  // Rule: FE-003 Pipeline Reset (RendererContract.md §FE-003)
  // Note: resetPipeline() is NOT called during seamless producer switching.
  // It may be used for other scenarios (e.g., plan updates, error recovery).
  // During seamless switching, renderer continues reading from buffer without reset.
  TEST_F(RendererContractTest, FE_003_PipelineResetClearsBuffersAndResetsTimestamps)
  {
    buffer::FrameRingBuffer buffer(10);

    // Fill buffer with some frames
    // Note: In production, FrameRouter pulls from producer and writes to buffer.
    // For this test, we directly push frames to test resetPipeline behavior.
    for (int i = 0; i < 5; ++i)
    {
      buffer::Frame frame;
      frame.metadata.pts = i * 33'366;
      frame.metadata.dts = i * 33'366;
      frame.metadata.duration = 1.0 / 30.0;
      frame.width = 1920;
      frame.height = 1080;
      ASSERT_TRUE(buffer.Push(frame));
    }

    EXPECT_EQ(buffer.Size(), 5u);

    renderer::RenderConfig config;
    config.mode = renderer::RenderMode::HEADLESS;

    std::shared_ptr<timing::MasterClock> clock;
    std::shared_ptr<telemetry::MetricsExporter> metrics;
    auto renderer = renderer::FrameRenderer::Create(config, buffer, clock, metrics, /*channel_id=*/0);
    ASSERT_TRUE(renderer->Start());

    // Let renderer consume some frames
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Reset pipeline (used for plan updates, error recovery, NOT for seamless switching)
    renderer->resetPipeline();

    // Buffer should be cleared
    EXPECT_EQ(buffer.Size(), 0u);

    renderer->Stop();
  }

} // namespace
