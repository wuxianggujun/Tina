#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>

#include <gtest/gtest.h>

#include "GlfwBackendTestAccess.hpp"
#include "WindowSurfaceLeaseAccess.hpp"

#include <array>
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

TEST(GlfwBackendIntegrationTests, PointerButtonAndWheelKeepEventTimeLogicalPosition)
{
    auto backend = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;

    constexpr double CursorAX = 32.0;
    constexpr double CursorAY = 48.0;
    constexpr double CursorBX = 96.0;
    constexpr double CursorBY = 128.0;
    const std::array events{
        Detail::GlfwPointerInjection{
            .kind = Detail::GlfwPointerInjectionKind::CursorPosition,
            .logicalX = CursorAX,
            .logicalY = CursorAY,
        },
        Detail::GlfwPointerInjection{
            .kind = Detail::GlfwPointerInjectionKind::Button,
            .button = PointerButton::Primary,
            .transition = DigitalTransition::Down,
        },
        Detail::GlfwPointerInjection{
            .kind = Detail::GlfwPointerInjectionKind::Wheel,
            .wheelDeltaX = 1.25,
            .wheelDeltaY = -2.5,
        },
        Detail::GlfwPointerInjection{
            .kind = Detail::GlfwPointerInjectionKind::CursorPosition,
            .logicalX = CursorBX,
            .logicalY = CursorBY,
        },
    };
    ASSERT_TRUE(Detail::queueGlfwPointerEventsForNextPollForTest(**backend, events).has_value());

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value()) << poll.error().message;
    ASSERT_TRUE(poll->isContinueFrame());
    ASSERT_NE(poll->frame(), nullptr);
    const auto transitions = poll->frame()->inputTransitions();
    ASSERT_EQ(transitions.size(), 4U);

    const auto* firstMove = std::get_if<PointerMoveTransition>(&transitions[0].payload);
    ASSERT_NE(firstMove, nullptr);
    EXPECT_DOUBLE_EQ(firstMove->logicalX, CursorAX);
    EXPECT_DOUBLE_EQ(firstMove->logicalY, CursorAY);

    const auto* button = std::get_if<PointerButtonTransition>(&transitions[1].payload);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->button, PointerButton::Primary);
    EXPECT_EQ(button->state, DigitalTransition::Down);
    EXPECT_DOUBLE_EQ(button->logicalX, CursorAX);
    EXPECT_DOUBLE_EQ(button->logicalY, CursorAY);

    const auto* wheel = std::get_if<PointerWheelTransition>(&transitions[2].payload);
    ASSERT_NE(wheel, nullptr);
    EXPECT_DOUBLE_EQ(wheel->logicalX, CursorAX);
    EXPECT_DOUBLE_EQ(wheel->logicalY, CursorAY);
    EXPECT_DOUBLE_EQ(wheel->deltaX, 1.25);
    EXPECT_DOUBLE_EQ(wheel->deltaY, -2.5);

    const auto* secondMove = std::get_if<PointerMoveTransition>(&transitions[3].payload);
    ASSERT_NE(secondMove, nullptr);
    EXPECT_DOUBLE_EQ(secondMove->logicalX, CursorBX);
    EXPECT_DOUBLE_EQ(secondMove->logicalY, CursorBY);
    EXPECT_DOUBLE_EQ(secondMove->deltaX, CursorBX - CursorAX);
    EXPECT_DOUBLE_EQ(secondMove->deltaY, CursorBY - CursorAY);

    const WindowFrameSnapshot* primary = poll->frame()->primaryWindow();
    ASSERT_NE(primary, nullptr);
    EXPECT_DOUBLE_EQ(primary->input.pointer.logicalX, CursorBX);
    EXPECT_DOUBLE_EQ(primary->input.pointer.logicalY, CursorBY);
    EXPECT_TRUE(primary->input.pointer.isHeld(PointerButton::Primary));
    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, SuspendedPacingWaitsForEventsAcrossThreeHundredFrames)
{
    auto backend = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    ASSERT_TRUE(Detail::forceGlfwSuspendedWaitPathForTest(**backend, 0.000001).has_value());

    constexpr u64 FrameCount = 300;
    for (u64 frameIndex = 0; frameIndex < FrameCount; ++frameIndex)
    {
        auto poll = (*backend)->pollFrame();
        ASSERT_TRUE(poll.has_value()) << poll.error().message;
        ASSERT_TRUE(poll->isContinueFrame());
    }

    auto stats = Detail::glfwEventPumpStatsForTest(**backend);
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_EQ(stats->waitEventsTimeoutCalls, FrameCount);
    EXPECT_EQ(stats->pollEventsCalls, 0U);
    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, IndependentFactoryDoesNotExposeWindowSurfaceIntegration)
{
    auto backend = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;

    EXPECT_EQ(dynamic_cast<Integration::IWindowSurfacePlatformBackend*>(backend->get()), nullptr);
    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, WindowSurfaceFactoryDefersPublicationAndPinsPrivateNativeBinding)
{
    auto params = hiddenWindowParams();
    params.primaryWindow.initiallyVisible = true;
    auto backend = createGlfwWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value()) << backend.error().message;

    auto visibleBeforePublish = Detail::glfwWindowVisibleForTest(**backend);
    ASSERT_TRUE(visibleBeforePublish.has_value()) << visibleBeforePublish.error().message;
    EXPECT_FALSE(*visibleBeforePublish);

    auto initialSurface = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(initialSurface.has_value()) << initialSurface.error().message;
    EXPECT_TRUE(initialSurface->surface.hasValue());
    EXPECT_TRUE(initialSurface->sourceWindow.hasValue());
    EXPECT_EQ(initialSurface->surfaceRevision, 1U);

    auto lease = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_TRUE(lease.has_value()) << lease.error().message;
    EXPECT_EQ(lease->surface(), initialSurface->surface);
    auto duplicate = (*backend)->acquirePrimaryWindowSurfaceLease();
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error().code, PlatformErrorCode::WindowSurfaceLeaseAlreadyAcquired);

    auto binding = Integration::Detail::NativeWindowSurfaceLeaseAccess::decode(*lease);
    ASSERT_TRUE(binding.has_value()) << binding.error().message;
    EXPECT_NE(binding->nativeWindow, 0U);
#if defined(_WIN32)
    EXPECT_EQ(binding->kind, Integration::Detail::NativeWindowBindingKind::Win32);
#endif

    ASSERT_TRUE((*backend)->publishPrimaryWindow().has_value());
    auto visibleAfterPublish = Detail::glfwWindowVisibleForTest(**backend);
    ASSERT_TRUE(visibleAfterPublish.has_value()) << visibleAfterPublish.error().message;
    EXPECT_TRUE(*visibleAfterPublish);

    lease = {};
    (*backend)->shutdown();
}

TEST(GlfwBackendDeathTest, ShuttingDownWithActiveWindowSurfaceLeaseTerminates)
{
    EXPECT_DEATH(
        {
            auto backend = createGlfwWindowSurfacePlatformBackend(hiddenWindowParams());
            if (!backend.has_value())
            {
                std::abort();
            }
            auto lease = (*backend)->acquirePrimaryWindowSurfaceLease();
            if (!lease.has_value())
            {
                std::abort();
            }
            (*backend)->shutdown();
        },
        ".*");
}

TEST(GlfwBackendIntegrationTests, WindowSurfaceSnapshotMatchesCommittedMetricsAndOnlyRevisesOnSurfaceFacts)
{
    auto backend = createGlfwWindowSurfacePlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    ASSERT_TRUE((*backend)->publishPrimaryWindow().has_value());

    auto firstPoll = (*backend)->pollFrame();
    ASSERT_TRUE(firstPoll.has_value()) << firstPoll.error().message;
    const WindowFrameSnapshot* firstWindow = firstPoll->frame()->primaryWindow();
    ASSERT_NE(firstWindow, nullptr);
    auto firstSurface = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(firstSurface.has_value()) << firstSurface.error().message;
    EXPECT_EQ(firstSurface->sourceWindow, firstWindow->metrics.window);
    EXPECT_EQ(firstSurface->sourceMetricsRevision, firstWindow->metrics.revision);
    EXPECT_EQ(firstSurface->framebufferExtent, firstWindow->metrics.framebufferExtent);
    EXPECT_EQ(firstSurface->contentScale, firstWindow->metrics.contentScale);

    auto stablePoll = (*backend)->pollFrame();
    ASSERT_TRUE(stablePoll.has_value()) << stablePoll.error().message;
    auto stableSurface = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(stableSurface.has_value()) << stableSurface.error().message;
    EXPECT_EQ(stableSurface->surfaceRevision, firstSurface->surfaceRevision);

    ASSERT_TRUE(Detail::resizeGlfwWindowForTest(**backend, {640, 360}).has_value());
    bool revised = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!revised && std::chrono::steady_clock::now() < deadline)
    {
        auto poll = (*backend)->pollFrame();
        ASSERT_TRUE(poll.has_value()) << poll.error().message;
        const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
        ASSERT_NE(window, nullptr);
        auto surface = (*backend)->primaryWindowSurfaceSnapshot();
        ASSERT_TRUE(surface.has_value()) << surface.error().message;
        EXPECT_EQ(surface->sourceMetricsRevision, window->metrics.revision);
        EXPECT_EQ(surface->framebufferExtent, window->metrics.framebufferExtent);
        revised = surface->surfaceRevision > firstSurface->surfaceRevision;
        if (!revised)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    EXPECT_TRUE(revised);
    (*backend)->shutdown();
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
