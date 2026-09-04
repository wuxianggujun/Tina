#pragma once

#include <tina/integration/WindowSurface.hpp>
#include <tina/platform/PlatformBackend.hpp>
#include <tina/platform/ios/IosInputBridge.hpp>

#include <memory>
#include <optional>

namespace Tina::Platform {

// iOS's drawable layer, handed over from the UIKit side.
//
// Deliberately an opaque std::uintptr_t rather than CAMetalLayer*: Game SDK headers must not expose
// platform SDK types, and the private platform/render bridge is the sole decoder of a native
// binding. The host obtains the layer from its UIView (`view.layer`, with layerClass returning
// CAMetalLayer) and passes its numeric value here.
//
// It is the *layer*, not the UIView and not the UIWindow. bgfx's iOS Metal context casts nwh
// straight to a CAMetalLayer when the handle type says so, and handing over a UIView instead
// produces a pointer of the wrong type that fails at draw time rather than at bind time.
//
// Tina does not own the layer's lifetime. iOS releases and recreates it across scene disconnection
// and some rotations, so the host must keep it alive for as long as the backend holds it, and hand
// over a replacement through the surface-rebind path rather than mutating this value.
struct IosNativeLayerHandle final {
    std::uintptr_t metalLayer = 0;
};

struct IosPlatformBackendCreateParams final {
    PlatformBackendCreateParams platform{};
    IosNativeLayerHandle layer{};
    // Shared with the host, which pushes from UIKit's main thread while the backend drains on the
    // owner thread. Shared rather than backend-owned because the two ends have different lifetimes:
    // touches can arrive before the backend exists and after it is destroyed, and a backend-owned
    // queue would force the host to guard every push against that.
    //
    // Empty is valid and means no input: frames then carry a window with every pointer absent. That
    // keeps the surface-only configuration usable (and testable) without a fake bridge.
    std::shared_ptr<IosTouchEventQueue> touchEvents{};
    // Same ownership rule, and empty is likewise valid: an iPhone with no hardware keyboard
    // attached never pushes one of these.
    std::shared_ptr<IosKeyEventQueue> keyEvents{};
    // Committed text. Separate from keyEvents because the two are different things on iOS: the
    // software keyboard delivers text through UITextInput's insertText: without producing UIKey
    // events at all, so a backend that only handled keys would see nothing while the user typed.
    //
    // Only carries commits that arrive with no marked text in flight. Once a preedit is active the
    // commit that resolves it travels through compositionEvents instead, because the Ended stage and
    // the text it produced have to keep their relative order and two rings cannot express that.
    std::shared_ptr<IosTextEventQueue> textEvents{};
    // Marked (preedit) text, plus the commits that end a marked pass.
    //
    // Same ownership rule and empty is likewise valid: a Latin keyboard never marks text, and a host
    // that does not forward setMarkedText: simply gets the committed-text behaviour.
    std::shared_ptr<IosCompositionEventQueue> compositionEvents{};
    // Drawable size in physical pixels, which is CAMetalLayer.drawableSize. Tina cannot query it
    // without linking QuartzCore into a header-visible path, and the host already has it at the
    // point it configures the layer.
    FramebufferExtent framebufferExtent{};
    // UIScreen/UITraitCollection nativeScale as a scale factor (2.0 or 3.0 on current devices). 0 is
    // rejected rather than defaulted: a wrong content scale silently mis-sizes every UI element,
    // which is far harder to notice than a startup failure.
    ContentScale contentScale{};
};

// Latched software-keyboard intent. Only UIKit can make a view first responder, so the engine
// records what it wants and the host performs it.
enum class IosSoftKeyboardRequest : u8 {
    None,
    Show,
    Hide,
};

// Caret geometry for the input system, in points from the window's top-left.
//
// Points rather than pixels because that is the space every UITextInput geometry callback works in
// (firstRectForRange: returns a CGRect in view coordinates), and the backend is where the content
// scale lives -- converting in ObjC would put the scale factor in two places. This is the one place
// the iOS facet deliberately differs from the Android one, which uses physical pixels because
// CursorAnchorInfo does.
struct IosCaretPoints final {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    auto operator<=>(const IosCaretPoints&) const = default;
};

// The iOS-specific lifecycle the generic platform interface has no place for.
//
// A separate interface rather than new IPlatformBackend virtuals: no desktop backend can implement
// these meaningfully, and adding them there would force GLFW, Headless and every test double to
// carry methods they must reject. Hosts reach this by dynamic_cast from the surface backend they
// created, exactly as the Android host does.
class IIosPlatformBackend {
  public:
    virtual ~IIosPlatformBackend() noexcept = default;

    // The drawable went away: the scene disconnected, or the view left the window. Frames continue
    // but report the surface suspended, so the engine keeps ticking without drawing to a dead layer.
    //
    // Also releases every touch slot: a drag interrupted by a task switch must not strand its
    // finger, which is the cocos2d-x defect ADR 0032 cites -- and on iOS it is worse, because UIKit
    // delivers no touchesCancelled: when the scene is torn down. An in-flight composition is
    // cancelled and the caret cleared for the same reason.
    virtual Core::Status onNativeLayerReleased() noexcept = 0;

    // A drawable arrived or was replaced. Advances nativeBindingRevision so the render device
    // rebuilds its swapchain against the replacement.
    //
    // Extent and scale are required because the new layer may differ from the old one -- a rotation
    // or a Split View resize is a common reason the layer was recreated at all.
    [[nodiscard]] virtual Core::Status onNativeLayerAcquired(IosNativeLayerHandle layer,
                                                            FramebufferExtent framebufferExtent,
                                                            ContentScale contentScale) noexcept = 0;

    // The drawable stayed the same but its size changed, which is what a rotation without a layer
    // swap looks like. Separate from onNativeLayerAcquired because the binding is unchanged: bgfx
    // needs a reset, not a rebind, and advancing nativeBindingRevision here would make the render
    // device throw away a swapchain that is still valid.
    [[nodiscard]] virtual Core::Status onDrawableResized(FramebufferExtent framebufferExtent,
                                                         ContentScale contentScale) noexcept = 0;

    // Total touch events the shared queue had no room for. Exposed so a host can tell a genuinely
    // undersized capacity from a quiet device.
    [[nodiscard]] virtual u64 droppedTouchEventCount() const noexcept = 0;

    // Committed text transitions this backend published into frames.
    //
    // Exposed because committed text carries no held state: unlike a key it leaves nothing in
    // WindowInputSnapshot to inspect, so without a count there is no way to tell "the keyboard sent
    // nothing" from "the text never reached a frame".
    [[nodiscard]] virtual u64 publishedTextCommitCount() const noexcept = 0;

    // --- Software keyboard ---
    //
    // Requests that the host show or hide the keyboard. Requests, not commands: showing it means
    // making a UIView the first responder, which only UIKit can do. A backend that pretended to have
    // done it would be claiming an integration it cannot perform from C++.
    [[nodiscard]] virtual Core::Status requestShowSoftKeyboard() noexcept = 0;
    [[nodiscard]] virtual Core::Status requestHideSoftKeyboard() noexcept = 0;

    // The host reports how much of the window bottom the keyboard covers, in physical pixels, from
    // UIKeyboardFrameEndUserInfoKey intersected with the view. 0 means hidden.
    //
    // Reported rather than derived: the engine cannot compute it. The height depends on the keyboard
    // type, the language, whether a candidate bar or QuickType row is showing, whether a hardware
    // keyboard is attached (which leaves only the shortcut bar), and Split View geometry.
    [[nodiscard]] virtual Core::Status onSoftKeyboardOcclusionChanged(u32 occludedPhysicalHeight) noexcept = 0;

    // Height of the window bottom currently covered by the keyboard, in window-logical units so UI
    // code can subtract it directly. 0 when hidden.
    //
    // This is the part that makes a software keyboard usable at all: without it a focused text field
    // sits behind the keyboard with no way to know it must scroll into view.
    [[nodiscard]] virtual float softKeyboardOccludedLogicalHeight() const noexcept = 0;

    // Latched intent for the host to inspect. Reading does not clear it: becoming first responder can
    // fail while a view is off-screen or mid-transition, and consuming at read time would lose the
    // intent permanently.
    [[nodiscard]] virtual IosSoftKeyboardRequest pendingSoftKeyboardRequest() const noexcept = 0;

    // Clears the pending intent only when it still matches the request the host successfully applied,
    // so a newer opposite request cannot be erased by a late acknowledgement of an older read.
    [[nodiscard]] virtual Core::Status acknowledgeSoftKeyboardRequest(IosSoftKeyboardRequest request) noexcept = 0;

    // --- Caret placement ---
    //
    // Where the focused TextEdit's caret is, for the host to return from firstRectForRange: and
    // friends. nullopt when nothing is focused.
    //
    // Non-consuming, unlike the keyboard request: a caret is continuous state, not a one-shot intent.
    // UIKit asks for this geometry whenever it repositions the candidate bar or the magnifier, and
    // consume-on-read would leave the second query with nothing.
    [[nodiscard]] virtual std::optional<IosCaretPoints> caretPoints() const noexcept = 0;

    // Composition transitions this backend published into frames, by stage.
    //
    // Split by stage rather than totalled because the failure modes are distinct and a single counter
    // hides all of them: Started without Ended means a preedit is stuck on screen, Ended without
    // Started means the session missed the beginning, and Cancelled climbing on its own means the
    // input system keeps taking and dropping the marked region.
    [[nodiscard]] virtual u64 publishedCompositionStartCount() const noexcept = 0;
    [[nodiscard]] virtual u64 publishedCompositionUpdateCount() const noexcept = 0;
    [[nodiscard]] virtual u64 publishedCompositionEndCount() const noexcept = 0;
    [[nodiscard]] virtual u64 publishedCompositionCancelCount() const noexcept = 0;
};

// Creates the iOS window-surface adapter.
//
// It publishes the native layer binding and window metrics so the bgfx RenderDevice can bind a
// CAMetalLayer, and -- when given queues -- drains them into each polled frame. Without them the
// frames carry a window but no input, with every pointer reported absent rather than resting at the
// origin: on touch there is no position between taps, so a present-but-idle pointer would latch
// hover on whatever was last touched (ADR 0032 C2).
//
// Thread affinity matches every other production backend: creation, every poll, shutdown and
// destruction happen on one thread. On iOS that is whichever thread drives EngineHost::tick(), which
// in practice is the main thread because CAMetalLayer must be touched there -- and it is why ADR
// 0032's D3 chose external frame driving. There is no blocking run() on this platform: a
// CADisplayLink callback calls tick() once per frame.
[[nodiscard]] Core::Result<std::unique_ptr<Integration::IWindowSurfacePlatformBackend>>
createIosWindowSurfacePlatformBackend(const IosPlatformBackendCreateParams& params);

} // namespace Tina::Platform
