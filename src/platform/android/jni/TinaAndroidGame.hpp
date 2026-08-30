#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/InputActions.hpp>

#include <atomic>
#include <memory>

namespace Tina::Platform::Android {

// What the APK observes about the running engine.
//
// Read from Java through JNI while the engine writes it from the frame loop, so each field is atomic
// and independently readable -- there is no consistent-snapshot requirement, and a mutex here would
// put the UI thread behind the frame loop.
struct AndroidGameTelemetry final {
    std::atomic<u64> frameUpdates{0};
    // Counted separately from frame updates because the two run off different clocks. Seeing both
    // advance on a device is what proves the fixed-step accumulator survived the move from a blocking
    // run() loop to Android's externally driven tick().
    std::atomic<u64> fixedUpdates{0};
    // UI updates that actually published a tree. Distinct from frameUpdates because a configuration
    // without a primary-window UI still runs frames -- so equal counters mean UI is live, and a frozen
    // uiUpdates against a climbing frameUpdates says the UI phase is being skipped.
    std::atomic<u64> uiUpdates{0};
    // Current phase of the animated panel. Exposed so a host can correlate a screenshot with what the
    // engine believed it was drawing, rather than guessing from the pixels alone.
    std::atomic<bool> uiPulseOn{false};
    // Presses and releases the UI actually routed to the panel. These are the numbers that prove the
    // routing half of the chain: a touch can reach WindowInputSnapshot and still hit nothing, so a
    // rising press count means the hit test and listener registration work too. Balanced counts also
    // show no press was left latched by a cancelled gesture.
    std::atomic<u64> pointerPresses{0};
    std::atomic<u64> pointerReleases{0};
    // Key presses the game observed as a bound action. Edge-counted, so this is presses rather than
    // frames held, and it is the number that proves the key path reaches game code -- not just the
    // input snapshot.
    std::atomic<u64> keyPresses{0};
    // Raised by the game when a key press should toggle the soft keyboard, cleared by the host once it
    // has acted. Needed because a phone has no hardware keys, so without this there is no way to reach
    // text input at all on a touch-only device.
    std::atomic<bool> softKeyboardToggleRequested{false};
    // Whether the focused TextEdit is currently drawing a preedit.
    //
    // This is the only signal that separates "the platform published composition stages" from "the UI
    // actually rendered a composing region". The backend's stage counters prove the first; a stage can
    // still be routed to a TextEdit that is not focused, in which case routeTextComposition returns
    // unconsumed and nothing is drawn -- indistinguishable from a working chain by counters alone.
    std::atomic<bool> uiPreeditActive{false};
    // Codepoints of committed text in the demo's TextEdit. Rising means a composing pass resolved into
    // real text rather than only painting a preedit that was later thrown away.
    std::atomic<u64> textEditCodepoints{0};
    // Raised by the host when the native window is gone, cleared by the game once it has unwound.
    //
    // Needed because backgrounding mid-touch delivers neither an Up nor a routed Cancel: the platform
    // layer clears its pointer slots, but a UI listener latch is game state and nothing unwinds it.
    // Measured on a device -- presses ran one ahead of releases and the panel stayed drawn as pressed
    // after the gesture was gone. The host is the only party that knows the window died, so it says so
    // rather than the game guessing from a timeout.
    std::atomic<bool> gestureStreamLost{false};
    std::atomic<bool> stateEntered{false};
    std::atomic<bool> applicationShutdown{false};
};

// The action the host binds arrow/D-pad/Enter to, so a key press has an observable effect. Declared
// here because the host owns the binding while the game owns the reaction, and both need the id.
inline constexpr InputActionId AndroidHighlightAction{1};

// The minimal game that exercises the real engine phases on Android.
//
// Deliberately not a sample copy: it draws nothing and owns no content, because the point is to prove
// that EngineHost's phase machinery -- fixed update, frame update, and input routing -- runs on a
// device, not to demonstrate gameplay. Rendering is still the clear colour the RenderDevice produces.
[[nodiscard]] std::unique_ptr<IGameApplication> createAndroidGameApplication(
    AndroidGameTelemetry& telemetry) noexcept;

} // namespace Tina::Platform::Android
