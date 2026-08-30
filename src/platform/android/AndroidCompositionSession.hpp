#pragma once

// Maps Android's composing-text calls onto Tina's four-stage TextCompositionStage model.
//
// Private to the Android adapter, and shaped like Imm32CompositionSession for the same reason: the
// state machine returns a description of what to publish rather than appending to a frame builder, so
// the mapping can be tested without a window, a JVM or a PlatformFrame.
//
// The mapping is not one-to-one, which is why it needs a state machine rather than a switch at the
// call site. Android offers setComposingText and finishComposingText and has no cancel at all, while
// Tina distinguishes Started from Updated (needs history) and Ended from Cancelled (needs to know
// whether a commit is arriving). Neither distinction is derivable from a single call.

#include <tina/platform/Input.hpp>
#include <tina/platform/android/AndroidInputBridge.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace Tina::Platform::Detail {

// What one Android composition call turns into.
//
// Both fields are optional and independent: a commit that ends a composition produces a stage *and*
// text, a plain preedit update produces only a stage, and a commit with no composition in flight
// produces only text. Returning them together is what keeps their relative order fixed -- the stage
// is always published first.
struct AndroidCompositionOutcome final {
    std::optional<TextCompositionStage> stage{};
    // Preedit for the stage, borrowed from the session and valid until the next call on it. Empty for
    // Ended and Cancelled, which carry no preedit by contract.
    std::string_view preeditUtf8{};
    u32 cursorCodepoint = 0;
    // Committed text to publish as a TextInputTransition after the stage. Borrowed from the caller's
    // event, not the session.
    std::string_view committedUtf8{};
};

class AndroidCompositionSession final {
  public:
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::string_view preeditUtf8() const noexcept;
    [[nodiscard]] u32 cursorCodepoint() const noexcept { return active_ ? cursorCodepoint_ : 0U; }

    // Applies one queued event.
    //
    // Never fails: the producer already validated the bytes and clamped the cursor, and every state
    // combination has a defined outcome. Two of them are deliberately silent -- an empty
    // setComposingText or a finishComposingText with nothing in flight. Publishing a Cancelled there
    // would announce the end of a composition that never started, which downstream would have to
    // filter out anyway; IMEs send both routinely as they take and release the region.
    [[nodiscard]] AndroidCompositionOutcome apply(const AndroidCompositionEvent& event) noexcept;

    // The window went away, or the session is being torn down. Cancels an active composition so its
    // preedit cannot outlive the surface that was drawing it; nullopt when nothing was in flight.
    //
    // Needed because losing the window delivers no InputConnection call at all: without this the UI
    // keeps a preedit on screen that the IME has long forgotten, and the next composition would report
    // Updated for a session the consumer never saw start.
    [[nodiscard]] std::optional<AndroidCompositionOutcome> cancel() noexcept;

  private:
    void storePreedit(const AndroidCompositionEvent& event) noexcept;
    void clear() noexcept;

    std::array<char, AndroidCompositionPreeditBytes> preeditBytes_{};
    usize preeditSize_ = 0;
    u32 cursorCodepoint_ = 0;
    bool active_ = false;
};

} // namespace Tina::Platform::Detail
