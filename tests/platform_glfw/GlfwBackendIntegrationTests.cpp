#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>

#include <gtest/gtest.h>

#include "GlfwBackendTestAccess.hpp"

#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>

namespace Tina::Platform {
namespace {

[[nodiscard]] PlatformBackendCreateParams hiddenWindowParams()
{
    PlatformBackendCreateParams params;
    params.primaryWindow.title = "Tina GLFW Test";
    params.primaryWindow.initialLogicalExtent = {320, 180};
    params.primaryWindow.initiallyVisible = false;
    return params;
}

TEST(GlfwBackendIntegrationTests, HiddenWindowPublishesCoherentPrimarySnapshot)
{
    auto backend = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value()) << poll.error().message;
    ASSERT_TRUE(poll->isContinueFrame());
    ASSERT_NE(poll->frame(), nullptr);
    const WindowFrameSnapshot* primary = poll->frame()->primaryWindow();
    ASSERT_NE(primary, nullptr);
    EXPECT_TRUE(primary->metrics.window.hasValue());
    EXPECT_EQ(primary->metrics.window, primary->input.window);
    EXPECT_EQ(primary->metrics.revision, primary->input.sourceMetricsRevision);
    EXPECT_GT(primary->metrics.logicalExtent.width, 0U);
    EXPECT_GT(primary->metrics.logicalExtent.height, 0U);
    EXPECT_FALSE(primary->metrics.visible);
    EXPECT_TRUE(poll->frame()->gamepads().empty());

    (*backend)->shutdown();
    auto stoppedPoll = (*backend)->pollFrame();
    ASSERT_FALSE(stoppedPoll.has_value());
    EXPECT_EQ(stoppedPoll.error().code, PlatformErrorCode::BackendStopped);
}

TEST(GlfwBackendIntegrationTests, ReportsTheSelectedNativeWindowSystemForTheGate)
{
    auto backend = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    auto runtimePlatform = Detail::glfwRuntimePlatformForTest(**backend);
    ASSERT_TRUE(runtimePlatform.has_value()) << runtimePlatform.error().message;

#if defined(_WIN32)
    EXPECT_EQ(*runtimePlatform, Detail::GlfwRuntimePlatform::Win32);
#elif defined(__APPLE__)
    EXPECT_EQ(*runtimePlatform, Detail::GlfwRuntimePlatform::Cocoa);
#else
    const char* expectedEnvironment = std::getenv("TINA_EXPECT_GLFW_PLATFORM");
    if (expectedEnvironment == nullptr)
    {
        EXPECT_TRUE(*runtimePlatform == Detail::GlfwRuntimePlatform::X11 ||
                    *runtimePlatform == Detail::GlfwRuntimePlatform::Wayland);
    } else if (std::string_view{expectedEnvironment} == "x11")
    {
        EXPECT_EQ(*runtimePlatform, Detail::GlfwRuntimePlatform::X11);
    } else if (std::string_view{expectedEnvironment} == "wayland")
    {
        EXPECT_EQ(*runtimePlatform, Detail::GlfwRuntimePlatform::Wayland);
    } else
    {
        FAIL() << "TINA_EXPECT_GLFW_PLATFORM must be x11 or wayland";
    }
#endif
    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, EnforcesOneActiveProcessBackendAndReleasesLeaseOnShutdown)
{
    auto first = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(first.has_value()) << first.error().message;

    auto second = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, PlatformErrorCode::BackendAlreadyActive);

    (*first)->shutdown();
    auto replacement = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    (*replacement)->shutdown();
}

TEST(GlfwBackendIntegrationTests, RejectsInvalidWindowConfigurationAndReleasesProcessLease)
{
    auto invalidParams = hiddenWindowParams();
    invalidParams.primaryWindow.initialLogicalExtent.width = 0;
    auto invalid = createGlfwPlatformBackend(invalidParams);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, Core::CoreErrorCode::InvalidArgument);

    auto invalidUtf8Params = hiddenWindowParams();
    invalidUtf8Params.primaryWindow.title = "\xC0\xAF";
    auto invalidUtf8 = createGlfwPlatformBackend(invalidUtf8Params);
    ASSERT_FALSE(invalidUtf8.has_value());
    EXPECT_EQ(invalidUtf8.error().code, Core::CoreErrorCode::InvalidArgument);

    auto invalidModeParams = hiddenWindowParams();
    invalidModeParams.primaryWindow.mode = static_cast<WindowMode>(255);
    auto invalidMode = createGlfwPlatformBackend(invalidModeParams);
    ASSERT_FALSE(invalidMode.has_value());
    EXPECT_EQ(invalidMode.error().code, Core::CoreErrorCode::InvalidArgument);

    auto valid = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(valid.has_value()) << valid.error().message;
    (*valid)->shutdown();
}

TEST(GlfwBackendIntegrationTests, NativeCloseRequestReturnsExitWithoutPublishingAPartialFrame)
{
    auto backend = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    ASSERT_TRUE(Detail::requestGlfwCloseForTest(**backend).has_value());

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value()) << poll.error().message;
    EXPECT_TRUE(poll->isExitRequested());
    EXPECT_EQ(poll->frame(), nullptr);
    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, ResizeCommitsOneMetricsRevisionAndMatchingLifecycleEvent)
{
    auto backend = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    auto firstPoll = (*backend)->pollFrame();
    ASSERT_TRUE(firstPoll.has_value()) << firstPoll.error().message;
    const WindowFrameSnapshot* firstWindow = firstPoll->frame()->primaryWindow();
    ASSERT_NE(firstWindow, nullptr);
    const u64 firstRevision = firstWindow->metrics.revision;
    const WindowId windowId = firstWindow->metrics.window;

    constexpr LogicalExtent targetExtent{640, 360};
    ASSERT_TRUE(Detail::resizeGlfwWindowForTest(**backend, targetExtent).has_value());

    u64 previousRevision = firstRevision;
    u64 metricsEventCount = 0;
    bool reachedTarget = false;
    const auto resizeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!reachedTarget && std::chrono::steady_clock::now() < resizeDeadline)
    {
        auto resizedPoll = (*backend)->pollFrame();
        ASSERT_TRUE(resizedPoll.has_value()) << resizedPoll.error().message;
        const WindowFrameSnapshot* resizedWindow = resizedPoll->frame()->primaryWindow();
        ASSERT_NE(resizedWindow, nullptr);

        u32 frameMetricsEventCount = 0;
        for (const PlatformEvent& platformEvent : resizedPoll->frame()->platformEvents())
        {
            const auto* event = std::get_if<WindowMetricsChangedEvent>(&platformEvent.payload);
            if (event == nullptr)
            {
                continue;
            }
            ++frameMetricsEventCount;
            ++metricsEventCount;
            EXPECT_EQ(event->window, windowId);
            EXPECT_EQ(event->metricsRevision, resizedWindow->metrics.revision);
        }
        EXPECT_GE(resizedWindow->metrics.revision, previousRevision);
        EXPECT_EQ(frameMetricsEventCount, resizedWindow->metrics.revision == previousRevision ? 0U : 1U);
        previousRevision = resizedWindow->metrics.revision;
        reachedTarget = resizedWindow->metrics.logicalExtent == targetExtent;
        if (!reachedTarget)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    EXPECT_TRUE(reachedTarget);
    EXPECT_GT(previousRevision, firstRevision);
    EXPECT_GE(metricsEventCount, 1U);
    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, FailedPartialPollRecoversBothStreamsFromFinalSnapshots)
{
    auto backend = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    auto firstPoll = (*backend)->pollFrame();
    ASSERT_TRUE(firstPoll.has_value()) << firstPoll.error().message;

    ASSERT_TRUE(Detail::resizeGlfwWindowForTest(**backend, {640, 360}).has_value());
    ASSERT_TRUE(Detail::failNextGlfwPollForTest(**backend).has_value());
    auto failedPoll = (*backend)->pollFrame();
    ASSERT_FALSE(failedPoll.has_value());
    EXPECT_EQ(failedPoll.error().code, PlatformErrorCode::BackendOperationFailed);

    auto recoveredPoll = (*backend)->pollFrame();
    ASSERT_TRUE(recoveredPoll.has_value()) << recoveredPoll.error().message;
    ASSERT_TRUE(recoveredPoll->isContinueFrame());
    ASSERT_NE(recoveredPoll->frame(), nullptr);
    const WindowFrameSnapshot* recoveredWindow = recoveredPoll->frame()->primaryWindow();
    ASSERT_NE(recoveredWindow, nullptr);
    constexpr LogicalExtent targetExtent{640, 360};

    ASSERT_EQ(recoveredPoll->frame()->inputTransitions().size(), 1U);
    const auto* inputReset = std::get_if<InputStreamReset>(&recoveredPoll->frame()->inputTransitions().front().payload);
    ASSERT_NE(inputReset, nullptr);
    EXPECT_EQ(inputReset->routedWindow, recoveredWindow->metrics.window);
    EXPECT_EQ(inputReset->reason, InputResetReason::BackendRecovery);

    ASSERT_EQ(recoveredPoll->frame()->platformEvents().size(), 1U);
    const auto* eventReset =
        std::get_if<PlatformEventStreamReset>(&recoveredPoll->frame()->platformEvents().front().payload);
    ASSERT_NE(eventReset, nullptr);
    EXPECT_EQ(eventReset->reason, PlatformEventResetReason::BackendRecovery);

    bool reachedTarget = recoveredWindow->metrics.logicalExtent == targetExtent;
    const auto recoveryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!reachedTarget && std::chrono::steady_clock::now() < recoveryDeadline)
    {
        auto convergencePoll = (*backend)->pollFrame();
        ASSERT_TRUE(convergencePoll.has_value()) << convergencePoll.error().message;
        const WindowFrameSnapshot* convergenceWindow = convergencePoll->frame()->primaryWindow();
        ASSERT_NE(convergenceWindow, nullptr);
        reachedTarget = convergenceWindow->metrics.logicalExtent == targetExtent;
        if (!reachedTarget)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    EXPECT_TRUE(reachedTarget);
    (*backend)->shutdown();
}

} // namespace
} // namespace Tina::Platform
