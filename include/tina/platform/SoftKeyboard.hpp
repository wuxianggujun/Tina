#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::Platform {

// Platform-neutral capability for mobile soft keyboard requests and observation.
//
// A backend that supports soft keyboard MUST return non-null from
// softKeyboard() and keep the same instance alive for its whole active
// lifetime, so callers may cache the pointer until shutdown. A backend without
// soft keyboard capability (GLFW, Headless, Html5) returns nullptr.
//
// Runtime latches requests through this capability; reading the pending request
// does NOT clear it. The host calls the system IME API, and only after that
// succeeds should it call acknowledgeSoftKeyboardRequest() to clear the latch.
// This prevents loss of intent when InputMethodManager is temporarily
// unavailable or a UIView has no window token.
//
// The keyboard occlusion height is reported by the host after the system
// keyboard appears or resizes. The backend stores it and returns the
// window-logical value on query so UI code can subtract it directly from
// viewport height to compute the available editing area.
class ISoftKeyboard {
  public:
    virtual ~ISoftKeyboard() noexcept = default;
    ISoftKeyboard(const ISoftKeyboard&) = delete;
    ISoftKeyboard& operator=(const ISoftKeyboard&) = delete;
    ISoftKeyboard(ISoftKeyboard&&) = delete;
    ISoftKeyboard& operator=(ISoftKeyboard&&) = delete;

    // Owner-thread request writes. They update the backend's intent latch only;
    // native IME calls remain the host's responsibility.
    virtual void requestShow() noexcept = 0;
    virtual void requestHide() noexcept = 0;

    // Latched intent for the host to inspect. Reading does not clear it:
    // becoming first responder can fail while a view is off-screen or
    // mid-transition, and consuming at read time would lose the intent
    // permanently.
    //
    // Returns true when a Show request is pending, false when a Hide request
    // is pending or no request is pending. Distinguishing Hide-vs-None is not
    // necessary: both mean "don't show the keyboard right now", and the
    // distinction only matters for the backend's internal state tracking.
    [[nodiscard]] virtual bool pendingShowRequest() const noexcept = 0;

    // Clears the pending Show intent only when one is still present. A Hide
    // request or no request at all both leave the state unchanged, so a late
    // acknowledgement of an old Show cannot erase a newer Hide.
    virtual void acknowledgeShowRequest() noexcept = 0;

    // Height of the window bottom currently covered by the soft keyboard, in
    // window-logical units so UI code can subtract it directly. Zero when
    // hidden.
    //
    // This is what makes a soft keyboard usable at all: without it a focused
    // text field sits behind the keyboard with no way to know it must scroll
    // into view.
    [[nodiscard]] virtual float occludedLogicalHeight() const noexcept = 0;

  protected:
    ISoftKeyboard() noexcept = default;
};

} // namespace Tina::Platform
