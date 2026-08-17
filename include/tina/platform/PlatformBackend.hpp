#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>

#include <functional>
#include <memory>
#include <optional>

namespace Tina::Platform {

struct PlatformBackendCreateParams final {
    PrimaryWindowConfig primaryWindow{};
    PlatformFrameCapacityConfig frameCapacities{};
};

class IPlatformBackend {
  public:
    virtual ~IPlatformBackend() = default;

    // A production backend is thread-affine: factory creation, every poll,
    // shutdown, and destruction must occur on the same owner thread. Native
    // adapters may additionally require that owner to be the process platform
    // thread; see the concrete factory contract.
    // Startup-only snapshot query: it must not pump events, publish a frame, or
    // consume PlatformFrameId/source sequence. nullopt means this backend is
    // explicitly Headless for the complete run.
    [[nodiscard]] virtual Core::Result<std::optional<WindowMetricsSnapshot>> initialPrimaryWindowMetrics() = 0;
    [[nodiscard]] virtual Core::Result<PlatformPollResult> pollFrame() = 0;
    // Publishes or clears the committed TextEdit caret placement for the
    // backend-owned primary window. The value is window-logical and must never
    // expose native/third-party types. nullopt clears any active IME hint.
    // Implementations remain thread-affine and may treat unsupported IME
    // placement as a platform-specific no-op while preserving the contract.
    virtual Core::Status updateTextInputPlacement(std::optional<TextInputPlacement> placement) = 0;
    virtual void shutdown() noexcept = 0;
};

using PlatformBackendFactory =
    std::move_only_function<Core::Result<std::unique_ptr<IPlatformBackend>>(const PlatformBackendCreateParams&)>;

} // namespace Tina::Platform
