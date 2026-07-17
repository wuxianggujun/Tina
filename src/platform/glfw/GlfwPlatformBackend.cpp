#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/platform/PlatformErrors.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>

#include "GlfwBackendTestAccess.hpp"
#include "GlfwDigitalFocusFilter.hpp"
#include "GlfwInputTranslation.hpp"

#include <GLFW/glfw3.h>

#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace Tina::Platform {
namespace {

constexpr usize GlfwErrorDescriptionCapacity = 512;

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

[[nodiscard]] Core::Result<WindowMetricsSnapshot> readWindowMetrics(GLFWwindow* window, WindowId id, u64 revision)
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
    if (logicalWidth <= 0 || logicalHeight <= 0 || framebufferWidth < 0 || framebufferHeight < 0 ||
        !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0.0F || scaleY <= 0.0F)
    {
        return Core::failure(PlatformErrorCode::BackendOperationFailed, "GLFW returned invalid primary window metrics");
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

class GlfwPlatformBackend final : public IPlatformBackend {
  public:
    using WindowPool = Core::GenerationPool<GlfwWindowRecord, WindowRegistryTag>;

    GlfwPlatformBackend(WindowPool windows, WindowId windowId, GLFWwindow* window, PlatformFrameBuilder frameBuilder,
                        WindowMetricsSnapshot metrics, WindowInputSnapshot input) noexcept
        : windows_(std::move(windows)), windowId_(windowId), window_(window), frameBuilder_(std::move(frameBuilder)),
          metrics_(metrics), input_(input), ownerThread_(std::this_thread::get_id())
    {
    }

    ~GlfwPlatformBackend() noexcept override
    {
        shutdown();
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
        clearGlfwErrors();
        glfwPollEvents();
        collectingFrame_ = false;

        if (auto status = checkGlfwOperation("glfwPollEvents"); !status)
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

        if (metricsDirty_)
        {
            auto latestMetrics = readWindowMetrics(window_, windowId_, metrics_.revision);
            if (!latestMetrics)
            {
                return std::unexpected(std::move(latestMetrics.error()));
            }
            metricsDirty_ = false;
            if (!sameMetricsFacts(metrics_, *latestMetrics))
            {
                if (metrics_.revision == (std::numeric_limits<u64>::max)())
                {
                    return Core::failure(PlatformErrorCode::BackendOperationFailed,
                                         "The primary window metrics revision is exhausted");
                }
                latestMetrics->revision = metrics_.revision + 1;
                metrics_ = *latestMetrics;
                recordAppend(frameBuilder_.appendPlatformEvent(WindowMetricsChangedEvent{
                    .window = windowId_,
                    .metricsRevision = metrics_.revision,
                }));
            }
        }
        if (callbackFailure_ != CallbackAssemblyFailure::None)
        {
            return Core::failure(PlatformErrorCode::CallbackFrameAssemblyFailed,
                                 "The window metrics event could not be appended");
        }
        if (failNextPollForTest_)
        {
            failNextPollForTest_ = false;
            return Core::failure(PlatformErrorCode::BackendOperationFailed,
                                 "The GLFW test seam injected a recoverable poll failure");
        }

        input_.sourceMetricsRevision = metrics_.revision;
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
        stopped_ = true;
        collectingFrame_ = false;
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

    [[nodiscard]] Core::Status finishCreation(bool initiallyVisible)
    {
        if (initiallyVisible)
        {
            clearGlfwErrors();
            glfwShowWindow(window_);
            if (auto status = checkGlfwOperation("glfwShowWindow"); !status)
            {
                return status;
            }
        }
        return refreshInitialState();
    }

    [[nodiscard]] Core::Status refreshInitialState()
    {
        auto latestMetrics = readWindowMetrics(window_, windowId_, 1);
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
        return Core::success();
    }

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
        const bool held = action == GLFW_PRESS;
        const DigitalTransition transition = held ? DigitalTransition::Down : DigitalTransition::Up;
        if (!focusFilter_.shouldAccept(*button, transition))
        {
            return;
        }
        input_.pointer.heldButtons.set(static_cast<usize>(*button), held);
        recordAppend(frameBuilder_.appendInputTransition(PointerButtonTransition{
            .window = windowId_,
            .pointer = PrimaryPointerId,
            .button = *button,
            .state = transition,
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
        }));
    }

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
    GLFWwindow* window_ = nullptr;
    PlatformFrameBuilder frameBuilder_;
    WindowMetricsSnapshot metrics_{};
    WindowInputSnapshot input_{};
    Detail::GlfwDigitalFocusFilter focusFilter_{};
    std::thread::id ownerThread_{};
    u64 nextFrameId_ = 1;
    CallbackAssemblyFailure callbackFailure_ = CallbackAssemblyFailure::None;
    bool collectingFrame_ = false;
    bool focusCancelPending_ = false;
    bool streamRecoveryPending_ = false;
    bool metricsDirty_ = false;
    bool closeRequested_ = false;
    bool failNextPollForTest_ = false;
    bool stopped_ = false;
};

[[nodiscard]] Core::Result<std::unique_ptr<IPlatformBackend>>
createBackendUnchecked(const PlatformBackendCreateParams& params)
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
    auto initialMetrics = readWindowMetrics(nativeWindow, *windowId, 1);
    if (!initialMetrics)
    {
        return std::unexpected(std::move(initialMetrics.error()));
    }
    WindowInputSnapshot initialInput{
        .window = *windowId,
        .sourceMetricsRevision = 1,
    };

    auto* concreteBackend = new (std::nothrow) GlfwPlatformBackend(
        std::move(*windowPool), *windowId, nativeWindow, std::move(*frameBuilder), *initialMetrics, initialInput);
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
    if (auto status = backend->finishCreation(params.primaryWindow.initiallyVisible); !status)
    {
        return std::unexpected(std::move(status.error()));
    }

    return std::unique_ptr<IPlatformBackend>{std::move(backend)};
}

} // namespace

namespace Detail {

Core::Status requestGlfwCloseForTest(IPlatformBackend& backend) noexcept
{
    auto* glfwBackend = dynamic_cast<GlfwPlatformBackend*>(&backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->requestCloseForTest();
}

Core::Status resizeGlfwWindowForTest(IPlatformBackend& backend, LogicalExtent extent) noexcept
{
    auto* glfwBackend = dynamic_cast<GlfwPlatformBackend*>(&backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->resizeForTest(extent);
}

Core::Status failNextGlfwPollForTest(IPlatformBackend& backend) noexcept
{
    auto* glfwBackend = dynamic_cast<GlfwPlatformBackend*>(&backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->failNextPollForTest();
}

Core::Result<GlfwRuntimePlatform> glfwRuntimePlatformForTest(IPlatformBackend& backend) noexcept
{
    auto* glfwBackend = dynamic_cast<GlfwPlatformBackend*>(&backend);
    if (glfwBackend == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "The backend is not a GLFW platform backend");
    }
    return glfwBackend->runtimePlatformForTest();
}

} // namespace Detail

Core::Result<std::unique_ptr<IPlatformBackend>> createGlfwPlatformBackend(const PlatformBackendCreateParams& params)
{
    try
    {
        return createBackendUnchecked(params);
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

} // namespace Tina::Platform
