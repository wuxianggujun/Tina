#include <gtest/gtest.h>

#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <limits>

namespace Tina::Tests {
namespace {

std::unique_ptr<Render::IRenderDevice>
createDevice(const Render::RenderDeviceCreateParams& params = Render::RenderDeviceCreateParams{})
{
    auto deviceResult = Render::createNullRenderDevice(params);
    EXPECT_TRUE(deviceResult.has_value());
    if (!deviceResult || *deviceResult == nullptr)
    {
        return nullptr;
    }
    return std::move(*deviceResult);
}

[[nodiscard]] constexpr Render::RenderSurfaceState activeSurface() noexcept
{
    return Render::RenderSurfaceState{
        .surface = {.owner = 1, .index = 0, .generation = 1},
        .framebufferExtent = {640, 480},
        .contentScale = {1.0F, 1.0F},
        .sourceMetricsRevision = 1,
        .surfaceRevision = 1,
        .availability = Render::RenderSurfaceAvailability::Active,
    };
}

[[nodiscard]] constexpr Render::RenderSurfaceState suspendedSurface() noexcept
{
    auto surface = activeSurface();
    surface.framebufferExtent = {0, 0};
    surface.availability = Render::RenderSurfaceAvailability::Suspended;
    return surface;
}

} // namespace

TEST(NullRenderDeviceTest, RejectsStructurallyInvalidInitialWindowSurface)
{
    const auto expectRejected = [](const Render::RenderSurfaceState& surface) {
        auto result =
            Render::createNullRenderDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = surface});
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Render::RenderErrorCode::InvalidSurfaceState);
    };

    auto surface = activeSurface();
    surface.surface = {};
    expectRejected(surface);

    surface = activeSurface();
    surface.surface.index = Render::RenderSurfaceId::InvalidIndex;
    expectRejected(surface);

    surface = activeSurface();
    surface.sourceMetricsRevision = 0;
    expectRejected(surface);

    surface = activeSurface();
    surface.surfaceRevision = 0;
    expectRejected(surface);

    surface = activeSurface();
    surface.contentScale.x = 0.0F;
    expectRejected(surface);

    surface = activeSurface();
    surface.contentScale.x = -1.0F;
    expectRejected(surface);

    surface = activeSurface();
    surface.contentScale.x = (std::numeric_limits<float>::quiet_NaN)();
    expectRejected(surface);

    surface = activeSurface();
    surface.contentScale.y = (std::numeric_limits<float>::infinity)();
    expectRejected(surface);

    surface = activeSurface();
    surface.framebufferExtent.width = 0;
    expectRejected(surface);

    surface = activeSurface();
    surface.availability = static_cast<Render::RenderSurfaceAvailability>(255);
    expectRejected(surface);
}

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
    for (u64 frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        ASSERT_TRUE(device
                        ->submitFrame(Render::RenderFrame{
                            .frameIndex = frameIndex,
                            .interpolation = 0.5,
                            .primaryWindowSurface = std::nullopt,
                        })
                        .has_value());
        ASSERT_TRUE(device->present().has_value());
    }

    const auto statistics = device->statistics();
    EXPECT_EQ(statistics.submitted, frameCount);
    EXPECT_EQ(statistics.presented, frameCount);
    EXPECT_EQ(statistics.liveResources, 0U);
}

TEST(NullRenderDeviceTest, SuspendedWindowSurfaceSkipsSubmissionButKeepsEngineFrameSequence)
{
    auto suspended = suspendedSurface();
    auto device = createDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = suspended});
    ASSERT_NE(device, nullptr);

    constexpr u64 suspendedFrameCount = 300;
    for (u64 frameIndex = 0; frameIndex < suspendedFrameCount; ++frameIndex)
    {
        suspended.sourceMetricsRevision = frameIndex + 1;
        auto skipped = device->submitFrame(Render::RenderFrame{
            .frameIndex = frameIndex,
            .primaryWindowSurface = suspended,
        });
        ASSERT_TRUE(skipped.has_value());
        EXPECT_EQ(skipped->kind, Render::RenderFrameSubmissionKind::SkippedSuspendedSurface);
        EXPECT_FALSE(skipped->requiresPresent());
    }

    auto active = suspended;
    active.framebufferExtent = {640, 480};
    active.sourceMetricsRevision = suspendedFrameCount + 1;
    active.surfaceRevision = 2;
    active.availability = Render::RenderSurfaceAvailability::Active;
    auto submitted = device->submitFrame(Render::RenderFrame{
        .frameIndex = suspendedFrameCount,
        .primaryWindowSurface = active,
    });
    ASSERT_TRUE(submitted.has_value());
    EXPECT_EQ(submitted->submissionIndex, 0U);
    ASSERT_TRUE(device->present().has_value());

    const auto statistics = device->statistics();
    EXPECT_EQ(statistics.submitted, 1U);
    EXPECT_EQ(statistics.presented, 1U);
    EXPECT_EQ(statistics.skippedSuspendedSurfaceFrames, suspendedFrameCount);
}

TEST(NullRenderDeviceTest, RejectsWindowSurfaceCompositionPresenceChanges)
{
    const auto initial = activeSurface();
    auto surfaceDevice = createDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = initial});
    ASSERT_NE(surfaceDevice, nullptr);

    auto missingSurface = surfaceDevice->submitFrame(Render::RenderFrame{.frameIndex = 0});
    ASSERT_FALSE(missingSurface.has_value());
    EXPECT_EQ(missingSurface.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    auto recoveredSurface =
        surfaceDevice->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = initial});
    ASSERT_TRUE(recoveredSurface.has_value());
    ASSERT_TRUE(surfaceDevice->present().has_value());

    auto surfaceFreeDevice = createDevice();
    ASSERT_NE(surfaceFreeDevice, nullptr);
    auto unexpectedSurface =
        surfaceFreeDevice->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = initial});
    ASSERT_FALSE(unexpectedSurface.has_value());
    EXPECT_EQ(unexpectedSurface.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    ASSERT_TRUE(surfaceFreeDevice->submitFrame(Render::RenderFrame{.frameIndex = 0}).has_value());
    ASSERT_TRUE(surfaceFreeDevice->present().has_value());
}

TEST(NullRenderDeviceTest, RejectsWindowSurfaceIdentityChangesWithoutConsumingTheFrame)
{
    const auto initial = activeSurface();
    auto device = createDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = initial});
    ASSERT_NE(device, nullptr);

    auto changedIdentity = initial;
    changedIdentity.surface.generation = 2;
    auto invalid = device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = changedIdentity});
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = initial}).has_value());
    ASSERT_TRUE(device->present().has_value());
}

TEST(NullRenderDeviceTest, RejectsWindowSurfaceRevisionRollbackWithoutMutatingCommittedState)
{
    auto initial = activeSurface();
    initial.sourceMetricsRevision = 4;
    initial.surfaceRevision = 4;
    auto device = createDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = initial});
    ASSERT_NE(device, nullptr);

    auto sourceRevisionRollback = initial;
    sourceRevisionRollback.sourceMetricsRevision = 3;
    auto invalidSourceRevision =
        device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = sourceRevisionRollback});
    ASSERT_FALSE(invalidSourceRevision.has_value());
    EXPECT_EQ(invalidSourceRevision.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    auto surfaceRevisionRollback = initial;
    surfaceRevisionRollback.surfaceRevision = 3;
    auto invalidSurfaceRevision =
        device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = surfaceRevisionRollback});
    ASSERT_FALSE(invalidSurfaceRevision.has_value());
    EXPECT_EQ(invalidSurfaceRevision.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    ASSERT_TRUE(device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = initial}).has_value());
    ASSERT_TRUE(device->present().has_value());
}

TEST(NullRenderDeviceTest, RequiresSurfaceRevisionToMatchCommittedFactChanges)
{
    const auto initial = activeSurface();
    auto device = createDevice(Render::RenderDeviceCreateParams{.initialPrimaryWindowSurface = initial});
    ASSERT_NE(device, nullptr);

    auto changedFactsWithoutRevision = initial;
    changedFactsWithoutRevision.framebufferExtent = {800, 600};
    changedFactsWithoutRevision.sourceMetricsRevision = 2;
    auto missingRevision =
        device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = changedFactsWithoutRevision});
    ASSERT_FALSE(missingRevision.has_value());
    EXPECT_EQ(missingRevision.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    auto changedFactsWithoutNewMetrics = changedFactsWithoutRevision;
    changedFactsWithoutNewMetrics.sourceMetricsRevision = initial.sourceMetricsRevision;
    changedFactsWithoutNewMetrics.surfaceRevision = 2;
    auto staleMetrics = device->submitFrame(
        Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = changedFactsWithoutNewMetrics});
    ASSERT_FALSE(staleMetrics.has_value());
    EXPECT_EQ(staleMetrics.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    auto changedFactsWithSkippedRevision = changedFactsWithoutRevision;
    changedFactsWithSkippedRevision.surfaceRevision = 3;
    auto skippedRevision = device->submitFrame(
        Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = changedFactsWithSkippedRevision});
    ASSERT_FALSE(skippedRevision.has_value());
    EXPECT_EQ(skippedRevision.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    auto changedFacts = changedFactsWithoutRevision;
    changedFacts.surfaceRevision = 2;
    ASSERT_TRUE(
        device->submitFrame(Render::RenderFrame{.frameIndex = 0, .primaryWindowSurface = changedFacts}).has_value());
    ASSERT_TRUE(device->present().has_value());

    auto changedRevisionWithoutFacts = changedFacts;
    changedRevisionWithoutFacts.sourceMetricsRevision = 3;
    changedRevisionWithoutFacts.surfaceRevision = 3;
    auto spuriousRevision =
        device->submitFrame(Render::RenderFrame{.frameIndex = 1, .primaryWindowSurface = changedRevisionWithoutFacts});
    ASSERT_FALSE(spuriousRevision.has_value());
    EXPECT_EQ(spuriousRevision.error().code, Render::RenderErrorCode::InvalidSurfaceState);

    changedFacts.sourceMetricsRevision = 3;
    ASSERT_TRUE(
        device->submitFrame(Render::RenderFrame{.frameIndex = 1, .primaryWindowSurface = changedFacts}).has_value());
    ASSERT_TRUE(device->present().has_value());
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
