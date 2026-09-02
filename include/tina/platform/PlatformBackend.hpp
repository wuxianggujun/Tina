#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace Tina::Platform {

struct PlatformBackendCreateParams final {
    PrimaryWindowConfig primaryWindow{};
    PlatformFrameCapacityConfig frameCapacities{};
    // Optional Desktop adapter event source. Disabled by default so Headless,
    // tests, and products with an explicit theme remain deterministic.
    bool publishSystemColorSchemeEvents = false;
    // Optional OS file-drop event source. Authoring hosts opt in explicitly.
    bool acceptFileDropEvents = false;
    // Additional SDL_GameControllerDB mapping lines, newline-separated. A desktop
    // backend only recognises pads it has a mapping for, so a controller the
    // shipped mapping table predates is otherwise invisible with no diagnostic.
    // Supplying entries here is the only way to fix that without rebuilding the
    // backend's third-party dependency. Ignored by backends with no mapping
    // concept, such as Headless.
    std::string_view gamepadMappings{};
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
    // Sets the cursor mode of the backend-owned primary window.
    //
    // Pure virtual with no default: a backend that cannot lock a cursor must say so
    // rather than accept the request and leave the caller believing a first-person
    // camera will work. Headless and Android therefore fail Locked outright.
    //
    // A backend switching to Locked must also drop the position difference across the
    // switch. The native layer warps the cursor as it captures it, and reporting that
    // warp as pointer movement would spin a first-person camera on the first frame.
    // Implementations remain thread-affine.
    virtual Core::Status setPointerCaptureMode(PointerCaptureMode mode) = 0;
    virtual void shutdown() noexcept = 0;
};

using PlatformBackendFactory =
    Core::MoveOnlyFunction<Core::Result<std::unique_ptr<IPlatformBackend>>(const PlatformBackendCreateParams&)>;

} // namespace Tina::Platform
