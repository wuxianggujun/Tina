#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/platform/Input.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <span>
#include <string_view>

namespace Tina::Platform {

// Lock-free single-producer/single-consumer queue for touch events crossing from Android's UI
// thread into the engine's owner thread.
//
// Shape chosen over cocos2d-x's `runOnGLThread` marshalling, which docs/platform-input.md
// records as allocating a Runnable (and capturing a String) per event -- real GC pressure under
// an event flood. Tina's PlatformFrameBuilder is fixed-capacity and per-poll, so the natural fit
// is: the producer writes into a fixed ring, the consumer drains it once per poll.
//
// Bounded and lossy by construction. A full ring drops the newest event and counts it rather
// than growing or blocking: growing would break the fixed-capacity invariant, and blocking the
// UI thread on a stalled engine thread would freeze the whole app. Drops are visible via
// droppedEventCount() so a capacity that is genuinely too small is diagnosable instead of
// silently degrading.
enum class AndroidTouchAction : u8 {
    // A finger touched down. Its pointer id becomes present.
    Down,
    // A finger moved while down.
    Move,
    // A finger lifted normally. Its pointer id becomes absent.
    Up,
    // The gesture was taken away (ANativeWindow lost focus, a parent view intercepted, the
    // activity paused). Distinct from Up because the interaction did not complete: downstream
    // must release capture without treating it as a click.
    Cancel,
};

struct AndroidTouchEvent final {
    AndroidTouchAction action = AndroidTouchAction::Down;
    // Dense 0..PointerCapacity-1 slot, NOT Android's raw pointer id.
    //
    // Android reuses and sparsely allocates pointer ids, so the producer must map them into
    // dense slots. cocos2d-x got this shape right and the implementation wrong: its
    // handleTouchesBegin had no else branch for an id already in the map, so a stale entry made
    // the next finger at that address permanently invisible (ADR 0032). Mapping therefore lives
    // in one place -- AndroidTouchSlotTable -- rather than being open-coded per call site.
    u8 pointerSlot = 0;
    // Physical pixels, matching what Android's MotionEvent reports. The backend converts to
    // window-logical using the content scale it was created with; doing it here would require
    // the producer to know the density.
    float physicalX = 0.0F;
    float physicalY = 0.0F;
};

// Fixed capacity. Sized well above PointerCapacity (8 simultaneous fingers) times a few frames
// of movement, so a normal multi-touch gesture cannot fill it between polls.
inline constexpr usize AndroidTouchEventCapacity = 256;

enum class AndroidKeyAction : u8 {
    Down,
    Up,
};

struct AndroidKeyEvent final {
    AndroidKeyAction action = AndroidKeyAction::Down;
    // Android's raw KEYCODE_* value, passed through untranslated.
    //
    // Translation happens in C++ (androidKeyFromKeyCode) rather than on the Java side, because
    // docs/platform-input.md records cocos2d-x keeping two hand-written enums aligned by numeric
    // coincidence, with no translation layer and no assertion -- so reordering either side silently
    // misrouted every event. Keeping the raw code here means Java owns no mapping at all.
    i32 androidKeyCode = 0;
    // True when Android reports this as a repeat from key-hold. Forwarded rather than dropped because
    // text-entry style navigation depends on repeats, and the engine's KeyTransition can express it.
    bool repeat = false;
};

// Sized like the touch ring: a key flood is far sparser than pointer movement, so the same capacity
// is generous. Same bounded/lossy contract, and the same single-producer/single-consumer restriction.
inline constexpr usize AndroidKeyEventCapacity = 128;

// Longest single committed text a queue slot carries.
//
// Android's InputConnection.commitText delivers whole strings, not characters -- autocomplete, paste
// and IME conversion all arrive as one call, so this is not a per-keystroke bound. 256 bytes holds a
// long paste in any script. This is one slot's bound; producers that accept a larger commit must split
// it at codepoint boundaries and publish the resulting slots as one batch.
inline constexpr usize AndroidTextCommitBytes = 256;

// One committed text event.
//
// Bytes are owned inline rather than referenced, because a std::string_view would point at a Java
// string that is released the moment the JNI call returns -- the consumer reads this on another thread,
// potentially frames later. PlatformFrameBuilder copies borrowed text into its own arena when the
// transition is appended, so this storage only has to survive the queue hop.
struct AndroidTextEvent final {
    // UTF-8, not NUL-terminated. Length is authoritative because the producer validates it.
    std::array<char, AndroidTextCommitBytes> utf8{};
    u16 byteCount = 0;
};

// Text arrives far more slowly than pointer movement, and each slot is 256 bytes, so a smaller ring is
// the right trade: 32 pending commits is already several seconds of fast typing.
inline constexpr usize AndroidTextEventCapacity = 32;

// What the IME did to the composing (preedit) region.
//
// Only two inputs, because that is all InputConnection offers: setComposingText replaces the region,
// finishComposingText abandons it. Android has no "cancel" call at all -- mapping those two onto Tina's
// four TextCompositionStage values is what AndroidCompositionSession does, and why the mapping lives in
// one state machine rather than at each call site.
enum class AndroidCompositionAction : u8 {
    // setComposingText. An empty string means the user deleted the whole preedit, which Android does
    // not distinguish from abandoning it -- so the session treats it as one.
    SetText,
    // finishComposingText. The IME gave up the region without committing.
    Finish,
    // commitText, routed through this queue rather than the text queue.
    //
    // Here because ordering is the whole point: a commit that ends an active composition must publish
    // Ended *before* the committed text, and two independent rings cannot preserve that relationship.
    // The text ring stays for commits that arrive with no composition in flight.
    Commit,
};

// Longest preedit a slot carries.
//
// 512 rather than AndroidTextCommitBytes (256) for two independent reasons: a Chinese/Japanese
// composing string is routinely longer than one commit, and UI::Detail::UIImeCompositionState::
// MaximumPreeditBytes is 512. The second one is binding -- accepting more than the UI can hold would
// produce a preedit the backend publishes and routeTextComposition then rejects with CapacityExceeded,
// failing the frame instead of this one event.
inline constexpr usize AndroidCompositionPreeditBytes = 512;

struct AndroidCompositionEvent final {
    AndroidCompositionAction action = AndroidCompositionAction::SetText;
    // UTF-8, not NUL-terminated, owned inline for the same reason AndroidTextEvent owns its bytes: a
    // view would point at a Java string freed when the JNI call returns.
    std::array<char, AndroidCompositionPreeditBytes> utf8{};
    u16 byteCount = 0;
    // Caret within this text, in codepoints, already clamped to its codepoint count.
    //
    // Clamped by the producer because PlatformFrameBuilder rejects the whole payload when the cursor
    // runs past the preedit (PlatformFrame.hpp copyBorrowedTextIntoArena), and Android's
    // newCursorPosition is a relative offset that can legitimately be negative or out of range. An
    // unclamped value would therefore cost the entire frame rather than this one transition.
    u32 cursorCodepoint = 0;
};

// A composing pass is a handful of events per character typed, and each slot is 512 bytes. 32 is
// already several seconds of continuous composition, and unlike touch there is no flood case.
inline constexpr usize AndroidCompositionEventCapacity = 32;

// Fills a composition event from UTF-16, which is what JNI's GetStringChars gives.
//
// UTF-16 rather than modified UTF-8 for exactly the reason committed text uses it: GetStringUTFChars
// splits non-BMP characters into CESU-8 surrogate pairs that strict validation rejects. A preedit can
// contain emoji too -- an IME predicting one shows it in the composing region first.
//
// An empty preedit is valid here, unlike a commit: it is how Android says the composing region was
// emptied, which the session turns into a cancel. `cursorUtf16Offset` is in UTF-16 code units (what
// Android reports) and is converted and clamped to codepoints.
[[nodiscard]] bool makeAndroidCompositionEventFromUtf16(std::u16string_view utf16, i32 cursorUtf16Offset,
                                                       AndroidCompositionAction action,
                                                       AndroidCompositionEvent& event) noexcept;

// Fills a text event from UTF-8 bytes, returning false when the input cannot be represented.
//
// Validation happens here, on the producer side, rather than at the consumer: the JNI layer is where a
// Java string is converted, so it is the only place that can report the failure to the caller that
// caused it. Rejects text that is empty, longer than AndroidTextCommitBytes, or not strict UTF-8 -- the
// engine requires strict UTF-8 without NUL, and PlatformFrameBuilder would reject the whole frame
// rather than one bad transition.
[[nodiscard]] bool makeAndroidTextEvent(std::string_view utf8, AndroidTextEvent& event) noexcept;

// Same, from UTF-16 -- which is what JNI's GetStringChars gives and what Java actually stores.
//
// This is the overload the JNI layer uses. GetStringUTFChars would be more convenient but returns
// *modified* UTF-8, where non-BMP characters arrive as CESU-8 surrogate pairs: an emoji becomes two
// invalid three-byte sequences that strict validation rejects, losing the character silently.
// Converting from UTF-16 is what lets astral-plane text through intact.
[[nodiscard]] bool makeAndroidTextEventFromUtf16(std::u16string_view utf16, AndroidTextEvent& event) noexcept;

// Splits one committed UTF-16 string into ordered composition-queue events without breaking a Unicode
// codepoint. Returns false for empty/invalid UTF-16, embedded NUL, or insufficient output capacity.
// The caller must publish the returned prefix with tryPushBatch(), so a full queue cannot expose only
// half of a paste or IME conversion.
[[nodiscard]] bool makeAndroidCommitEventsFromUtf16(std::u16string_view utf16,
                                                    std::span<AndroidCompositionEvent> events,
                                                    usize& eventCount) noexcept;

// Maps an Android KEYCODE_* to the engine's Key.
//
// Returns Key::Unknown for anything unmapped, and callers drop those rather than guessing: an
// unmapped code is a key Tina has no name for, and inventing one would route it to whatever
// enumerator happened to sit at that index.
//
// Deliberately not a table indexed by key code -- Android's codes are sparse and reach past 300, so a
// dense table would be mostly padding and would silently shift if a code were inserted.
[[nodiscard]] Key androidKeyFromKeyCode(i32 androidKeyCode) noexcept;

// Maps Android's sparse, reused pointer ids onto Tina's dense 0..PointerCapacity-1 slots.
//
// This exists because cocos2d-x got the shape right and the implementation wrong (ADR 0032):
// its handleTouchesBegin had no else branch when an id was already mapped, so it silently
// dropped the event -- and since UIKit/Android reuse pointer identities, one stale entry made
// every future finger at that identity permanently invisible. The bug is unfixable at the call
// site, so all mapping goes through this one type and every path has an explicit outcome.
//
// Owner-thread only, like everything else the backend touches.
class AndroidTouchSlotTable final {
  public:
    // No slot available. Returned rather than silently reusing one, because reuse is exactly how
    // two fingers end up sharing state.
    static constexpr u8 InvalidSlot = 0xFFU;

    // Not defaulted: zero-initialising the table would mark every slot as tracking Android
    // pointer id 0, which is a perfectly valid id -- the first finger down would collide with all
    // eight slots at once.
    AndroidTouchSlotTable() noexcept;

    // Claims a slot for a down event. An id that is somehow already mapped keeps its slot rather
    // than being dropped: a duplicated Down means the previous Up was lost, and stranding the
    // finger is worse than reusing its slot.
    [[nodiscard]] u8 acquire(i32 androidPointerId) noexcept;

    // Slot for an already-tracked id, or InvalidSlot. A Move/Up for an untracked id is a lost
    // Down, not something to invent a slot for.
    [[nodiscard]] u8 find(i32 androidPointerId) const noexcept;

    // Releases the mapping so the slot can serve a later finger. Unknown ids are a no-op.
    void release(i32 androidPointerId) noexcept;

    // Drops every mapping. Used when the whole gesture stream is taken away (activity paused,
    // window destroyed): leaving entries behind is precisely the cocos failure mode, where a
    // backgrounded drag stranded its finger until process exit.
    void releaseAll() noexcept;

    [[nodiscard]] usize activeCount() const noexcept;

  private:
    // A flat array rather than a map: the slot count is 8, so a linear scan beats hashing and
    // needs no allocation on an input path. Sized from PointerCapacity rather than a literal, so
    // widening the engine's pointer table cannot leave this silently narrower.
    static constexpr i32 UnusedPointerId = -1;

    std::array<i32, PointerCapacity> androidPointerIds_{};
};

// Templated over the event type so touch and key rings share one implementation. The interesting part
// here is the memory ordering, and having it written twice is how the second copy ends up subtly
// wrong -- so the header carries the definition rather than each ring getting its own.
template <typename Event, usize Capacity>
class AndroidEventQueue final {
  public:
    AndroidEventQueue() noexcept = default;

    AndroidEventQueue(const AndroidEventQueue&) = delete;
    AndroidEventQueue& operator=(const AndroidEventQueue&) = delete;
    AndroidEventQueue(AndroidEventQueue&&) = delete;
    AndroidEventQueue& operator=(AndroidEventQueue&&) = delete;

    // Producer side: call from Android's UI thread only. Returns false when the ring is full, having
    // dropped the event and incremented the drop count.
    [[nodiscard]] bool tryPush(const Event& event) noexcept
    {
        // Both indices are kept already wrapped into [0, SlotCount), so they index slots directly.
        // relaxed on our own index (this thread is its only writer), acquire on the other thread's so
        // slots it has finished reading are visible as free.
        const u64 write = writeIndex_.load(std::memory_order_relaxed);
        const u64 read = readIndex_.load(std::memory_order_acquire);

        const u64 next = (write + 1) % SlotCount;
        // This is why one slot stays unused: it makes "next == read" mean full while "write == read"
        // means empty, so the two states are distinguishable without a separate size counter that
        // would need its own synchronisation.
        if (next == read)
        {
            // Full: drop the newest and count it. Growing would break the fixed-capacity invariant,
            // and blocking here would freeze Android's UI thread behind a stalled engine thread.
            droppedEventCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        slots_[write] = event;
        // Release publishes the slot write above before the consumer can observe the new index.
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    // Producer side: publishes all events atomically with respect to the consumer. Either the whole
    // batch fits, or none of its slots become visible. This matters for split text commits: exposing a
    // prefix and dropping the suffix would silently corrupt pasted text.
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
    // Only the producer writes writeIndex_, only the consumer writes readIndex_. That single-
    // writer-per-index property is what makes this safe without a mutex, and why the
    // single-producer/single-consumer restriction above is a hard requirement rather than a
    // convention. Both are atomic because each is *read* by the other thread; the release/acquire
    // pairing on them is also what publishes the slot contents.
    //
    // Separately aligned to avoid false sharing: the two threads write these every event, and
    // sharing a cache line would bounce it between cores for no reason.
    alignas(64) std::atomic<u64> writeIndex_{0};
    alignas(64) std::atomic<u64> readIndex_{0};
    alignas(64) std::atomic<u64> droppedEventCount_{0};
};

using AndroidTouchEventQueue = AndroidEventQueue<AndroidTouchEvent, AndroidTouchEventCapacity>;
using AndroidKeyEventQueue = AndroidEventQueue<AndroidKeyEvent, AndroidKeyEventCapacity>;
using AndroidTextEventQueue = AndroidEventQueue<AndroidTextEvent, AndroidTextEventCapacity>;
using AndroidCompositionEventQueue =
    AndroidEventQueue<AndroidCompositionEvent, AndroidCompositionEventCapacity>;

} // namespace Tina::Platform
