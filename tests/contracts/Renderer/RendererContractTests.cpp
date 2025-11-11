#include "tests/BaseContractTest.h"
#include "tests/contracts/ContractRegistryEnvironment.h"

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
          "FE-002"};
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

    auto renderer = renderer::FrameRenderer::Create(config, buffer);
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

    auto renderer = renderer::FrameRenderer::Create(config, buffer);
    ASSERT_TRUE(renderer->Start());

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    renderer->Stop();

    const auto &stats = renderer->GetStats();
    EXPECT_GT(stats.frames_skipped, 0u);
  }

} // namespace
