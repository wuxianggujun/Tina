#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>

#include <gtest/gtest.h>

#include "GlfwBackendTestAccess.hpp"
#include "WindowSurfaceLeaseAccess.hpp"

#include <array>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <span>
#include <string_view>
#include <thread>

#if defined(_WIN32)
// GetClipCursor reads the desktop-wide cursor clip, which is the state a leaked pointer lock
// leaves behind. No Tina header exposes it, so the test reaches for the OS directly.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

[[nodiscard]] const FileDropEvent* findFileDrop(const PlatformFrameView& frame) noexcept
{
    for (const PlatformEvent& event : frame.platformEvents())
    {
        if (const auto* drop = std::get_if<FileDropEvent>(&event.payload); drop != nullptr)
        {
            return drop;
        }
    }
    return nullptr;
}

void expectInvalidFileDropPayload(PlatformBackendCreateParams params,
                                  Detail::GlfwFileDropInjection injection)
{
    params.acceptFileDropEvents = true;
    auto backend = createGlfwPlatformBackend(params);
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    ASSERT_TRUE(Detail::queueGlfwFileDropForNextPollForTest(**backend, injection).has_value());

    auto poll = (*backend)->pollFrame();
    ASSERT_FALSE(poll.has_value());
    EXPECT_EQ(poll.error().code, PlatformErrorCode::CallbackFrameAssemblyFailed);
    ASSERT_TRUE(poll.error().nativeCode.has_value());
    EXPECT_EQ(*poll.error().nativeCode,
              static_cast<i64>(Detail::GlfwCallbackAssemblyFailure::InvalidPayload));
    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, InitialPrimaryWindowMetricsDoesNotPumpAndPublishesMatchingFirstFrameEvent)
{
    auto backend = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;

    auto statsBeforeSeed = Detail::glfwEventPumpStatsForTest(**backend);
    ASSERT_TRUE(statsBeforeSeed.has_value()) << statsBeforeSeed.error().message;
    EXPECT_EQ(statsBeforeSeed->pollEventsCalls, 0U);
    EXPECT_EQ(statsBeforeSeed->waitEventsTimeoutCalls, 0U);

    auto startupMetrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(startupMetrics.has_value()) << startupMetrics.error().message;
    ASSERT_TRUE(startupMetrics->has_value());
    EXPECT_TRUE((*startupMetrics)->window.hasValue());
    EXPECT_GT((*startupMetrics)->logicalExtent.width, 0U);
    EXPECT_GT((*startupMetrics)->logicalExtent.height, 0U);
    EXPECT_FALSE((*startupMetrics)->visible);
#if defined(_WIN32)
    EXPECT_EQ((*startupMetrics)->logicalExtent,
              hiddenWindowParams().primaryWindow.initialLogicalExtent);
    EXPECT_NEAR(static_cast<double>((*startupMetrics)->framebufferExtent.width),
                static_cast<double>((*startupMetrics)->logicalExtent.width) *
                    (*startupMetrics)->contentScale.x,
                2.0);
    EXPECT_NEAR(static_cast<double>((*startupMetrics)->framebufferExtent.height),
                static_cast<double>((*startupMetrics)->logicalExtent.height) *
                    (*startupMetrics)->contentScale.y,
                2.0);
#endif

    auto statsAfterSeed = Detail::glfwEventPumpStatsForTest(**backend);
    ASSERT_TRUE(statsAfterSeed.has_value()) << statsAfterSeed.error().message;
    EXPECT_EQ(statsAfterSeed->pollEventsCalls, 0U);
    EXPECT_EQ(statsAfterSeed->waitEventsTimeoutCalls, 0U);

    auto firstPoll = (*backend)->pollFrame();
    ASSERT_TRUE(firstPoll.has_value()) << firstPoll.error().message;
    ASSERT_TRUE(firstPoll->isContinueFrame());
    ASSERT_NE(firstPoll->frame(), nullptr);
    EXPECT_EQ(firstPoll->frame()->id(), PlatformFrameId{1});

    const WindowFrameSnapshot* primary = firstPoll->frame()->primaryWindow();
    ASSERT_NE(primary, nullptr);
    EXPECT_EQ(primary->metrics.window, (*startupMetrics)->window);
    EXPECT_EQ(primary->metrics.revision, (*startupMetrics)->revision);

    ASSERT_EQ(firstPoll->frame()->platformEvents().size(), 1U);
    const auto* metricsEvent =
        std::get_if<WindowMetricsChangedEvent>(&firstPoll->frame()->platformEvents().front().payload);
    ASSERT_NE(metricsEvent, nullptr);
    EXPECT_EQ(firstPoll->frame()->platformEvents().front().sequence, 1U);
    EXPECT_EQ(metricsEvent->window, primary->metrics.window);
    EXPECT_EQ(metricsEvent->metricsRevision, primary->metrics.revision);

    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, FileDropIsNotPublishedWhenTheBackendDoesNotAcceptDrops)
{
    auto backend = createGlfwPlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    const char* paths[] = {"C:/Assets/ignored.png"};
    ASSERT_TRUE(Detail::queueGlfwFileDropForNextPollForTest(
                    **backend,
                    Detail::GlfwFileDropInjection{.paths = std::span<const char* const>{paths}})
                    .has_value());

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value()) << poll.error().message;
    ASSERT_TRUE(poll->isContinueFrame());
    ASSERT_NE(poll->frame(), nullptr);
    EXPECT_EQ(findFileDrop(*poll->frame()), nullptr);
    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, FileDropCopiesValidUtf8BatchIntoThePublishedFrame)
{
    auto params = hiddenWindowParams();
    params.acceptFileDropEvents = true;
    auto backend = createGlfwPlatformBackend(params);
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    const char* paths[] = {"C:/Assets/a.png", "C:/Assets/b.wav"};
    ASSERT_TRUE(Detail::queueGlfwFileDropForNextPollForTest(
                    **backend,
                    Detail::GlfwFileDropInjection{.paths = std::span<const char* const>{paths}})
                    .has_value());

    auto poll = (*backend)->pollFrame();
    ASSERT_TRUE(poll.has_value()) << poll.error().message;
    ASSERT_TRUE(poll->isContinueFrame());
    ASSERT_NE(poll->frame(), nullptr);
    const FileDropEvent* drop = findFileDrop(*poll->frame());
    ASSERT_NE(drop, nullptr);
    ASSERT_EQ(drop->paths.size(), 2U);
    EXPECT_EQ(drop->paths[0], paths[0]);
    EXPECT_EQ(drop->paths[1], paths[1]);
    EXPECT_TRUE(std::isfinite(drop->logicalX));
    EXPECT_TRUE(std::isfinite(drop->logicalY));
    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, FileDropRejectsCapacityWithoutFailingTheFrameAndRecovers)
{
    auto params = hiddenWindowParams();
    params.acceptFileDropEvents = true;
    params.frameCapacities.fileDropPathCapacity = 1;
    params.frameCapacities.fileDropByteCapacity = 64;
    auto backend = createGlfwPlatformBackend(params);
    ASSERT_TRUE(backend.has_value()) << backend.error().message;

    const char* overCapacity[] = {"C:/Assets/a.png", "C:/Assets/b.png"};
    ASSERT_TRUE(Detail::queueGlfwFileDropForNextPollForTest(
                    **backend,
                    Detail::GlfwFileDropInjection{.paths = std::span<const char* const>{overCapacity}})
                    .has_value());
    auto rejected = (*backend)->pollFrame();
    ASSERT_TRUE(rejected.has_value()) << rejected.error().message;
    ASSERT_TRUE(rejected->isContinueFrame());
    EXPECT_EQ(findFileDrop(*rejected->frame()), nullptr);

    const char* accepted[] = {"C:/Assets/recovered.png"};
    ASSERT_TRUE(Detail::queueGlfwFileDropForNextPollForTest(
                    **backend,
                    Detail::GlfwFileDropInjection{.paths = std::span<const char* const>{accepted}})
                    .has_value());
    auto recovered = (*backend)->pollFrame();
    ASSERT_TRUE(recovered.has_value()) << recovered.error().message;
    ASSERT_TRUE(recovered->isContinueFrame());
    const FileDropEvent* drop = findFileDrop(*recovered->frame());
    ASSERT_NE(drop, nullptr);
    ASSERT_EQ(drop->paths.size(), 1U);
    EXPECT_EQ(drop->paths.front(), accepted[0]);
    (*backend)->shutdown();
}

TEST(GlfwBackendIntegrationTests, FileDropRejectsNullPathArrayAsInvalidPayload)
{
    expectInvalidFileDropPayload(hiddenWindowParams(), Detail::GlfwFileDropInjection{.nullPathArray = true});
}

TEST(GlfwBackendIntegrationTests, FileDropRejectsNullPathEntryAsInvalidPayload)
{
    const char* paths[] = {"C:/Assets/valid.png", nullptr};
    expectInvalidFileDropPayload(
        hiddenWindowParams(), Detail::GlfwFileDropInjection{.paths = std::span<const char* const>{paths}});
}

TEST(GlfwBackendIntegrationTests, FileDropRejectsEmptyPathAsInvalidPayload)
{
    const char* paths[] = {""};
    expectInvalidFileDropPayload(
        hiddenWindowParams(), Detail::GlfwFileDropInjection{.paths = std::span<const char* const>{paths}});
}

TEST(GlfwBackendIntegrationTests, FileDropRejectsInvalidUtf8PathAsInvalidPayload)
{
    const char invalidUtf8[] = {'C', ':', '/', static_cast<char>(0xC0), static_cast<char>(0xAF), '\0'};
    const char* paths[] = {invalidUtf8};
    expectInvalidFileDropPayload(
        hiddenWindowParams(), Detail::GlfwFileDropInjection{.paths = std::span<const char* const>{paths}});
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
    // Without a connected standard gamepad the dense snapshot is empty. When a
    // pad is present, sampleGamepads publishes it; this smoke only requires a
    // coherent empty-or-nonempty publish path without assembly failure.
    EXPECT_LE(poll->frame()->gamepads().size(),
              Platform::PlatformFrameBuilder::MaximumGamepadSlots);

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
    EXPECT_DOUBLE_EQ(primary->input.pointers[Platform::PrimaryPointerId].logicalX, CursorBX);
    EXPECT_DOUBLE_EQ(primary->input.pointers[Platform::PrimaryPointerId].logicalY, CursorBY);
    EXPECT_TRUE(primary->input.pointers[Platform::PrimaryPointerId].isHeld(PointerButton::Primary));
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

TEST(GlfwBackendIntegrationTests, WindowSurfaceSnapshotTracksInitialMetricsSeedAndFirstFrameBoundary)
{
    auto backend = createGlfwWindowSurfacePlatformBackend(hiddenWindowParams());
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    ASSERT_TRUE((*backend)->publishPrimaryWindow().has_value());

    auto startupMetrics = (*backend)->initialPrimaryWindowMetrics();
    ASSERT_TRUE(startupMetrics.has_value()) << startupMetrics.error().message;
    ASSERT_TRUE(startupMetrics->has_value());

    auto startupSurface = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(startupSurface.has_value()) << startupSurface.error().message;
    EXPECT_EQ(startupSurface->sourceWindow, (*startupMetrics)->window);
    EXPECT_EQ(startupSurface->sourceMetricsRevision, (*startupMetrics)->revision);
    EXPECT_EQ(startupSurface->framebufferExtent, (*startupMetrics)->framebufferExtent);
    EXPECT_EQ(startupSurface->contentScale, (*startupMetrics)->contentScale);

    auto firstPoll = (*backend)->pollFrame();
    ASSERT_TRUE(firstPoll.has_value()) << firstPoll.error().message;
    ASSERT_TRUE(firstPoll->isContinueFrame());
    const WindowFrameSnapshot* primary = firstPoll->frame()->primaryWindow();
    ASSERT_NE(primary, nullptr);

    ASSERT_EQ(firstPoll->frame()->platformEvents().size(), 1U);
    const auto* metricsEvent =
        std::get_if<WindowMetricsChangedEvent>(&firstPoll->frame()->platformEvents().front().payload);
    ASSERT_NE(metricsEvent, nullptr);
    EXPECT_EQ(metricsEvent->window, primary->metrics.window);
    EXPECT_EQ(metricsEvent->metricsRevision, primary->metrics.revision);

    auto frameSurface = (*backend)->primaryWindowSurfaceSnapshot();
    ASSERT_TRUE(frameSurface.has_value()) << frameSurface.error().message;
    EXPECT_EQ(frameSurface->sourceWindow, primary->metrics.window);
    EXPECT_EQ(frameSurface->sourceMetricsRevision, primary->metrics.revision);
    EXPECT_EQ(frameSurface->framebufferExtent, primary->metrics.framebufferExtent);
    EXPECT_EQ(frameSurface->contentScale, primary->metrics.contentScale);
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

#if defined(_WIN32)
TEST(GlfwBackendIntegrationTests, IconifiedWindowKeepsLastPositiveLogicalExtent)
{
    auto params = hiddenWindowParams();
    params.primaryWindow.initiallyVisible = true;
    auto backend = createGlfwPlatformBackend(params);
    ASSERT_TRUE(backend.has_value()) << backend.error().message;

    auto firstPoll = (*backend)->pollFrame();
    ASSERT_TRUE(firstPoll.has_value()) << firstPoll.error().message;
    const WindowFrameSnapshot* firstWindow = firstPoll->frame()->primaryWindow();
    ASSERT_NE(firstWindow, nullptr);
    const LogicalExtent lastActiveLogicalExtent = firstWindow->metrics.logicalExtent;
    ASSERT_GT(lastActiveLogicalExtent.width, 0U);
    ASSERT_GT(lastActiveLogicalExtent.height, 0U);

    ASSERT_TRUE(Detail::iconifyGlfwWindowForTest(**backend).has_value());
    bool reachedIconifiedState = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!reachedIconifiedState && std::chrono::steady_clock::now() < deadline)
    {
        auto poll = (*backend)->pollFrame();
        ASSERT_TRUE(poll.has_value()) << poll.error().message;
        const WindowFrameSnapshot* window = poll->frame()->primaryWindow();
        ASSERT_NE(window, nullptr);
        EXPECT_EQ(window->metrics.logicalExtent, lastActiveLogicalExtent);
        reachedIconifiedState = window->metrics.minimized;
        if (reachedIconifiedState)
        {
            EXPECT_EQ(window->metrics.framebufferExtent, (FramebufferExtent{0, 0}));
        }
        if (!reachedIconifiedState)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    EXPECT_TRUE(reachedIconifiedState);
    (*backend)->shutdown();
}
#endif

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

#if defined(_WIN32)
// A locked cursor is clipped to the window rect. Measured on Windows 11: destroying the window
// releases the clip, and so does an abrupt _Exit, so this is not the source of a leaked clip --
// it is pinned here so a future refactor that stops destroying the window on shutdown, or that
// keeps a hidden window alive across runs, does not silently start leaking one.
TEST(GlfwBackendIntegrationTests, ShutdownReleasesALockedCursorClip)
{
    // The window is far smaller than any desktop, so "released" is asserted as "the clip grew
    // back past the window" rather than against absolute screen bounds. Absolute bounds are not
    // usable here: GetClipCursor reports physical pixels while GetSystemMetrics reports logical
    // ones in a process that is not per-monitor DPI aware, so on a scaled display the two
    // disagree by the scale factor even when the cursor is fully released.
    //
    // The pre-open clip is equally unusable as a baseline, because the clip is desktop-wide
    // state that outlives the process that set it: an earlier unreleased run leaves it already
    // narrowed, and comparing against it would make this test pass on a machine whose pointer
    // is still trapped.
    PlatformBackendCreateParams params = hiddenWindowParams();
    params.primaryWindow.initiallyVisible = true;
    params.primaryWindow.pointerCapture = PointerCaptureMode::Locked;
    auto backend = createGlfwWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value()) << backend.error().message;
    ASSERT_TRUE((*backend)->publishPrimaryWindow().has_value());

    RECT lockedClip{};
    ASSERT_NE(GetClipCursor(&lockedClip), 0);
    const LONG lockedWidth = lockedClip.right - lockedClip.left;
    const LONG lockedHeight = lockedClip.bottom - lockedClip.top;
    // 320x180 logical, so even at 200% scale the clip is far under 1000 physical pixels wide if
    // it really is confined to the window.
    const bool clipNarrowed = lockedWidth < 1000 && lockedHeight < 1000;

    (*backend)->shutdown();

    RECT clipAfterShutdown{};
    ASSERT_NE(GetClipCursor(&clipAfterShutdown), 0);
    // Guarded because a headless or remote session may refuse the clip outright; the release is
    // only observable where the lock was observable, and asserting otherwise would test the host
    // rather than the backend.
    if (clipNarrowed)
    {
        EXPECT_GT(clipAfterShutdown.right - clipAfterShutdown.left, lockedWidth);
        EXPECT_GT(clipAfterShutdown.bottom - clipAfterShutdown.top, lockedHeight);
        EXPECT_GE(clipAfterShutdown.right - clipAfterShutdown.left,
                  GetSystemMetrics(SM_CXVIRTUALSCREEN));
        EXPECT_GE(clipAfterShutdown.bottom - clipAfterShutdown.top,
                  GetSystemMetrics(SM_CYVIRTUALSCREEN));
    }
}
#endif

// Locking before the window is shown clips the pointer to a rect the user cannot see, which is
// the "my cursor was trapped before the demo even appeared" half of the same defect. The lock
// has to wait for publication. This uses the WindowSurface factory because that is the path the
// engine actually takes: it defers publication until the render device has its surface, which is
// exactly the window during which the old code already held the cursor.
TEST(GlfwBackendIntegrationTests, RequestedLockIsDeferredUntilTheWindowIsPublished)
{
    PlatformBackendCreateParams params = hiddenWindowParams();
    params.primaryWindow.initiallyVisible = true;
    params.primaryWindow.pointerCapture = PointerCaptureMode::Locked;
    auto backend = createGlfwWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend.has_value()) << backend.error().message;

    auto deferred = Detail::glfwPointerCaptureStateForTest(**backend);
    ASSERT_TRUE(deferred.has_value()) << deferred.error().message;
    // The request is remembered, not dropped: the caller asked for Locked and must still read
    // Locked back, or a game would think the backend refused and disable its camera.
    EXPECT_EQ(deferred->requestedMode, PointerCaptureMode::Locked);
    EXPECT_FALSE(deferred->cursorHidden);

    ASSERT_TRUE((*backend)->publishPrimaryWindow().has_value());

    auto applied = Detail::glfwPointerCaptureStateForTest(**backend);
    ASSERT_TRUE(applied.has_value()) << applied.error().message;
    EXPECT_EQ(applied->requestedMode, PointerCaptureMode::Locked);
    EXPECT_TRUE(applied->cursorHidden);
    (*backend)->shutdown();
}

} // namespace
} // namespace Tina::Platform
