#include <gtest/gtest.h>

#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

namespace Tina::Tests {
namespace {

std::unique_ptr<Render::IRenderDevice> createDevice()
{
    auto deviceResult = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    EXPECT_TRUE(deviceResult.has_value());
    if (!deviceResult || *deviceResult == nullptr) {
        return nullptr;
    }
    return std::move(*deviceResult);
}

} // namespace

TEST(NullRenderDeviceTest, EnforcesSubmitPresentPairsAndContiguousFrameIndices)
{
    auto device = createDevice();
    ASSERT_NE(device, nullptr);

    auto presentWithoutSubmit = device->present();
    ASSERT_FALSE(presentWithoutSubmit.has_value());
    EXPECT_EQ(presentWithoutSubmit.error().code, Render::RenderErrorCode::NoFrameSubmitted);

    auto wrongFirstFrame = device->submitFrame(Render::RenderFrame{.frameIndex = 1});
    ASSERT_FALSE(wrongFirstFrame.has_value());
    EXPECT_EQ(wrongFirstFrame.error().code, Render::RenderErrorCode::UnexpectedFrameIndex);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    auto duplicateSubmit = device->submitFrame(Render::RenderFrame{.frameIndex = 0});
    ASSERT_FALSE(duplicateSubmit.has_value());
    EXPECT_EQ(duplicateSubmit.error().code, Render::RenderErrorCode::FrameAlreadyOpen);

    ASSERT_TRUE(device->present().has_value());
    auto repeatedFrame = device->submitFrame(Render::RenderFrame{.frameIndex = 0});
    ASSERT_FALSE(repeatedFrame.has_value());
    EXPECT_EQ(repeatedFrame.error().code, Render::RenderErrorCode::UnexpectedFrameIndex);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 1}).has_value());
    ASSERT_TRUE(device->present().has_value());

    const auto statistics = device->statistics();
    EXPECT_EQ(statistics.submitted, 2U);
    EXPECT_EQ(statistics.presented, 2U);
    EXPECT_EQ(statistics.liveResources, 0U);
}

TEST(NullRenderDeviceTest, RunsThreeHundredFramesWithoutGpuResources)
{
    auto device = createDevice();
    ASSERT_NE(device, nullptr);

    constexpr u64 frameCount = 300;
    for (u64 frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        ASSERT_TRUE(device->submitFrame(Render::RenderFrame{
            .frameIndex = frameIndex,
            .interpolation = 0.5,
            .surfaceSuspended = false,
        }).has_value());
        ASSERT_TRUE(device->present().has_value());
    }

    const auto statistics = device->statistics();
    EXPECT_EQ(statistics.submitted, frameCount);
    EXPECT_EQ(statistics.presented, frameCount);
    EXPECT_EQ(statistics.liveResources, 0U);
}

TEST(NullRenderDeviceTest, RejectsWorkAfterIdempotentShutdown)
{
    auto device = createDevice();
    ASSERT_NE(device, nullptr);

    device->shutdown();
    device->shutdown();

    auto submitResult = device->submitFrame(Render::RenderFrame{});
    ASSERT_FALSE(submitResult.has_value());
    EXPECT_EQ(submitResult.error().code, Render::RenderErrorCode::DeviceStopped);

    auto presentResult = device->present();
    ASSERT_FALSE(presentResult.has_value());
    EXPECT_EQ(presentResult.error().code, Render::RenderErrorCode::DeviceStopped);

    EXPECT_EQ(device->statistics().liveResources, 0U);
}

} // namespace Tina::Tests
