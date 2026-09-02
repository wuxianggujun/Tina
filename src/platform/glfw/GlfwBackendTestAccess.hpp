#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/Input.hpp>
#include <tina/platform/PlatformBackend.hpp>

#include <span>

namespace Tina::Platform::Detail {

enum class GlfwRuntimePlatform : u8 {
    Unknown,
    Win32,
    Cocoa,
    Wayland,
    X11,
    Null,
};

struct GlfwEventPumpStats final {
    u64 pollEventsCalls = 0;
    u64 waitEventsTimeoutCalls = 0;
};

struct GlfwPointerCaptureState final {
    // What the caller asked for, which a deferred lock must still report.
    PointerCaptureMode requestedMode = PointerCaptureMode::Free;
    // Whether the native cursor mode is actually applied right now.
    bool cursorHidden = false;
};

enum class GlfwPointerInjectionKind : u8 {
    CursorPosition,
    Button,
    Wheel,
};

struct GlfwPointerInjection final {
    GlfwPointerInjectionKind kind = GlfwPointerInjectionKind::CursorPosition;
    double logicalX = 0.0;
    double logicalY = 0.0;
    PointerButton button = PointerButton::Primary;
    DigitalTransition transition = DigitalTransition::Down;
    double wheelDeltaX = 0.0;
    double wheelDeltaY = 0.0;
};

// The injection span is borrowed only for the queue call. The backend copies
// every non-null path into its fixed private arena before the next poll.
struct GlfwFileDropInjection final {
    std::span<const char* const> paths{};
    bool nullPathArray = false;
};

enum class GlfwCallbackAssemblyFailure : i64 {
    None = 0,
    SequenceExhausted = 1,
    InvalidPayload = 2,
    FrameNotOpen = 3,
    InvalidTextCodepoint = 4,
};

[[nodiscard]] Core::Status requestGlfwCloseForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Status resizeGlfwWindowForTest(IPlatformBackend& backend, LogicalExtent extent) noexcept;
[[nodiscard]] Core::Status iconifyGlfwWindowForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Status failNextGlfwPollForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Status forceGlfwSuspendedWaitPathForTest(IPlatformBackend& backend,
                                                            double waitTimeoutSeconds) noexcept;
[[nodiscard]] Core::Status
queueGlfwPointerEventsForNextPollForTest(IPlatformBackend& backend,
                                         std::span<const GlfwPointerInjection> events) noexcept;
[[nodiscard]] Core::Status queueGlfwFileDropForNextPollForTest(IPlatformBackend& backend,
                                                               GlfwFileDropInjection injection) noexcept;
[[nodiscard]] Core::Result<GlfwEventPumpStats> glfwEventPumpStatsForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Result<GlfwRuntimePlatform> glfwRuntimePlatformForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Result<GlfwPointerCaptureState>
glfwPointerCaptureStateForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Result<bool> glfwWindowVisibleForTest(IPlatformBackend& backend) noexcept;

} // namespace Tina::Platform::Detail
