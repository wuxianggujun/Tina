#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/Input.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <span>
#include <string_view>

namespace Tina::Platform {

// Lock-free single-producer/single-consumer rings carrying UIKit events into the engine's owner
// thread, plus the UITouch identity mapping.
//
// Same shape as the Android bridge on purpose: UIKit and Android differ in how events arrive, not
// in what the engine needs from them, and a second queue design would be a second place for the
// memory ordering to be subtly wrong. The producer is the UIKit main thread, the consumer is
// whichever thread drives EngineHost::tick().
//
// Bounded and lossy by construction. A full ring drops the newest event and counts it rather than
// growing or blocking: growing breaks the fixed-capacity invariant, and blocking UIKit's main
// thread behind a stalled engine thread would freeze the app and get it killed by the watchdog.
enum class IosTouchPhase : u8 {
    // A finger touched down. Its pointer id becomes present.
    Began,
    // A finger moved while down.
    Moved,
    // A finger lifted normally. Its pointer id becomes absent.
    Ended,
    // UIKit took the touch away: an incoming call, a system gesture, the view leaving the
    // hierarchy. Distinct from Ended because the interaction did not complete, so downstream must
    // release capture without treating it as a tap.
    Cancelled,
};

struct IosTouchEvent final {
    IosTouchPhase phase = IosTouchPhase::Began;
    // Dense 0..PointerCapacity-1 slot, NOT a UITouch pointer value.
    //
    // UIKit identifies a touch by the address of a UITouch object and *reuses those objects*, which
    // is precisely how cocos2d-x stranded fingers: a stale map entry made the next finger at that
    // address permanently invisible (ADR 0032). Mapping therefore happens in IosTouchSlotTable and
    // never at a call site.
    u8 pointerSlot = 0;
    // Points, which is what UIKit reports from locationInView. The backend converts to
    // window-logical using the content scale it holds; converting in the ObjC layer would put the
    // scale factor in two places.
    float pointX = 0.0F;
    float pointY = 0.0F;
};

// Sized well above PointerCapacity times a few frames of movement, so a normal multi-touch gesture
// cannot fill it between polls.
inline constexpr usize IosTouchEventCapacity = 256;

enum class IosKeyAction : u8 {
    Down,
    Up,
};

struct IosKeyEvent final {
    IosKeyAction action = IosKeyAction::Down;
    // UIKeyboardHIDUsage, the raw USB HID usage code UIKey reports, passed through untranslated.
    //
    // Translation happens in C++ (iosKeyFromHidUsage) so the ObjC layer owns no mapping table.
    // docs/platform-input.md records cocos2d-x keeping two hand-written enums aligned by numeric
    // coincidence with no translation layer, so reordering either side silently misrouted events.
    i32 hidUsage = 0;
    // True when UIKit reports this as a hold repeat. Forwarded rather than dropped because held-key
    // navigation depends on repeats and KeyTransition can already express it.
    bool repeat = false;
};

inline constexpr usize IosKeyEventCapacity = 128;

// Longest single committed text a queue slot carries.
//
// UITextInput delivers whole strings: autocorrect, dictation, paste and candidate selection all
// arrive as one insertText: call, so this is not a per-keystroke bound. 256 bytes holds a long
// paste in any script. Producers with a larger commit must split it at codepoint boundaries and
// publish the slots as one batch.
inline constexpr usize IosTextCommitBytes = 256;

// One committed text event.
//
// Bytes are owned inline rather than referenced: an NSString's UTF-8 buffer is only valid for the
// duration of the ObjC call, while the consumer reads this on another thread, potentially frames
// later.
struct IosTextEvent final {
    // UTF-8, not NUL-terminated. byteCount is authoritative because the producer validated it.
    std::array<char, IosTextCommitBytes> utf8{};
    u16 byteCount = 0;
};

inline constexpr usize IosTextEventCapacity = 32;

// What UITextInput did to the marked (preedit) region.
//
// Only three inputs, because that is all the protocol offers: setMarkedText:selectedRange:
// replaces the region, unmarkText abandons it, insertText: commits. Mapping those onto Tina's four
// TextCompositionStage values is what IosCompositionSession does.
enum class IosCompositionAction : u8 {
    // setMarkedText:selectedRange:. An empty string means the user deleted the whole marked region,
    // which UIKit does not distinguish from abandoning it, so the session treats it as one.
    SetMarkedText,
    // unmarkText. The input system gave up the region without committing.
    Unmark,
    // insertText:, routed through this queue rather than the text queue.
    //
    // Here because ordering is the whole point: a commit that ends an active composition must
    // publish Ended *before* the committed text, and two independent rings cannot preserve that.
    // The text ring stays for commits that arrive with no marked text in flight.
    Commit,
};

// Longest preedit a slot carries. 512 rather than IosTextCommitBytes for two independent reasons: a
// Chinese/Japanese marked string is routinely longer than one commit, and
// UI::Detail::UIImeCompositionState::MaximumPreeditBytes is 512. The second is binding -- accepting
// more than the UI can hold would produce a preedit that routeTextComposition rejects with
// CapacityExceeded, failing the whole frame instead of this one event.
inline constexpr usize IosCompositionPreeditBytes = 512;

struct IosCompositionEvent final {
    IosCompositionAction action = IosCompositionAction::SetMarkedText;
    // UTF-8, not NUL-terminated, owned inline for the same reason IosTextEvent owns its bytes.
    std::array<char, IosCompositionPreeditBytes> utf8{};
    u16 byteCount = 0;
    // Caret within this text, in codepoints, already clamped to its codepoint count.
    //
    // Clamped by the producer because PlatformFrameBuilder rejects the whole payload when the cursor
    // runs past the preedit, and UITextInput's selectedRange is expressed in UTF-16 code units that
    // can legitimately sit past what Tina counts. An unclamped value costs the entire frame rather
    // than this one transition.
    u32 cursorCodepoint = 0;
};

inline constexpr usize IosCompositionEventCapacity = 32;

// Fills a composition event from UTF-16, which is what an NSString stores natively.
//
// UTF-16 rather than UTF-8 bytes so the cursor offset can be converted from the UTF-16 code-unit
// range UITextInput reports without the ObjC layer having to count codepoints. An empty preedit is
// valid here, unlike a commit: it is how UIKit says the marked region was emptied, which the
// session turns into a cancel.
[[nodiscard]] bool makeIosCompositionEventFromUtf16(std::u16string_view utf16, i32 cursorUtf16Offset,
                                                    IosCompositionAction action,
                                                    IosCompositionEvent& event) noexcept;

// Fills a text event from UTF-8 bytes, returning false when the input cannot be represented.
//
// Validation happens on the producer side because that is where an NSString is converted, so it is
// the only place that can report the failure to the caller that caused it. Rejects text that is
// empty, longer than IosTextCommitBytes, or not strict UTF-8 -- PlatformFrameBuilder would
// otherwise reject the whole frame rather than one bad transition.
[[nodiscard]] bool makeIosTextEvent(std::string_view utf8, IosTextEvent& event) noexcept;

// Same, from UTF-16. This is the overload the ObjC layer uses, because an NSString's own storage is
// UTF-16 and going through it avoids a second conversion the engine would then have to validate.
[[nodiscard]] bool makeIosTextEventFromUtf16(std::u16string_view utf16, IosTextEvent& event) noexcept;

// Splits one committed UTF-16 string into ordered composition-queue events without breaking a
// codepoint. Returns false for empty/invalid UTF-16, embedded NUL, or insufficient output capacity.
// The caller must publish the returned prefix with tryPushBatch(), so a full queue cannot expose
// only half of a paste or candidate selection.
[[nodiscard]] bool makeIosCommitEventsFromUtf16(std::u16string_view utf16,
                                                std::span<IosCompositionEvent> events,
                                                usize& eventCount) noexcept;

// Maps a UIKeyboardHIDUsage to the engine's Key.
//
// Returns Key::Unknown for anything unmapped, and callers drop those rather than guessing: an
// unmapped usage is a key Tina has no name for, and inventing one would route it to whatever
// enumerator happened to sit at that index. HID usages are a published USB standard, so they are
// spelled out in the implementation rather than pulled from a UIKit header this target cannot see.
[[nodiscard]] Key iosKeyFromHidUsage(i32 hidUsage) noexcept;

// Maps UIKit's reused UITouch identities onto Tina's dense 0..PointerCapacity-1 slots.
//
// Identity is the UITouch object address, passed across the boundary as an opaque integer. UIKit
// pools and reuses those objects, which is exactly why this cannot be a plain map with no eviction
// path: cocos2d-x had no else branch for an already-mapped identity and silently dropped the event,
// making every later finger at that address permanently invisible (ADR 0032).
//
// Owner-thread only, like everything else the backend touches.
class IosTouchSlotTable final {
  public:
    // No slot available. Returned rather than evicting a finger that is still down.
    static constexpr u8 InvalidSlot = 0xFFU;

    // Not defaulted: a zero-initialised table would claim every slot tracks identity 0. That is not
    // a valid UITouch address, but relying on that coincidence is how the Android table's first
    // version broke, so the sentinel is explicit here too.
    IosTouchSlotTable() noexcept;

    // Claims a slot for a Began. An identity that is somehow already mapped keeps its slot rather
    // than being dropped: a duplicated Began means the matching Ended was lost, and stranding the
    // finger is worse than restarting it.
    [[nodiscard]] u8 acquire(std::uintptr_t touchIdentity) noexcept;

    // Slot for an already-tracked identity, or InvalidSlot. A Moved/Ended for an untracked identity
    // is a lost Began, not something to invent a slot for.
    [[nodiscard]] u8 find(std::uintptr_t touchIdentity) const noexcept;

    // Releases the mapping so the slot can serve a later finger. Unknown identities are a no-op.
    void release(std::uintptr_t touchIdentity) noexcept;

    // Drops every mapping, for when the whole gesture stream is taken away (the app backgrounded,
    // the layer was released). Leaving entries behind is precisely the cocos failure mode, where a
    // backgrounded drag stranded its finger until process exit.
    void releaseAll() noexcept;

    [[nodiscard]] usize activeCount() const noexcept;

  private:
    // A flat array rather than a map: eight slots make a linear scan cheaper than hashing, and an
    // input path must not allocate. Sized from PointerCapacity so widening the engine's pointer
    // table cannot leave this silently narrower.
    static constexpr std::uintptr_t UnusedIdentity = 0;

    std::array<std::uintptr_t, PointerCapacity> touchIdentities_{};
};

// Templated over the event type so all four rings share one implementation. The interesting part is
// the memory ordering, and having it written four times is how the later copies end up subtly
// wrong.
template <typename Event, usize Capacity>
class IosEventQueue final {
  public:
    IosEventQueue() noexcept = default;

    IosEventQueue(const IosEventQueue&) = delete;
    IosEventQueue& operator=(const IosEventQueue&) = delete;
    IosEventQueue(IosEventQueue&&) = delete;
    IosEventQueue& operator=(IosEventQueue&&) = delete;

    // Producer side: call from UIKit's main thread only. Returns false when the ring is full,
    // having dropped the event and incremented the drop count.
    [[nodiscard]] bool tryPush(const Event& event) noexcept
    {
        // Both indices stay wrapped into [0, SlotCount), so they index slots directly. relaxed on
        // our own index (this thread is its only writer), acquire on the other thread's so slots it
        // has finished reading are visible as free.
        const u64 write = writeIndex_.load(std::memory_order_relaxed);
        const u64 read = readIndex_.load(std::memory_order_acquire);

        const u64 next = (write + 1) % SlotCount;
        // This is why one slot stays unused: it makes "next == read" mean full while "write == read"
        // means empty, so the two are distinguishable without a size counter needing its own
        // synchronisation.
        if (next == read)
        {
            droppedEventCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        slots_[write] = event;
        // Release publishes the slot write above before the consumer can observe the new index.
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    // Producer side: publishes all events atomically with respect to the consumer. Either the whole
    // batch fits or none of its slots become visible, which is what keeps a split text commit from
    // exposing a prefix and dropping the suffix.
    [[nodiscard]] bool tryPushBatch(std::span<const Event> events) noexcept
    {
        if (events.empty())
        {
            return true;
        }

        const u64 write = writeIndex_.load(std::memory_order_relaxed);
        const u64 read = readIndex_.load(std::memory_order_acquire);
        const u64 used = write >= read ? write - read : SlotCount - (read - write);
        const u64 available = Capacity - used;
        if (events.size() > available)
        {
            droppedEventCount_.fetch_add(static_cast<u64>(events.size()), std::memory_order_relaxed);
            return false;
        }

        u64 next = write;
        for (const Event& event : events)
        {
            slots_[next] = event;
            next = (next + 1) % SlotCount;
        }
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side: call from the engine owner thread only. Returns false when empty.
    [[nodiscard]] bool tryPop(Event& event) noexcept
    {
        const u64 read = readIndex_.load(std::memory_order_relaxed);
        const u64 write = writeIndex_.load(std::memory_order_acquire);
        if (read == write)
        {
            return false;
        }

        event = slots_[read];
        // Release so the producer cannot overwrite the slot until this read is visible as complete.
        readIndex_.store((read + 1) % SlotCount, std::memory_order_release);
        return true;
    }

    // Monotonic count of events the ring had no room for. Never reset, so a caller sampling it per
    // frame sees a total rather than a window and cannot miss a drop between samples.
    [[nodiscard]] u64 droppedEventCount() const noexcept
    {
        return droppedEventCount_.load(std::memory_order_relaxed);
    }

  private:
    static constexpr usize SlotCount = Capacity + 1;

    Event slots_[SlotCount]{};
    // Only the producer writes writeIndex_, only the consumer writes readIndex_. That
    // single-writer-per-index property is what makes this safe without a mutex, and why the
    // single-producer/single-consumer restriction is a hard requirement rather than a convention.
    // Separately aligned to avoid false sharing: both threads write these every event.
    alignas(64) std::atomic<u64> writeIndex_{0};
    alignas(64) std::atomic<u64> readIndex_{0};
    alignas(64) std::atomic<u64> droppedEventCount_{0};
};

using IosTouchEventQueue = IosEventQueue<IosTouchEvent, IosTouchEventCapacity>;
using IosKeyEventQueue = IosEventQueue<IosKeyEvent, IosKeyEventCapacity>;
using IosTextEventQueue = IosEventQueue<IosTextEvent, IosTextEventCapacity>;
using IosCompositionEventQueue = IosEventQueue<IosCompositionEvent, IosCompositionEventCapacity>;

} // namespace Tina::Platform
