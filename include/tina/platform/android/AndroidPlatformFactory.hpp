#pragma once

#include <tina/integration/WindowSurface.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/platform/android/AndroidInputBridge.hpp>

#include <memory>
#include <optional>

namespace Tina::Platform {

// Android's native window, handed over from the Java/Kotlin side.
//
// Deliberately an opaque std::uintptr_t rather than ANativeWindow*: Game SDK headers must not
// expose platform SDK types, and the private platform/render bridge is the sole decoder of a
// native binding. The host obtains the pointer from ANativeWindow_fromSurface() (or
// android_app::window) and passes its numeric value here.
//
// Tina does not own the window's lifetime. Android destroys and recreates it across
// background/foreground transitions, so the host must keep it alive for as long as the backend
// holds it, and hand over a replacement through the surface-rebind path rather than mutating
// this value.
struct AndroidNativeWindowHandle final {
    std::uintptr_t nativeWindow = 0;
};

struct AndroidPlatformBackendCreateParams final {
    PlatformBackendCreateParams platform{};
    AndroidNativeWindowHandle window{};
    // Shared with the host, which pushes from Android's UI thread while the backend drains on the
    // owner thread. Shared rather than backend-owned because the two ends have different
    // lifetimes: touches can arrive before the backend exists and after it is destroyed, and a
    // backend-owned queue would force the host to guard every push against that.
    //
    // Empty is valid and means no input: frames then carry a window with every pointer absent.
    // That keeps the surface-only configuration usable (and testable) without a fake bridge.
    std::shared_ptr<AndroidTouchEventQueue> touchEvents{};
    // Same ownership rule as touchEvents, and empty is likewise valid: a device with no keyboard and no
    // D-pad simply never pushes.
    std::shared_ptr<AndroidKeyEventQueue> keyEvents{};
    // Committed IME text. Separate from keyEvents because the two are different things on Android: the
    // soft keyboard delivers text through InputConnection without producing key codes at all, so a
    // backend that only handled keys would see nothing while the user typed.
    //
    // Only carries commits that arrive with no composition in flight. Once a preedit is active the
    // commit that resolves it travels through compositionEvents instead, because the Ended stage and
    // the text it produced have to keep their relative order and two rings cannot express that.
    std::shared_ptr<AndroidTextEventQueue> textEvents{};
    // Composing (preedit) text, plus the commits that end a composing pass.
    //
    // Same ownership rule and empty is likewise valid: an ASCII-only keyboard never composes, and a
    // host that does not forward setComposingText simply gets the committed-text behaviour.
    std::shared_ptr<AndroidCompositionEventQueue> compositionEvents{};
    // Framebuffer size in physical pixels, from ANativeWindow_getWidth/Height. Tina cannot
    // query it here without linking libandroid into a header-visible path, and the host
    // already has it at the point it acquires the window.
    FramebufferExtent framebufferExtent{};
    // Display density as scale factor (Android densityDpi / 160). 0 is rejected rather than
    // defaulted: a wrong content scale silently mis-sizes every UI element, which is far
    // harder to notice than a startup failure.
    ContentScale contentScale{};
};

// The Android-specific lifecycle the generic platform interface has no place for.
//
// Android destroys the native window when the activity goes to background and hands back a
// *different* one on return, so the engine must survive a window swap inside one run. That is what
// ADR 0034's nativeBindingRevision exists for; these methods are how a real platform drives it.
//
// A separate interface rather than new IPlatformBackend virtuals: no desktop backend can implement
// them meaningfully, and adding them there would force GLFW, Headless and every test double to
// carry a method they must reject. Hosts reach this by dynamic_cast from the surface backend they
// created.
// Latched soft-keyboard intent. Only Java can call InputMethodManager, so the engine records what
// it wants and the host performs it.
enum class AndroidSoftKeyboardRequest : u8 {
    None,
    Show,
    Hide,
};

// Caret geometry for the IME, in physical pixels from the window's top-left.
//
// Physical rather than logical because that is what CursorAnchorInfo takes, and the backend is where
// the content scale lives -- converting in Java would put the density in two places.
struct AndroidCaretPixels final {
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;

    auto operator<=>(const AndroidCaretPixels&) const = default;
};

class IAndroidPlatformBackend {
  public:
    virtual ~IAndroidPlatformBackend() noexcept = default;

    // The activity lost its window (APP_CMD_TERM_WINDOW). Frames continue but report the surface
    // suspended, so the engine keeps ticking without drawing to a dead window.
    //
    // Also releases every touch slot: a drag interrupted by a task switch must not strand its
    // finger, which is exactly the cocos2d-x defect ADR 0032 cites. An in-flight composition is
    // cancelled and the caret cleared for the same reason -- Android delivers no InputConnection call
    // when the window goes, so a preedit would otherwise stay on screen for the rest of the run.
    virtual Core::Status onNativeWindowDestroyed() noexcept = 0;

    // A new window arrived (APP_CMD_INIT_WINDOW). Advances nativeBindingRevision so the render
    // device rebuilds its backbuffer against the replacement.
    //
    // Extent and scale are required because the new window may differ from the old one -- a
    // rotation or a multi-window resize is a common reason the activity was recreated at all.
    [[nodiscard]] virtual Core::Status onNativeWindowCreated(AndroidNativeWindowHandle window,
                                                            FramebufferExtent framebufferExtent,
                                                            ContentScale contentScale) noexcept = 0;

    // Total touch events the shared queue had no room for. Exposed so a host can tell a genuinely
    // undersized capacity from a quiet device.
    [[nodiscard]] virtual u64 droppedTouchEventCount() const noexcept = 0;

    // Committed text transitions this backend published into frames.
    //
    // Exposed because committed text carries no held state: unlike a key it leaves nothing in
    // WindowInputSnapshot to inspect, so without a count there is no way to tell "the IME sent nothing"
    // from "the text never reached a frame".
    [[nodiscard]] virtual u64 publishedTextCommitCount() const noexcept = 0;

    // --- C6: soft keyboard ---
    //
    // Requests that the host show or hide the IME. Requests, not commands: only Java can call
    // InputMethodManager, so these record intent that the host acts on, and pendingSoftKeyboard-
    // Request() is how it observes that intent. A backend that pretended to have done it would be
    // claiming an integration it cannot perform from C++.
    [[nodiscard]] virtual Core::Status requestShowSoftKeyboard() noexcept = 0;
    [[nodiscard]] virtual Core::Status requestHideSoftKeyboard() noexcept = 0;

    // The host reports where the IME actually ended up, in physical pixels from the window's top
    // edge. Height 0 means hidden.
    //
    // Reported rather than derived: the engine cannot compute it. Android's IME height depends on
    // the keyboard app, the language, whether a suggestion strip is showing, and split-screen
    // geometry. Guessing it would misplace the focused field by an arbitrary amount.
    [[nodiscard]] virtual Core::Status onSoftKeyboardOcclusionChanged(u32 occludedPhysicalHeight) noexcept = 0;

    // Height of the window bottom currently covered by the IME, in window-logical units so UI code
    // can subtract it directly. 0 when hidden.
    //
    // This is the part that makes a soft keyboard usable at all: without it a focused text field
    // sits behind the keyboard with no way to know it must scroll into view.
    [[nodiscard]] virtual float softKeyboardOccludedLogicalHeight() const noexcept = 0;

    // Latched intent for the host to consume. Reading it clears it, so one request produces exactly
    // one InputMethodManager call rather than being re-applied every frame.
    [[nodiscard]] virtual AndroidSoftKeyboardRequest takePendingSoftKeyboardRequest() noexcept = 0;

    // --- Caret placement ---
    //
    // Where the focused TextEdit's caret is, for the host to hand to
    // InputMethodManager.updateCursorAnchorInfo. nullopt when nothing is focused.
    //
    // Unlike the keyboard request, reading does NOT clear this: a caret is continuous state, not a
    // one-shot intent. An IME asks for cursor updates and then expects the current value every time it
    // looks, so consume-on-read would make the second query see nothing.
    //
    // Latched rather than pushed because only Java can construct a CursorAnchorInfo, and only the IME
    // knows whether it wants one -- Android's contract is that a host reports this after
    // requestCursorUpdates(), not unconditionally.
    [[nodiscard]] virtual std::optional<AndroidCaretPixels> caretPixels() const noexcept = 0;

    // Composition transitions this backend published into frames, by stage.
    //
    // Split by stage rather than totalled because the failure modes are distinct and a single counter
    // hides all of them: Started without Ended means a preedit is stuck on screen, Ended without
    // Started means the session missed the beginning, and Cancelled climbing on its own means the IME
    // keeps taking and dropping the region. Committed text carries no held state either, which is the
    // same reason publishedTextCommitCount exists.
    [[nodiscard]] virtual u64 publishedCompositionStartCount() const noexcept = 0;
    [[nodiscard]] virtual u64 publishedCompositionUpdateCount() const noexcept = 0;
    [[nodiscard]] virtual u64 publishedCompositionEndCount() const noexcept = 0;
    [[nodiscard]] virtual u64 publishedCompositionCancelCount() const noexcept = 0;
};

// Creates the Android window-surface adapter.
//
// It publishes the native window binding and window metrics so the bgfx RenderDevice can bind an
// ANativeWindow, and -- when given a bridge -- drains that bridge's touch events into each polled
// frame. Without a bridge the frames carry a window but no input, with every pointer reported
// absent rather than resting at the origin: on touch there is no position between taps, so a
// present-but-idle pointer would latch hover on whatever was last touched (ADR 0032 C2).
//
// Keys, committed text, composing text and the soft keyboard are all wired. Thread affinity matches
// every other production backend: creation, every poll, shutdown and destruction happen on one thread.
// On Android that is whichever thread drives EngineHost::tick(), which is why ADR 0032's D3 chose
// external frame driving -- the platform's own UI thread need not be that thread.
[[nodiscard]] Core::Result<std::unique_ptr<Integration::IWindowSurfacePlatformBackend>>
createAndroidWindowSurfacePlatformBackend(const AndroidPlatformBackendCreateParams& params);

} // namespace Tina::Platform
