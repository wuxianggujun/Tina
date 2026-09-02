#pragma once

#include <tina/platform/Input.hpp>

#include <array>

namespace Tina::Platform {

// Maps a browser touch identifier onto one of Tina's dense pointer slots.
//
// The DOM identifier is an arbitrary integer that is reused once a finger lifts, so it
// cannot index the pointer table directly. This is the same problem AndroidTouchSlotTable
// solves, and the shape is deliberately identical: cocos2d-x got it wrong by silently
// dropping a Down whose identifier was already mapped, which made every later finger at
// that identity permanently invisible (ADR 0032). Every path here has an explicit outcome.
//
// Slots are allocated from 0, so the first finger owns PrimaryPointerId exactly as it does
// on Android. Reserving slot 0 for the mouse instead would leave a touch-only device unable
// to fire any PointerButtonBinding action, because EngineConfig only accepts bindings on the
// primary pointer. Aliasing with the mouse is prevented by consuming the touch events, which
// stops the browser synthesising compatibility mouse events for them.
//
// Owner-thread only, like the rest of the backend.
class Html5TouchSlotTable final {
  public:
    // No slot available. Returned rather than evicting a finger that is still down.
    static constexpr PointerId InvalidSlot = ~PointerId{0};

    // Not defaulted: a zero-initialised table would claim every slot is tracking identifier
    // 0, which is a value the browser really does use.
    Html5TouchSlotTable() noexcept;

    // Claims a slot for a touchstart. An identifier that is already mapped keeps its slot: a
    // duplicated start means the matching end was lost, and restarting the finger is better
    // than stranding it for the rest of the gesture.
    [[nodiscard]] PointerId acquire(int identifier) noexcept;

    // Slot for an already-tracked identifier, or InvalidSlot. A move or end for an untracked
    // identifier is a lost start, not something to invent a slot for.
    [[nodiscard]] PointerId find(int identifier) const noexcept;

    void release(int identifier) noexcept;

    // Drops every mapping, for when the whole gesture stream is taken away (focus lost, queue
    // overflow). Leaving entries behind is exactly how a finger gets stranded.
    void releaseAll() noexcept;

    // Whether a touch currently owns this slot. Focus loss has to clear the touch slots
    // without disturbing the mouse, and the two share slot 0.
    [[nodiscard]] bool ownsSlot(PointerId slot) const noexcept;

    // Last published position for this slot. The browser reports no movement delta for a
    // touch point -- movementX/Y exist only for a pointer-locked mouse -- so the delta has to
    // be differenced against this.
    void setLastPosition(PointerId slot, double logicalX, double logicalY) noexcept;
    [[nodiscard]] bool lastPosition(PointerId slot, double& logicalX, double& logicalY) const noexcept;

  private:
    // Flat array, not a map: eight slots make a linear scan cheaper than hashing, and an
    // input path must not allocate. Sized from PointerCapacity so widening the engine's
    // pointer table cannot leave this silently narrower.
    static constexpr int UnusedIdentifier = -1;

    struct Entry final {
        int identifier = UnusedIdentifier;
        double lastLogicalX = 0.0;
        double lastLogicalY = 0.0;
    };

    std::array<Entry, PointerCapacity> entries_{};
};

} // namespace Tina::Platform
