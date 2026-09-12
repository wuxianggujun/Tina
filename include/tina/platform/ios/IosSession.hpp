#pragma once

#include <tina/core/base/Types.hpp>

#include <tina/platform/ios/IosPlatformFactory.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace Tina::Platform {

// The C++ half of the iOS host: owns the shared input rings, the UITouch slot table, and the
// platform backend. UIKit talks to this; this talks to the engine.
//
// Deliberately free of ObjC and of EngineHost. The former cannot compile here; the latter is a
// later slice that consumes the backend this session already builds. Splitting them is what lets
// the host-facing mapping (touch identities, UTF-16 commits, layer bind) run as ordinary unit
// tests on Windows rather than only compiling under Xcode.
//
// Same ownership rule as the Android JNI session: the queues outlive the backend, because UIKit
// can deliver touches before the layer exists and after it is gone.
class IosSession final {
  public:
    [[nodiscard]] static Core::Result<std::unique_ptr<IosSession>> Create();

    IosSession(const IosSession&) = delete;
    IosSession& operator=(const IosSession&) = delete;
    IosSession(IosSession&&) = delete;
    IosSession& operator=(IosSession&&) = delete;
    ~IosSession() noexcept;

    // First bind creates the backend; a later bind with a different layer is ADR 0034 rebind.
    // Extent and scale are required: the host already has CAMetalLayer.drawableSize and the
    // native scale at the point it configures the layer, and Tina cannot query either without
    // linking QuartzCore.
    [[nodiscard]] Core::Status bindLayer(IosNativeLayerHandle layer, FramebufferExtent framebufferExtent,
                                         ContentScale contentScale) noexcept;

    // Rotation without a layer swap. No-op when no backend exists yet: the next bind carries
    // the final geometry.
    [[nodiscard]] Core::Status resizeDrawable(FramebufferExtent framebufferExtent,
                                              ContentScale contentScale) noexcept;

    // Scene disconnected or the view left the window. Idempotent. Also releases every touch
    // slot: UIKit delivers no touchesCancelled: on teardown.
    void unbindLayer() noexcept;

    // One UITouch. Identity is the object address as an opaque integer; 0 is rejected because
    // it is the slot table's unused sentinel. Coordinates are points (locationInView), already
    // logical -- the backend must not divide them by content scale.
    [[nodiscard]] bool onTouch(Tina::Core::uintptr touchIdentity, IosTouchPhase phase, float pointX,
                               float pointY) noexcept;

    // One UIKey. hidUsage is UIKeyboardHIDUsage, untranslated; Key::Unknown is dropped here
    // rather than enqueued, so the host can let the system keep unmapped keys.
    [[nodiscard]] bool onKey(i32 hidUsage, IosKeyAction action, bool repeat) noexcept;

    // insertText: with no marked region in flight, or the commit that ends a marked pass --
    // the composition ring owns that distinction, not this method. UTF-16 because that is
    // what an NSString stores.
    [[nodiscard]] bool onTextCommitUtf16(std::u16string_view utf16) noexcept;

    // setMarkedText:selectedRange:. An empty string is how UIKit empties the region.
    [[nodiscard]] bool onSetMarkedTextUtf16(std::u16string_view utf16, i32 cursorUtf16Offset) noexcept;

    // unmarkText. Silent when nothing is in flight; the session, not the host, decides that.
    [[nodiscard]] bool onUnmarkText() noexcept;

    // Main-thread producer API. Stick Y uses positive-down, triggers use [0, 1].
    [[nodiscard]] bool onGamepadConnected(Tina::Core::uintptr deviceId, std::string_view name,
                                          std::string_view model) noexcept;
    [[nodiscard]] bool onGamepadDisconnected(Tina::Core::uintptr deviceId) noexcept;
    [[nodiscard]] bool onGamepadButton(Tina::Core::uintptr deviceId, GamepadButton button,
                                       DigitalTransition state) noexcept;
    [[nodiscard]] bool onGamepadAxis(Tina::Core::uintptr deviceId, GamepadAxis axis, float value) noexcept;
    [[nodiscard]] bool takeGamepadResyncRequest() noexcept;

    [[nodiscard]] IosSoftKeyboardRequest pendingSoftKeyboardRequest() const noexcept;
    [[nodiscard]] Core::Status acknowledgeSoftKeyboardRequest(IosSoftKeyboardRequest request) noexcept;
    [[nodiscard]] Core::Status onSoftKeyboardOcclusionChanged(u32 occludedPhysicalHeight) noexcept;
    [[nodiscard]] std::optional<IosCaretPoints> caretPoints() const noexcept;

    // One backend poll. No EngineHost: this is the host-facing half, and a later slice drives
    // tick() from CADisplayLink. Returns Exit when the session has been shut down.
    [[nodiscard]] Core::Result<PlatformPollResult> pollFrame();

    [[nodiscard]] IIosPlatformBackend* facet() noexcept;
    [[nodiscard]] Integration::IWindowSurfacePlatformBackend* backend() noexcept;

    void shutdown() noexcept;

  private:
    IosSession() = default;

    std::shared_ptr<IosTouchEventQueue> touchEvents_ = std::make_shared<IosTouchEventQueue>();
    std::shared_ptr<IosKeyEventQueue> keyEvents_ = std::make_shared<IosKeyEventQueue>();
    std::shared_ptr<IosTextEventQueue> textEvents_ = std::make_shared<IosTextEventQueue>();
    std::shared_ptr<IosCompositionEventQueue> compositionEvents_ =
        std::make_shared<IosCompositionEventQueue>();
    std::shared_ptr<MobileGamepadEventQueue> gamepadEvents_ = std::make_shared<MobileGamepadEventQueue>();
    IosTouchSlotTable slots_{};
    std::unique_ptr<Integration::IWindowSurfacePlatformBackend> backend_{};
    IIosPlatformBackend* iosBackend_ = nullptr;
    bool stopped_ = false;
};

} // namespace Tina::Platform
