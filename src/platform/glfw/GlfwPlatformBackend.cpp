#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>

#include "../../integration/WindowSurfaceLeaseAccess.hpp"
#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
#include "GlfwBackendTestAccess.hpp"
#endif
#include "GlfwDigitalFocusFilter.hpp"
#include "GlfwInputTranslation.hpp"
#include "GlfwNativeWindowBinding.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace Tina::Platform {
namespace {

constexpr usize GlfwErrorDescriptionCapacity = 512;
constexpr double SuspendedEventWaitTimeoutSeconds = 1.0 / 60.0;

struct GlfwErrorSnapshot final {
    int code = GLFW_NO_ERROR;
    std::array<char, GlfwErrorDescriptionCapacity> description{};
    usize descriptionSize = 0;

    void capture(int nativeCode, const char* nativeDescription) noexcept
    {
        if (code != GLFW_NO_ERROR)
        {
            return;
        }
        code = nativeCode;
        if (nativeDescription == nullptr)
        {
            return;
        }
        while (descriptionSize + 1 < description.size() && nativeDescription[descriptionSize] != '\0')
        {
            description[descriptionSize] = nativeDescription[descriptionSize];
            ++descriptionSize;
        }
        while (descriptionSize != 0 && !Core::isStrictUtf8WithoutNul(text()))
        {
            --descriptionSize;
        }
        description[descriptionSize] = '\0';
    }

    [[nodiscard]] std::string_view text() const noexcept
    {
        return {description.data(), descriptionSize};
    }
};

struct GlfwWindowSurfaceRecord final {};

std::atomic_bool g_glfwBackendActive = false;

void clearGlfwErrors() noexcept
{
    while (glfwGetError(nullptr) != GLFW_NO_ERROR)
    {
    }
}

[[nodiscard]] GlfwErrorSnapshot takeGlfwError() noexcept
{
    const char* description = nullptr;
    const int code = glfwGetError(&description);
    GlfwErrorSnapshot snapshot;
    snapshot.capture(code, description);
    return snapshot;
}

[[nodiscard]] Core::Error glfwFailure(Core::ErrorCode code, std::string_view message, std::string_view operation)
{
    Core::Error error{code, message};
    const GlfwErrorSnapshot nativeError = takeGlfwError();
    if (nativeError.code != GLFW_NO_ERROR)
    {
        error.setNativeCode(nativeError.code);
        error.addContext(operation, nativeError.text());
    } else
    {
        error.addContext(operation, "GLFW did not provide a native error");
    }
    return error;
}

[[nodiscard]] Core::Status checkGlfwOperation(std::string_view operation)
{
    const GlfwErrorSnapshot nativeError = takeGlfwError();
    if (nativeError.code == GLFW_NO_ERROR)
    {
        return Core::success();
    }
    Core::Error error{PlatformErrorCode::BackendOperationFailed, "A GLFW operation failed"};
    error.setNativeCode(nativeError.code);
    error.addContext(operation, nativeError.text());
    return Core::failure(std::move(error));
}

struct GlfwWindowRecord final {
    GLFWwindow* native = nullptr;
};

struct WindowCreatePlan final {
    int width = 0;
    int height = 0;
    int positionX = 0;
    int positionY = 0;
    bool decorated = true;
    bool resizable = true;
    bool positionExplicitly = false;
};

[[nodiscard]] Core::Result<WindowCreatePlan> buildWindowCreatePlan(const PrimaryWindowConfig& config)
{
    WindowCreatePlan plan{
        .width = static_cast<int>(config.initialLogicalExtent.width),
        .height = static_cast<int>(config.initialLogicalExtent.height),
        .decorated = config.mode == WindowMode::Windowed,
        .resizable = config.resizable && config.mode == WindowMode::Windowed,
    };
    if (config.mode == WindowMode::Windowed)
    {
        return plan;
    }

    clearGlfwErrors();
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr)
    {
        return Core::failure(glfwFailure(PlatformErrorCode::WindowCreationFailed, "No primary monitor is available",
                                         "glfwGetPrimaryMonitor"));
    }
    const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
    if (videoMode == nullptr || videoMode->width <= 0 || videoMode->height <= 0)
    {
        return Core::failure(glfwFailure(PlatformErrorCode::WindowCreationFailed,
                                         "The primary monitor has no usable video mode", "glfwGetVideoMode"));
    }
    plan.width = videoMode->width;
    plan.height = videoMode->height;
    if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
    {
        clearGlfwErrors();
        glfwGetMonitorPos(monitor, &plan.positionX, &plan.positionY);
        if (auto status = checkGlfwOperation("glfwGetMonitorPos"); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        plan.positionExplicitly = true;
    }
    return plan;
}

[[nodiscard]] Core::Status validatePrimaryWindowConfig(const PrimaryWindowConfig& config)
{
    if (config.title.empty() || !Core::isStrictUtf8WithoutNul(config.title) || config.initialLogicalExtent.width == 0 ||
        config.initialLogicalExtent.height == 0 ||
        config.initialLogicalExtent.width > static_cast<u32>((std::numeric_limits<int>::max)()) ||
        config.initialLogicalExtent.height > static_cast<u32>((std::numeric_limits<int>::max)()) ||
        (config.mode != WindowMode::Windowed && config.mode != WindowMode::BorderlessFullscreen))
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The GLFW primary window configuration is invalid");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<WindowMetricsSnapshot> readWindowMetrics(GLFWwindow* window, WindowId id, u64 revision,
                                                                    LogicalExtent lastValidLogicalExtent)
{
    int logicalWidth = 0;
    int logicalHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    float scaleX = 1.0F;
    float scaleY = 1.0F;

    clearGlfwErrors();
    glfwGetWindowSize(window, &logicalWidth, &logicalHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    const int focused = glfwGetWindowAttrib(window, GLFW_FOCUSED);
    const int minimized = glfwGetWindowAttrib(window, GLFW_ICONIFIED);
    const int visible = glfwGetWindowAttrib(window, GLFW_VISIBLE);
    if (auto status = checkGlfwOperation("readWindowMetrics"); !status)
    {
        return std::unexpected(std::move(status.error()));
    }
    const bool nativeLogicalExtentUnavailable = logicalWidth <= 0 || logicalHeight <= 0;
    const bool canReuseLastLogicalExtent =
        nativeLogicalExtentUnavailable && lastValidLogicalExtent.width != 0 && lastValidLogicalExtent.height != 0 &&
        (minimized == GLFW_TRUE || framebufferWidth == 0 || framebufferHeight == 0);
    if ((nativeLogicalExtentUnavailable && !canReuseLastLogicalExtent) || framebufferWidth < 0 ||
        framebufferHeight < 0 || !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0.0F || scaleY <= 0.0F)
    {
        return Core::failure(PlatformErrorCode::BackendOperationFailed, "GLFW returned invalid primary window metrics");
    }
    if (canReuseLastLogicalExtent)
    {
        logicalWidth = static_cast<int>(lastValidLogicalExtent.width);
        logicalHeight = static_cast<int>(lastValidLogicalExtent.height);
    }

    return WindowMetricsSnapshot{
        .window = id,
        .logicalExtent = {static_cast<u32>(logicalWidth), static_cast<u32>(logicalHeight)},
        .framebufferExtent = {static_cast<u32>(framebufferWidth), static_cast<u32>(framebufferHeight)},
        .contentScale = {scaleX, scaleY},
        .revision = revision,
        .focused = focused == GLFW_TRUE,
        .minimized = minimized == GLFW_TRUE,
        .visible = visible == GLFW_TRUE,
    };
}

[[nodiscard]] bool sameMetricsFacts(const WindowMetricsSnapshot& left, const WindowMetricsSnapshot& right) noexcept
{
    return left.window == right.window && left.logicalExtent == right.logicalExtent &&
           left.framebufferExtent == right.framebufferExtent && left.contentScale == right.contentScale &&
           left.focused == right.focused && left.minimized == right.minimized && left.visible == right.visible;
}

enum class CallbackAssemblyFailure : u8 {
    None,
    SequenceExhausted,
    InvalidPayload,
    FrameNotOpen,
    InvalidTextCodepoint,
};

class GlfwPlatformBackend final : public Integration::IWindowSurfacePlatformBackend {
  public:
    using WindowPool = Core::GenerationPool<GlfwWindowRecord, WindowRegistryTag>;
    using SurfacePool = Core::GenerationPool<GlfwWindowSurfaceRecord, Integration::WindowSurfaceRegistryTag>;

    GlfwPlatformBackend(WindowPool windows, WindowId windowId, SurfacePool surfaces,
                        Integration::WindowSurfaceId surfaceId,
                        std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> surfaceLeaseControl,
                        GLFWwindow* window, PlatformFrameBuilder frameBuilder, WindowMetricsSnapshot metrics,
                        WindowInputSnapshot input, bool initiallyVisible) noexcept
        : windows_(std::move(windows)), windowId_(windowId), surfaces_(std::move(surfaces)), surfaceId_(surfaceId),
          surfaceLeaseControl_(std::move(surfaceLeaseControl)), window_(window), frameBuilder_(std::move(frameBuilder)),
          metrics_(metrics), input_(input), ownerThread_(std::this_thread::get_id()),
          surfaceSnapshot_(makeSurfaceSnapshot(surfaceId_, metrics_, 1)), initiallyVisible_(initiallyVisible)
    {
    }

    ~GlfwPlatformBackend() noexcept override
    {
        shutdown();
    }

    [[nodiscard]] Core::Result<std::optional<WindowMetricsSnapshot>> initialPrimaryWindowMetrics() override
    {
        if (stopped_)
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW platform backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW initial window metrics must be read on the creating thread");
        }
        if (!hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendOperationFailed,
                                 "The GLFW primary window registry entry is no longer live");
        }

        auto refreshed = refreshMetricsFromNative(true);
        if (!refreshed)
        {
            return std::unexpected(std::move(refreshed.error()));
        }
        startupMetricsEventPending_ = true;
        return std::optional<WindowMetricsSnapshot>{metrics_};
    }

    [[nodiscard]] Core::Result<PlatformPollResult> pollFrame() override
    {
        if (stopped_)
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW platform backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW platform backend must be polled on its creating thread");
        }
        if (!hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendOperationFailed,
                                 "The GLFW primary window registry entry is no longer live");
        }
        if (closeRequested_ || glfwWindowShouldClose(window_) == GLFW_TRUE)
        {
            return PlatformPollResult::Exit();
        }
        if (nextFrameId_ == (std::numeric_limits<u64>::max)())
        {
            return Core::failure(PlatformErrorCode::FrameSequenceExhausted,
                                 "The GLFW platform frame sequence is exhausted");
        }

        auto beginStatus = frameBuilder_.beginFrame(PlatformFrameId{nextFrameId_++});
        if (!beginStatus)
        {
            return std::unexpected(std::move(beginStatus.error()));
        }
        auto discardPartialFrame = Core::makeScopeExit([this]() noexcept {
            (void)frameBuilder_.discardFrame();
            streamRecoveryPending_ = true;
            metricsDirty_ = true;
        });

        callbackFailure_ = CallbackAssemblyFailure::None;
        if (streamRecoveryPending_)
        {
            recordAppend(frameBuilder_.appendInputTransition(InputStreamReset{
                .routedWindow = windowId_,
                .reason = InputResetReason::BackendRecovery,
            }));
            recordAppend(frameBuilder_.appendPlatformEvent(PlatformEventStreamReset{
                .reason = PlatformEventResetReason::BackendRecovery,
            }));
            focusCancelPending_ = false;
            streamRecoveryPending_ = false;
        }
        if (focusCancelPending_)
        {
            recordAppend(frameBuilder_.appendInputTransition(InputCancelTransition{
                .routedWindow = windowId_,
                .reason = InputCancelReason::FocusLost,
            }));
            focusCancelPending_ = false;
        }
        input_.pointer.accumulatedDeltaX = 0.0;
        input_.pointer.accumulatedDeltaY = 0.0;
        collectingFrame_ = true;
        bool waitForEvents = surfaceSnapshot_.suspended;
        double waitTimeoutSeconds = SuspendedEventWaitTimeoutSeconds;
#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
        if (forceSuspendedWaitPathForTest_)
        {
            waitForEvents = true;
            waitTimeoutSeconds = suspendedWaitTimeoutForTest_;
        }
#endif
        clearGlfwErrors();
        if (waitForEvents)
        {
            glfwWaitEventsTimeout(waitTimeoutSeconds);
#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
            ++eventPumpStatsForTest_.waitEventsTimeoutCalls;
#endif
        } else
        {
            glfwPollEvents();
#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
            ++eventPumpStatsForTest_.pollEventsCalls;
#endif
        }
#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
        emitQueuedPointerEventsForTest();
#endif
        collectingFrame_ = false;

        if (auto status = checkGlfwOperation(waitForEvents ? "glfwWaitEventsTimeout" : "glfwPollEvents"); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        if (callbackFailure_ != CallbackAssemblyFailure::None)
        {
            Core::Error error{PlatformErrorCode::CallbackFrameAssemblyFailed,
                              "A GLFW callback could not append a valid platform transition"};
            error.setNativeCode(static_cast<i64>(callbackFailure_));
            error.addContext("GlfwPlatformBackend::pollFrame", "first callback failure wins");
            return Core::failure(std::move(error));
        }
        if (closeRequested_ || glfwWindowShouldClose(window_) == GLFW_TRUE)
        {
            return PlatformPollResult::Exit();
        }

        bool metricsEventPublishedThisFrame = false;
        if (metricsDirty_)
        {
            auto changed = refreshMetricsFromNative(false);
            if (!changed)
            {
                return std::unexpected(std::move(changed.error()));
            }
            if (*changed)
            {
                recordAppend(frameBuilder_.appendPlatformEvent(WindowMetricsChangedEvent{
                    .window = windowId_,
                    .metricsRevision = metrics_.revision,
                }));
                metricsEventPublishedThisFrame = true;
            }
        }
        if (startupMetricsEventPending_ && !metricsEventPublishedThisFrame)
        {
            recordAppend(frameBuilder_.appendPlatformEvent(WindowMetricsChangedEvent{
                .window = windowId_,
                .metricsRevision = metrics_.revision,
            }));
        }
        startupMetricsEventPending_ = false;
        if (callbackFailure_ != CallbackAssemblyFailure::None)
        {
            return Core::failure(PlatformErrorCode::CallbackFrameAssemblyFailed,
                                 "The window metrics event could not be appended");
        }
#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
        if (failNextPollForTest_)
        {
            failNextPollForTest_ = false;
            return Core::failure(PlatformErrorCode::BackendOperationFailed,
                                 "The GLFW test seam injected a recoverable poll failure");
        }
#endif

        input_.sourceMetricsRevision = metrics_.revision;
        auto nextSurfaceSnapshot = surfaceSnapshotFromCommittedMetrics();
        if (!nextSurfaceSnapshot)
        {
            return std::unexpected(std::move(nextSurfaceSnapshot.error()));
        }
        if (!frameBuilder_.setPrimaryWindowSnapshot(metrics_, input_) || !frameBuilder_.setGamepadSnapshots({}))
        {
            return Core::failure(PlatformErrorCode::InvalidFrameSnapshot,
                                 "The GLFW final platform snapshots could not be committed");
        }
        auto frame = frameBuilder_.finishFrame();
        if (!frame)
        {
            return std::unexpected(std::move(frame.error()));
        }
        surfaceSnapshot_ = *nextSurfaceSnapshot;
        discardPartialFrame.release();
        return PlatformPollResult::Continue(*frame);
    }

    void shutdown() noexcept override
    {
        if (stopped_)
        {
            return;
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (surfaceLeaseControl_ != nullptr && surfaceLeaseControl_->activeLeaseCount != 0)
        {
            std::terminate();
        }
        stopped_ = true;
        collectingFrame_ = false;
        if (surfaceLeaseControl_ != nullptr)
        {
            surfaceLeaseControl_->surfaceAlive = false;
        }
        if (window_ != nullptr)
        {
            clearCallbacks();
            glfwSetWindowUserPointer(window_, nullptr);
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        if (windowId_.hasValue())
        {
            (void)windows_.erase(windowId_);
            windowId_ = {};
        }
        if (surfaceId_.hasValue())
        {
            (void)surfaces_.erase(surfaceId_);
            surfaceId_ = {};
        }
        glfwTerminate();
        g_glfwBackendActive.store(false, std::memory_order_release);
    }

    void registerCallbacks() noexcept
    {
        glfwSetWindowUserPointer(window_, this);
        glfwSetKeyCallback(window_, &GlfwPlatformBackend::keyCallback);
        glfwSetCharCallback(window_, &GlfwPlatformBackend::characterCallback);
        glfwSetCursorPosCallback(window_, &GlfwPlatformBackend::cursorPositionCallback);
        glfwSetMouseButtonCallback(window_, &GlfwPlatformBackend::mouseButtonCallback);
        glfwSetScrollCallback(window_, &GlfwPlatformBackend::scrollCallback);
        glfwSetWindowFocusCallback(window_, &GlfwPlatformBackend::focusCallback);
        glfwSetWindowSizeCallback(window_, &GlfwPlatformBackend::windowSizeCallback);
        glfwSetFramebufferSizeCallback(window_, &GlfwPlatformBackend::framebufferSizeCallback);
        glfwSetWindowContentScaleCallback(window_, &GlfwPlatformBackend::contentScaleCallback);
        glfwSetWindowIconifyCallback(window_, &GlfwPlatformBackend::iconifyCallback);
        glfwSetWindowCloseCallback(window_, &GlfwPlatformBackend::closeCallback);
    }

    [[nodiscard]] Core::Status finishCreation(bool publishDuringCreation)
    {
        if (auto status = refreshInitialState(); !status)
        {
            return status;
        }
        return publishDuringCreation ? publishPrimaryWindow() : Core::success();
    }

    [[nodiscard]] Core::Result<Integration::NativeWindowSurfaceLease>
    acquirePrimaryWindowSurfaceLease() noexcept override
    {
        if (stopped_ || !hasLiveWindow() || surfaceLeaseControl_ == nullptr || !surfaceId_.hasValue())
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                                 "The GLFW primary WindowSurface is unavailable");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW WindowSurface lease must be acquired on the creating thread");
        }
        if (surfaceLeaseAcquired_)
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceLeaseAlreadyAcquired,
                                 "The GLFW primary WindowSurface lease was already acquired");
        }

        auto binding = Detail::readGlfwNativeWindowBinding(window_);
        if (!binding)
        {
            return std::unexpected(std::move(binding.error()));
        }
        auto lease =
            Integration::Detail::NativeWindowSurfaceLeaseAccess::Create(surfaceLeaseControl_, surfaceId_, *binding);
        if (!lease)
        {
            return std::unexpected(std::move(lease.error()));
        }
        surfaceLeaseAcquired_ = true;
        return std::move(*lease);
    }

    [[nodiscard]] Core::Result<Integration::WindowSurfaceSnapshot>
    primaryWindowSurfaceSnapshot() const noexcept override
    {
        if (stopped_ || !hasLiveWindow() || !surfaceSnapshot_.surface.hasValue())
        {
            return Core::failure(PlatformErrorCode::WindowSurfaceUnavailable,
                                 "The GLFW primary WindowSurface snapshot is unavailable");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW WindowSurface snapshot must be read on the creating thread");
        }
        return surfaceSnapshot_;
    }

    [[nodiscard]] Core::Status publishPrimaryWindow() noexcept override
    {
        if (stopped_ || !hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW platform backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW primary window must be published on the creating thread");
        }
        if (windowPublished_)
        {
            return Core::success();
        }
        if (initiallyVisible_)
        {
            clearGlfwErrors();
            glfwShowWindow(window_);
            if (auto status = checkGlfwOperation("glfwShowWindow"); !status)
            {
                Core::Error error = std::move(status.error());
                error.addContext("IWindowSurfacePlatformBackend::publishPrimaryWindow");
                return Core::failure(std::move(error));
            }
            metricsDirty_ = true;
        }
        windowPublished_ = true;
        return Core::success();
    }

    [[nodiscard]] Core::Status refreshInitialState()
    {
        auto latestMetrics = readWindowMetrics(window_, windowId_, 1, metrics_.logicalExtent);
        if (!latestMetrics)
        {
            return Core::failure(std::move(latestMetrics.error()));
        }
        double cursorX = 0.0;
        double cursorY = 0.0;
        clearGlfwErrors();
        glfwGetCursorPos(window_, &cursorX, &cursorY);
        if (auto status = checkGlfwOperation("glfwGetCursorPos"); !status)
        {
            return status;
        }
        if (!std::isfinite(cursorX) || !std::isfinite(cursorY))
        {
            return Core::failure(PlatformErrorCode::BackendOperationFailed,
                                 "GLFW returned an invalid initial cursor position");
        }
        metrics_ = *latestMetrics;
        input_.window = windowId_;
        input_.sourceMetricsRevision = metrics_.revision;
        input_.pointer.pointer = PrimaryPointerId;
        input_.pointer.logicalX = cursorX;
        input_.pointer.logicalY = cursorY;
        input_.pointer.accumulatedDeltaX = 0.0;
        input_.pointer.accumulatedDeltaY = 0.0;
        focusFilter_.reset(metrics_.focused);
        metricsDirty_ = false;
        surfaceSnapshot_ = makeSurfaceSnapshot(surfaceId_, metrics_, 1);
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> refreshMetricsFromNative(bool commitSurfaceSnapshot)
    {
        auto latestMetrics = readWindowMetrics(window_, windowId_, metrics_.revision, metrics_.logicalExtent);
        if (!latestMetrics)
        {
            return std::unexpected(std::move(latestMetrics.error()));
        }

        WindowMetricsSnapshot nextMetrics = metrics_;
        const bool changed = !sameMetricsFacts(metrics_, *latestMetrics);
        if (changed)
        {
            if (metrics_.revision == (std::numeric_limits<u64>::max)())
            {
                return Core::failure(PlatformErrorCode::BackendOperationFailed,
                                     "The primary window metrics revision is exhausted");
            }
            latestMetrics->revision = metrics_.revision + 1;
            nextMetrics = *latestMetrics;
        }

        std::optional<Integration::WindowSurfaceSnapshot> nextSurfaceSnapshot;
        if (commitSurfaceSnapshot)
        {
            auto snapshot = surfaceSnapshotFromMetrics(nextMetrics);
            if (!snapshot)
            {
                return std::unexpected(std::move(snapshot.error()));
            }
            nextSurfaceSnapshot = *snapshot;
        }

        metrics_ = nextMetrics;
        metricsDirty_ = false;
        input_.sourceMetricsRevision = metrics_.revision;
        if (nextSurfaceSnapshot.has_value())
        {
            surfaceSnapshot_ = *nextSurfaceSnapshot;
        }
        return changed;
    }

    [[nodiscard]] static Integration::WindowSurfaceSnapshot makeSurfaceSnapshot(Integration::WindowSurfaceId surfaceId,
                                                                                const WindowMetricsSnapshot& metrics,
                                                                                u64 surfaceRevision) noexcept
    {
        return Integration::WindowSurfaceSnapshot{
            .surface = surfaceId,
            .sourceWindow = metrics.window,
            .framebufferExtent = metrics.framebufferExtent,
            .contentScale = metrics.contentScale,
            .sourceMetricsRevision = metrics.revision,
            .surfaceRevision = surfaceRevision,
            .suspended =
                metrics.minimized || metrics.framebufferExtent.width == 0 || metrics.framebufferExtent.height == 0,
        };
    }

    [[nodiscard]] Core::Result<Integration::WindowSurfaceSnapshot>
    surfaceSnapshotFromMetrics(const WindowMetricsSnapshot& metrics) const noexcept
    {
        Integration::WindowSurfaceSnapshot next =
            makeSurfaceSnapshot(surfaceId_, metrics, surfaceSnapshot_.surfaceRevision);
        const bool changed = next.framebufferExtent != surfaceSnapshot_.framebufferExtent ||
                             next.contentScale != surfaceSnapshot_.contentScale ||
                             next.suspended != surfaceSnapshot_.suspended;
        if (changed)
        {
            if (surfaceSnapshot_.surfaceRevision == (std::numeric_limits<u64>::max)())
            {
                return Core::failure(PlatformErrorCode::WindowSurfaceRevisionExhausted,
                                     "The GLFW WindowSurface revision is exhausted");
            }
            next.surfaceRevision = surfaceSnapshot_.surfaceRevision + 1;
        }
        return next;
    }

    [[nodiscard]] Core::Result<Integration::WindowSurfaceSnapshot> surfaceSnapshotFromCommittedMetrics() const noexcept
    {
        return surfaceSnapshotFromMetrics(metrics_);
    }

#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
    [[nodiscard]] Core::Status requestCloseForTest() noexcept
    {
        if (stopped_ || !hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW test backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW test operation must run on the creating thread");
        }
        clearGlfwErrors();
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
        return checkGlfwOperation("glfwSetWindowShouldClose");
    }

    [[nodiscard]] Core::Result<bool> windowVisibleForTest() noexcept
    {
        if (stopped_ || !hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW test backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW test operation must run on the creating thread");
        }
        clearGlfwErrors();
        const int visible = glfwGetWindowAttrib(window_, GLFW_VISIBLE);
        if (auto status = checkGlfwOperation("glfwGetWindowAttrib(GLFW_VISIBLE)"); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        return visible == GLFW_TRUE;
    }

    [[nodiscard]] Core::Status resizeForTest(LogicalExtent extent) noexcept
    {
        if (stopped_ || !hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW test backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW test operation must run on the creating thread");
        }
        if (extent.width == 0 || extent.height == 0 ||
            extent.width > static_cast<u32>((std::numeric_limits<int>::max)()) ||
            extent.height > static_cast<u32>((std::numeric_limits<int>::max)()))
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument, "The GLFW test extent is invalid");
        }
        clearGlfwErrors();
        glfwSetWindowSize(window_, static_cast<int>(extent.width), static_cast<int>(extent.height));
        return checkGlfwOperation("glfwSetWindowSize");
    }

    [[nodiscard]] Core::Status iconifyForTest() noexcept
    {
        if (stopped_ || !hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW test backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW test operation must run on the creating thread");
        }
        clearGlfwErrors();
        glfwIconifyWindow(window_);
        if (auto status = checkGlfwOperation("glfwIconifyWindow"); !status)
        {
            return status;
        }
        metricsDirty_ = true;
        return Core::success();
    }

    [[nodiscard]] Core::Status failNextPollForTest() noexcept
    {
        if (stopped_ || !hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW test backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW test operation must run on the creating thread");
        }
        failNextPollForTest_ = true;
        return Core::success();
    }

    [[nodiscard]] Core::Status forceSuspendedWaitPathForTest(double waitTimeoutSeconds) noexcept
    {
        if (stopped_ || !hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW test backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW test operation must run on the creating thread");
        }
        if (!std::isfinite(waitTimeoutSeconds) || waitTimeoutSeconds <= 0.0)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The GLFW test wait timeout must be finite and positive");
        }
        forceSuspendedWaitPathForTest_ = true;
        suspendedWaitTimeoutForTest_ = waitTimeoutSeconds;
        return Core::success();
    }

    [[nodiscard]] Core::Status
    queuePointerEventsForNextPollForTest(std::span<const Detail::GlfwPointerInjection> events) noexcept
    {
        if (stopped_ || !hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW test backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW test operation must run on the creating thread");
        }
        if (events.size() > queuedPointerEventsForTest_.size() || queuedPointerEventCountForTest_ != 0)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The GLFW pointer test event queue is unavailable");
        }
        for (const Detail::GlfwPointerInjection& event : events)
        {
            if (!isValidPointerInjectionForTest(event))
            {
                return Core::failure(Core::CoreErrorCode::InvalidArgument, "The GLFW pointer test event is invalid");
            }
        }
        queuedPointerEventCountForTest_ = events.size();
        for (usize index = 0; index < queuedPointerEventCountForTest_; ++index)
        {
            queuedPointerEventsForTest_[index] = events[index];
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<Detail::GlfwEventPumpStats> eventPumpStatsForTest() const noexcept
    {
        if (stopped_ || !hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW test backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW test operation must run on the creating thread");
        }
        return eventPumpStatsForTest_;
    }

    [[nodiscard]] Core::Result<Detail::GlfwRuntimePlatform> runtimePlatformForTest() noexcept
    {
        if (stopped_ || !hasLiveWindow())
        {
            return Core::failure(PlatformErrorCode::BackendStopped, "The GLFW test backend is stopped");
        }
        if (std::this_thread::get_id() != ownerThread_)
        {
            return Core::failure(PlatformErrorCode::WrongOwnerThread,
                                 "The GLFW test operation must run on the creating thread");
        }
        clearGlfwErrors();
        const int platform = glfwGetPlatform();
        if (auto status = checkGlfwOperation("glfwGetPlatform"); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
        switch (platform)
        {
        case GLFW_PLATFORM_WIN32:
            return Detail::GlfwRuntimePlatform::Win32;
        case GLFW_PLATFORM_COCOA:
            return Detail::GlfwRuntimePlatform::Cocoa;
        case GLFW_PLATFORM_WAYLAND:
            return Detail::GlfwRuntimePlatform::Wayland;
        case GLFW_PLATFORM_X11:
            return Detail::GlfwRuntimePlatform::X11;
        case GLFW_PLATFORM_NULL:
            return Detail::GlfwRuntimePlatform::Null;
        default:
            return Detail::GlfwRuntimePlatform::Unknown;
        }
    }
#endif

  private:
    [[nodiscard]] bool hasLiveWindow() const noexcept
    {
        const GlfwWindowRecord* record = windows_.tryGet(windowId_);
        return record != nullptr && record->native == window_ && window_ != nullptr;
    }

    [[nodiscard]] static GlfwPlatformBackend* fromWindow(GLFWwindow* window) noexcept
    {
        return static_cast<GlfwPlatformBackend*>(glfwGetWindowUserPointer(window));
    }

    void recordAppend(FrameBatchAppendResult result) noexcept
    {
        if (callbackFailure_ != CallbackAssemblyFailure::None)
        {
            return;
        }
        switch (result)
        {
        case FrameBatchAppendResult::Appended:
        case FrameBatchAppendResult::Coalesced:
        case FrameBatchAppendResult::ResetInserted:
        case FrameBatchAppendResult::IgnoredAfterReset:
            return;
        case FrameBatchAppendResult::SequenceExhausted:
            callbackFailure_ = CallbackAssemblyFailure::SequenceExhausted;
            return;
        case FrameBatchAppendResult::InvalidPayload:
            callbackFailure_ = CallbackAssemblyFailure::InvalidPayload;
            return;
        case FrameBatchAppendResult::FrameNotOpen:
            callbackFailure_ = CallbackAssemblyFailure::FrameNotOpen;
            return;
        }
    }

    void onKey(int nativeKey, int action) noexcept
    {
        if (!collectingFrame_)
        {
            return;
        }
        const Key key = Detail::translateGlfwKey(nativeKey);
        if (key == Key::Unknown)
        {
            return;
        }
        const usize keyIndex = static_cast<usize>(key);
        if (!Detail::shouldAcceptGlfwKeyAction(action, input_.heldKeys.test(keyIndex)))
        {
            return;
        }
        const bool held = action != GLFW_RELEASE;
        const DigitalTransition transition = held ? DigitalTransition::Down : DigitalTransition::Up;
        if (!focusFilter_.shouldAccept(key, transition))
        {
            return;
        }
        input_.heldKeys.set(static_cast<usize>(key), held);
        recordAppend(frameBuilder_.appendInputTransition(KeyTransition{
            .window = windowId_,
            .key = key,
            .state = transition,
            .repeat = action == GLFW_REPEAT,
        }));
    }

    void onCharacter(u32 codepoint) noexcept
    {
        if (!collectingFrame_)
        {
            return;
        }
        const auto encoded = Detail::encodeUtf8Codepoint(codepoint);
        if (!encoded)
        {
            if (callbackFailure_ == CallbackAssemblyFailure::None)
            {
                callbackFailure_ = CallbackAssemblyFailure::InvalidTextCodepoint;
            }
            return;
        }
        recordAppend(frameBuilder_.appendInputTransition(TextInputTransition{
            .window = windowId_,
            .committedUtf8 = encoded->view(),
        }));
    }

    void onCursorPosition(double logicalX, double logicalY) noexcept
    {
        if (!std::isfinite(logicalX) || !std::isfinite(logicalY))
        {
            if (collectingFrame_ && callbackFailure_ == CallbackAssemblyFailure::None)
            {
                callbackFailure_ = CallbackAssemblyFailure::InvalidPayload;
            }
            return;
        }
        const double deltaX = logicalX - input_.pointer.logicalX;
        const double deltaY = logicalY - input_.pointer.logicalY;
        input_.pointer.logicalX = logicalX;
        input_.pointer.logicalY = logicalY;
        if (!collectingFrame_)
        {
            return;
        }
        input_.pointer.accumulatedDeltaX += deltaX;
        input_.pointer.accumulatedDeltaY += deltaY;
        recordAppend(frameBuilder_.appendInputTransition(PointerMoveTransition{
            .window = windowId_,
            .pointer = PrimaryPointerId,
            .logicalX = logicalX,
            .logicalY = logicalY,
            .deltaX = deltaX,
            .deltaY = deltaY,
        }));
    }

    void onMouseButton(int nativeButton, int action) noexcept
    {
        if (!collectingFrame_ || (action != GLFW_PRESS && action != GLFW_RELEASE))
        {
            return;
        }
        const auto button = Detail::translateGlfwPointerButton(nativeButton);
        if (!button)
        {
            return;
        }
        onPointerButton(*button, action == GLFW_PRESS ? DigitalTransition::Down : DigitalTransition::Up);
    }

    void onPointerButton(PointerButton button, DigitalTransition transition) noexcept
    {
        if (!collectingFrame_)
        {
            return;
        }
        if (!focusFilter_.shouldAccept(button, transition))
        {
            return;
        }

        appendAcceptedPointerButton(button, transition);
    }

    void appendAcceptedPointerButton(PointerButton button, DigitalTransition transition) noexcept
    {
        const bool held = transition == DigitalTransition::Down;
        input_.pointer.heldButtons.set(static_cast<usize>(button), held);
        recordAppend(frameBuilder_.appendInputTransition(PointerButtonTransition{
            .window = windowId_,
            .pointer = PrimaryPointerId,
            .button = button,
            .state = transition,
            .logicalX = input_.pointer.logicalX,
            .logicalY = input_.pointer.logicalY,
        }));
    }

    void onScroll(double deltaX, double deltaY) noexcept
    {
        if (!collectingFrame_)
        {
            return;
        }
        recordAppend(frameBuilder_.appendInputTransition(PointerWheelTransition{
            .window = windowId_,
            .pointer = PrimaryPointerId,
            .deltaX = deltaX,
            .deltaY = deltaY,
            .logicalX = input_.pointer.logicalX,
            .logicalY = input_.pointer.logicalY,
        }));
    }

#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
    [[nodiscard]] static bool isValidPointerInjectionForTest(const Detail::GlfwPointerInjection& event) noexcept
    {
        switch (event.kind)
        {
        case Detail::GlfwPointerInjectionKind::CursorPosition:
            return std::isfinite(event.logicalX) && std::isfinite(event.logicalY);
        case Detail::GlfwPointerInjectionKind::Button:
            return static_cast<usize>(event.button) < PointerButtonCount &&
                   (event.transition == DigitalTransition::Down || event.transition == DigitalTransition::Up);
        case Detail::GlfwPointerInjectionKind::Wheel:
            return std::isfinite(event.wheelDeltaX) && std::isfinite(event.wheelDeltaY);
        }
        return false;
    }

    void emitQueuedPointerEventsForTest() noexcept
    {
        const usize queuedCount = queuedPointerEventCountForTest_;
        queuedPointerEventCountForTest_ = 0;
        for (usize index = 0; index < queuedCount && callbackFailure_ == CallbackAssemblyFailure::None; ++index)
        {
            const Detail::GlfwPointerInjection& event = queuedPointerEventsForTest_[index];
            switch (event.kind)
            {
            case Detail::GlfwPointerInjectionKind::CursorPosition:
                onCursorPosition(event.logicalX, event.logicalY);
                break;
            case Detail::GlfwPointerInjectionKind::Button:
                // The hidden integration-test window is intentionally not
                // focused. Focus filtering has its own direct tests; this
                // seam enters after that filter so it exercises the exact
                // accepted-event position capture and frame assembly path.
                appendAcceptedPointerButton(event.button, event.transition);
                break;
            case Detail::GlfwPointerInjectionKind::Wheel:
                onScroll(event.wheelDeltaX, event.wheelDeltaY);
                break;
            }
        }
    }
#endif

    void onFocus(int focused) noexcept
    {
        metricsDirty_ = true;
        if (focused == GLFW_TRUE)
        {
            focusFilter_.onFocusGained();
            return;
        }
        focusFilter_.onFocusLost(input_);
        input_.heldKeys.reset();
        input_.pointer.heldButtons.reset();
        if (!collectingFrame_)
        {
            focusCancelPending_ = true;
            return;
        }
        recordAppend(frameBuilder_.appendInputTransition(InputCancelTransition{
            .routedWindow = windowId_,
            .reason = InputCancelReason::FocusLost,
        }));
    }

    void clearCallbacks() noexcept
    {
        glfwSetKeyCallback(window_, nullptr);
        glfwSetCharCallback(window_, nullptr);
        glfwSetCursorPosCallback(window_, nullptr);
        glfwSetMouseButtonCallback(window_, nullptr);
        glfwSetScrollCallback(window_, nullptr);
        glfwSetWindowFocusCallback(window_, nullptr);
        glfwSetWindowSizeCallback(window_, nullptr);
        glfwSetFramebufferSizeCallback(window_, nullptr);
        glfwSetWindowContentScaleCallback(window_, nullptr);
        glfwSetWindowIconifyCallback(window_, nullptr);
        glfwSetWindowCloseCallback(window_, nullptr);
    }

    static void keyCallback(GLFWwindow* window, int key, int, int action, int) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->onKey(key, action);
        }
    }

    static void characterCallback(GLFWwindow* window, unsigned int codepoint) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->onCharacter(codepoint);
        }
    }

    static void cursorPositionCallback(GLFWwindow* window, double logicalX, double logicalY) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->onCursorPosition(logicalX, logicalY);
        }
    }

    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->onMouseButton(button, action);
        }
    }

    static void scrollCallback(GLFWwindow* window, double deltaX, double deltaY) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->onScroll(deltaX, deltaY);
        }
    }

    static void focusCallback(GLFWwindow* window, int focused) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->onFocus(focused);
        }
    }

    static void windowSizeCallback(GLFWwindow* window, int, int) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->metricsDirty_ = true;
        }
    }

    static void framebufferSizeCallback(GLFWwindow* window, int, int) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->metricsDirty_ = true;
        }
    }

    static void contentScaleCallback(GLFWwindow* window, float, float) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->metricsDirty_ = true;
        }
    }

    static void iconifyCallback(GLFWwindow* window, int) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->metricsDirty_ = true;
        }
    }

    static void closeCallback(GLFWwindow* window) noexcept
    {
        if (auto* backend = fromWindow(window); backend != nullptr)
        {
            backend->closeRequested_ = true;
        }
    }

    WindowPool windows_;
    WindowId windowId_{};
    SurfacePool surfaces_;
    Integration::WindowSurfaceId surfaceId_{};
    std::shared_ptr<Integration::Detail::NativeWindowSurfaceLeaseControl> surfaceLeaseControl_;
    GLFWwindow* window_ = nullptr;
    PlatformFrameBuilder frameBuilder_;
    WindowMetricsSnapshot metrics_{};
    WindowInputSnapshot input_{};
    Detail::GlfwDigitalFocusFilter focusFilter_{};
    std::thread::id ownerThread_{};
    Integration::WindowSurfaceSnapshot surfaceSnapshot_{};
    u64 nextFrameId_ = 1;
    CallbackAssemblyFailure callbackFailure_ = CallbackAssemblyFailure::None;
    bool collectingFrame_ = false;
    bool focusCancelPending_ = false;
    bool streamRecoveryPending_ = false;
    bool metricsDirty_ = false;
    bool startupMetricsEventPending_ = false;
    bool closeRequested_ = false;
#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
    static constexpr usize MaximumQueuedPointerEventsForTest = 16;
    bool failNextPollForTest_ = false;
    bool forceSuspendedWaitPathForTest_ = false;
    double suspendedWaitTimeoutForTest_ = SuspendedEventWaitTimeoutSeconds;
    std::array<Detail::GlfwPointerInjection, MaximumQueuedPointerEventsForTest> queuedPointerEventsForTest_{};
    usize queuedPointerEventCountForTest_ = 0;
    Detail::GlfwEventPumpStats eventPumpStatsForTest_{};
#endif
    bool stopped_ = false;
    bool initiallyVisible_ = true;
    bool windowPublished_ = false;
    bool surfaceLeaseAcquired_ = false;
};

// The independent GLFW composition must not expose the WindowSurface SPI even
// though both factories share the same native implementation internally.
class GlfwIndependentPlatformBackend final : public IPlatformBackend {
  public:
    explicit GlfwIndependentPlatformBackend(std::unique_ptr<GlfwPlatformBackend> implementation) noexcept
        : implementation_(std::move(implementation))
    {
    }

    [[nodiscard]] Core::Result<std::optional<WindowMetricsSnapshot>> initialPrimaryWindowMetrics() override
    {
        return implementation_->initialPrimaryWindowMetrics();
    }

    [[nodiscard]] Core::Result<PlatformPollResult> pollFrame() override
    {
        return implementation_->pollFrame();
    }

    void shutdown() noexcept override
    {
        implementation_->shutdown();
    }

#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
    [[nodiscard]] GlfwPlatformBackend& implementationForTest() noexcept
    {
        return *implementation_;
    }
#endif

  private:
    std::unique_ptr<GlfwPlatformBackend> implementation_;
};

[[nodiscard]] Core::Result<std::unique_ptr<GlfwPlatformBackend>>
createBackendUnchecked(const PlatformBackendCreateParams& params, bool publishDuringCreation)
{
    if (auto status = validatePrimaryWindowConfig(params.primaryWindow); !status)
    {
        return std::unexpected(std::move(status.error()));
    }
    auto frameBuilder = PlatformFrameBuilder::Create(params.frameCapacities);
    if (!frameBuilder)
    {
        return std::unexpected(std::move(frameBuilder.error()));
    }
    auto windowPool = GlfwPlatformBackend::WindowPool::Create(1);
    if (!windowPool)
    {
        return std::unexpected(std::move(windowPool.error()));
    }
    auto surfacePool = GlfwPlatformBackend::SurfacePool::Create(1);
    if (!surfacePool)
    {
        return std::unexpected(std::move(surfacePool.error()));
    }
    auto surfaceId = surfacePool->tryEmplace(GlfwWindowSurfaceRecord{});
    if (!surfaceId)
    {
        return std::unexpected(std::move(surfaceId.error()));
    }
    auto surfaceLeaseControl = std::make_shared<Integration::Detail::NativeWindowSurfaceLeaseControl>();
    surfaceLeaseControl->ownerThread = std::this_thread::get_id();

    bool expected = false;
    if (!g_glfwBackendActive.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        return Core::failure(PlatformErrorCode::BackendAlreadyActive,
                             "Only one Tina GLFW platform backend may be active in a process");
    }

    GLFWwindow* nativeWindow = nullptr;
    bool initialized = false;
    auto rollback = Core::makeScopeExit([&]() noexcept {
        if (nativeWindow != nullptr)
        {
            glfwDestroyWindow(nativeWindow);
        }
        if (initialized)
        {
            glfwTerminate();
        }
        g_glfwBackendActive.store(false, std::memory_order_release);
    });

    clearGlfwErrors();
    if (glfwInit() != GLFW_TRUE)
    {
        return Core::failure(
            glfwFailure(PlatformErrorCode::BackendInitializationFailed, "GLFW initialization failed", "glfwInit"));
    }
    initialized = true;

    auto plan = buildWindowCreatePlan(params.primaryWindow);
    if (!plan)
    {
        return std::unexpected(std::move(plan.error()));
    }

    clearGlfwErrors();
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, plan->resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, plan->decorated ? GLFW_TRUE : GLFW_FALSE);
    if (auto status = checkGlfwOperation("configure GLFW window hints"); !status)
    {
        return std::unexpected(std::move(status.error()));
    }

    clearGlfwErrors();
    nativeWindow = glfwCreateWindow(plan->width, plan->height, params.primaryWindow.title.c_str(), nullptr, nullptr);
    if (nativeWindow == nullptr)
    {
        return Core::failure(glfwFailure(PlatformErrorCode::WindowCreationFailed,
                                         "The GLFW primary window could not be created", "glfwCreateWindow"));
    }
    if (plan->positionExplicitly && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
    {
        glfwSetWindowPos(nativeWindow, plan->positionX, plan->positionY);
        if (auto status = checkGlfwOperation("glfwSetWindowPos"); !status)
        {
            return std::unexpected(std::move(status.error()));
        }
    }

    auto windowId = windowPool->tryEmplace(GlfwWindowRecord{.native = nativeWindow});
    if (!windowId)
    {
        return std::unexpected(std::move(windowId.error()));
    }
    auto initialMetrics = readWindowMetrics(nativeWindow, *windowId, 1, {});
    if (!initialMetrics)
    {
        return std::unexpected(std::move(initialMetrics.error()));
    }
    WindowInputSnapshot initialInput{
        .window = *windowId,
        .sourceMetricsRevision = 1,
    };

    auto* concreteBackend = new (std::nothrow) GlfwPlatformBackend(
        std::move(*windowPool), *windowId, std::move(*surfacePool), *surfaceId, std::move(surfaceLeaseControl),
        nativeWindow, std::move(*frameBuilder), *initialMetrics, initialInput, params.primaryWindow.initiallyVisible);
    if (concreteBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "The GLFW platform backend allocation failed");
    }
    std::unique_ptr<GlfwPlatformBackend> backend{concreteBackend};
    nativeWindow = nullptr;
    initialized = false;
    rollback.release();

    clearGlfwErrors();
    backend->registerCallbacks();
    if (auto status = checkGlfwOperation("register GLFW callbacks"); !status)
    {
        return std::unexpected(std::move(status.error()));
    }
    if (auto status = backend->finishCreation(publishDuringCreation); !status)
    {
        return std::unexpected(std::move(status.error()));
    }

    return backend;
}

#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
[[nodiscard]] GlfwPlatformBackend* glfwBackendForTest(IPlatformBackend& backend) noexcept
{
    if (auto* directBackend = dynamic_cast<GlfwPlatformBackend*>(&backend); directBackend != nullptr)
    {
        return directBackend;
    }
    if (auto* independentBackend = dynamic_cast<GlfwIndependentPlatformBackend*>(&backend);
        independentBackend != nullptr)
    {
        return &independentBackend->implementationForTest();
    }
    return nullptr;
}
#endif

} // namespace

#if defined(TINA_PLATFORM_GLFW_ENABLE_TEST_ACCESS)
namespace Detail {

Core::Status requestGlfwCloseForTest(IPlatformBackend& backend) noexcept
{
    auto* glfwBackend = glfwBackendForTest(backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->requestCloseForTest();
}

Core::Status resizeGlfwWindowForTest(IPlatformBackend& backend, LogicalExtent extent) noexcept
{
    auto* glfwBackend = glfwBackendForTest(backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->resizeForTest(extent);
}

Core::Status iconifyGlfwWindowForTest(IPlatformBackend& backend) noexcept
{
    auto* glfwBackend = glfwBackendForTest(backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->iconifyForTest();
}

Core::Status failNextGlfwPollForTest(IPlatformBackend& backend) noexcept
{
    auto* glfwBackend = glfwBackendForTest(backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->failNextPollForTest();
}

Core::Status forceGlfwSuspendedWaitPathForTest(IPlatformBackend& backend, double waitTimeoutSeconds) noexcept
{
    auto* glfwBackend = glfwBackendForTest(backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->forceSuspendedWaitPathForTest(waitTimeoutSeconds);
}

Core::Status queueGlfwPointerEventsForNextPollForTest(IPlatformBackend& backend,
                                                      std::span<const GlfwPointerInjection> events) noexcept
{
    auto* glfwBackend = glfwBackendForTest(backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->queuePointerEventsForNextPollForTest(events);
}

Core::Result<GlfwEventPumpStats> glfwEventPumpStatsForTest(IPlatformBackend& backend) noexcept
{
    auto* glfwBackend = glfwBackendForTest(backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->eventPumpStatsForTest();
}

Core::Result<GlfwRuntimePlatform> glfwRuntimePlatformForTest(IPlatformBackend& backend) noexcept
{
    auto* glfwBackend = glfwBackendForTest(backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->runtimePlatformForTest();
}

Core::Result<bool> glfwWindowVisibleForTest(IPlatformBackend& backend) noexcept
{
    auto* glfwBackend = glfwBackendForTest(backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->windowVisibleForTest();
}

} // namespace Detail
#endif

Core::Result<std::unique_ptr<IPlatformBackend>> createGlfwPlatformBackend(const PlatformBackendCreateParams& params)
{
    try
    {
        auto backend = createBackendUnchecked(params, true);
        if (!backend)
        {
            return std::unexpected(std::move(backend.error()));
        }
        auto independentBackend = std::make_unique<GlfwIndependentPlatformBackend>(std::move(*backend));
        return std::unique_ptr<IPlatformBackend>{std::move(independentBackend)};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "The GLFW platform backend ran out of memory");
    } catch (const std::exception& exception)
    {
        Core::Error error{PlatformErrorCode::BackendInitializationFailed,
                          "The GLFW platform backend threw during creation"};
        error.addContext("createGlfwPlatformBackend", exception.what());
        return Core::failure(std::move(error));
    } catch (...)
    {
        return Core::failure(PlatformErrorCode::BackendInitializationFailed,
                             "The GLFW platform backend threw a non-standard exception during creation");
    }
}

Core::Result<std::unique_ptr<Integration::IWindowSurfacePlatformBackend>>
createGlfwWindowSurfacePlatformBackend(const PlatformBackendCreateParams& params)
{
    try
    {
        auto backend = createBackendUnchecked(params, false);
        if (!backend)
        {
            return std::unexpected(std::move(backend.error()));
        }
        return std::unique_ptr<Integration::IWindowSurfacePlatformBackend>{std::move(*backend)};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "The GLFW WindowSurface platform backend ran out of memory");
    } catch (const std::exception& exception)
    {
        Core::Error error{PlatformErrorCode::BackendInitializationFailed,
                          "The GLFW WindowSurface platform backend threw during creation"};
        error.addContext("createGlfwWindowSurfacePlatformBackend", exception.what());
        return Core::failure(std::move(error));
    } catch (...)
    {
        return Core::failure(PlatformErrorCode::BackendInitializationFailed,
                             "The GLFW WindowSurface platform backend threw an unknown exception during creation");
    }
}

} // namespace Tina::Platform
