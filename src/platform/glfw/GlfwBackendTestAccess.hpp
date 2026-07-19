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

[[nodiscard]] Core::Status requestGlfwCloseForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Status resizeGlfwWindowForTest(IPlatformBackend& backend, LogicalExtent extent) noexcept;
[[nodiscard]] Core::Status iconifyGlfwWindowForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Status failNextGlfwPollForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Status forceGlfwSuspendedWaitPathForTest(IPlatformBackend& backend,
                                                            double waitTimeoutSeconds) noexcept;
[[nodiscard]] Core::Status
queueGlfwPointerEventsForNextPollForTest(IPlatformBackend& backend,
                                         std::span<const GlfwPointerInjection> events) noexcept;
[[nodiscard]] Core::Result<GlfwEventPumpStats> glfwEventPumpStatsForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Result<GlfwRuntimePlatform> glfwRuntimePlatformForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Result<bool> glfwWindowVisibleForTest(IPlatformBackend& backend) noexcept;

} // namespace Tina::Platform::Detail
