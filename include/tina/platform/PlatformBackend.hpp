#pragma once

#include <tina/core/base/MoveOnlyFunction.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/Clipboard.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/SoftKeyboard.hpp>
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
    // The system clipboard, or nullptr when this backend has none.
    //
    // Pure virtual with no default for the same reason as setPointerCaptureMode:
    // a backend must state whether the capability exists instead of accepting
    // calls it cannot honour. Returning nullptr is that statement, and it is
    // checkable once at wiring time rather than on every paste.
    //
    // The returned clipboard is owned by the backend and borrowed by the caller.
    // It must not outlive the backend, and it inherits the backend's owner
    // thread. A backend that returns non-null must keep the same instance alive
    // and valid for its whole active lifetime, so callers may cache the pointer
    // until shutdown().
    [[nodiscard]] virtual IClipboard* clipboard() noexcept = 0;
    // The soft keyboard capability, or nullptr when this backend has none.
    //
    // Pure virtual with no default for the same reason as clipboard: a backend
    // must state whether the capability exists. Mobile backends (Android, iOS)
    // return a non-null instance; desktop and Headless return nullptr.
    //
    // The returned instance is owned by the backend and borrowed by the caller.
    // It must not outlive the backend, and it inherits the backend's owner
    // thread. A backend that returns non-null must keep the same instance alive
    // and valid for its whole active lifetime, so callers may cache the pointer
    // until shutdown().
    [[nodiscard]] virtual ISoftKeyboard* softKeyboard() noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

using PlatformBackendFactory =
    Core::MoveOnlyFunction<Core::Result<std::unique_ptr<IPlatformBackend>>(const PlatformBackendCreateParams&)>;

} // namespace Tina::Platform
