#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformBackend.hpp>

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

[[nodiscard]] Core::Status requestGlfwCloseForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Status resizeGlfwWindowForTest(IPlatformBackend& backend, LogicalExtent extent) noexcept;
[[nodiscard]] Core::Status failNextGlfwPollForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Status forceGlfwSuspendedWaitPathForTest(IPlatformBackend& backend,
                                                            double waitTimeoutSeconds) noexcept;
[[nodiscard]] Core::Result<GlfwEventPumpStats> glfwEventPumpStatsForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Result<GlfwRuntimePlatform> glfwRuntimePlatformForTest(IPlatformBackend& backend) noexcept;
[[nodiscard]] Core::Result<bool> glfwWindowVisibleForTest(IPlatformBackend& backend) noexcept;

} // namespace Tina::Platform::Detail
