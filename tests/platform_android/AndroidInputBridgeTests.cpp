#include <tina/platform/android/AndroidInputBridge.hpp>

#include <tina/core/text/Utf8.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Tina::Platform {
namespace {

// Android pointer id 0 is valid and common (the first finger). A zero-initialised table would
// claim every slot already tracks it, so the very first Down would collide eight ways.
TEST(AndroidTouchSlotTableTest, ZeroIsATrackableAndroidPointerId)
{
    AndroidTouchSlotTable table;
    EXPECT_EQ(table.activeCount(), 0U);
    EXPECT_EQ(table.find(0), AndroidTouchSlotTable::InvalidSlot);

    const u8 slot = table.acquire(0);
    ASSERT_NE(slot, AndroidTouchSlotTable::InvalidSlot);
    EXPECT_EQ(table.find(0), slot);
    EXPECT_EQ(table.activeCount(), 1U);
}

TEST(AndroidTouchSlotTableTest, AssignsDenseSlotsForSparseAndroidIds)
{
    AndroidTouchSlotTable table;
    // Android ids are sparse and arbitrary; engine slots must come out dense.
    const u8 first = table.acquire(7);
    const u8 second = table.acquire(3);
    const u8 third = table.acquire(91);
    EXPECT_EQ(first, 0U);
    EXPECT_EQ(second, 1U);
    EXPECT_EQ(third, 2U);
    EXPECT_EQ(table.activeCount(), 3U);
}

// The exact cocos2d-x defect ADR 0032 cites: its handleTouchesBegin had no else branch for an id
// already in the map, so it dropped the event -- and because pointer identities get reused, one
// stale entry made every future finger at that identity permanently invisible.
TEST(AndroidTouchSlotTableTest, ADuplicateDownKeepsTheSlotInsteadOfStrandingTheFinger)
{
    AndroidTouchSlotTable table;
    const u8 first = table.acquire(5);
    ASSERT_NE(first, AndroidTouchSlotTable::InvalidSlot);

    // A second Down for a live id means the matching Up was lost. Reusing the slot at worst
    // restarts the finger; returning InvalidSlot would make it invisible for the whole gesture.
    const u8 again = table.acquire(5);
    EXPECT_EQ(again, first);
    EXPECT_EQ(table.activeCount(), 1U) << "a duplicate Down must not consume a second slot";
}

// Reuse is the norm on Android, so a released slot must be genuinely reusable.
TEST(AndroidTouchSlotTableTest, AReleasedSlotServesTheNextFinger)
{
    AndroidTouchSlotTable table;
    const u8 first = table.acquire(11);
    table.release(11);
    EXPECT_EQ(table.activeCount(), 0U);
    EXPECT_EQ(table.find(11), AndroidTouchSlotTable::InvalidSlot);

    const u8 reused = table.acquire(12);
    EXPECT_EQ(reused, first);
}

TEST(AndroidTouchSlotTableTest, RejectsMoreSimultaneousFingersThanCapacity)
{
    AndroidTouchSlotTable table;
    for (usize index = 0; index < PointerCapacity; ++index)
    {
        ASSERT_NE(table.acquire(static_cast<i32>(100 + index)), AndroidTouchSlotTable::InvalidSlot);
    }
    EXPECT_EQ(table.activeCount(), PointerCapacity);

    // Refused rather than evicting a finger that is still down: eviction is how two fingers end up
    // sharing one slot's state.
    EXPECT_EQ(table.acquire(999), AndroidTouchSlotTable::InvalidSlot);
    EXPECT_EQ(table.activeCount(), PointerCapacity);
}

// cocos2d-x had no "release everything" hook on backgrounding, so a drag interrupted by a task
// switch stranded its finger until process exit.
TEST(AndroidTouchSlotTableTest, ReleaseAllClearsEveryMapping)
{
    AndroidTouchSlotTable table;
    (void)table.acquire(1);
    (void)table.acquire(2);
    (void)table.acquire(3);
    ASSERT_EQ(table.activeCount(), 3U);

    table.releaseAll();
    EXPECT_EQ(table.activeCount(), 0U);
    EXPECT_EQ(table.find(2), AndroidTouchSlotTable::InvalidSlot);
    // And the table is usable afterwards, from slot zero again.
    EXPECT_EQ(table.acquire(4), 0U);
}

TEST(AndroidTouchSlotTableTest, RejectsNegativeAndroidIds)
{
    AndroidTouchSlotTable table;
    EXPECT_EQ(table.acquire(-1), AndroidTouchSlotTable::InvalidSlot);
    EXPECT_EQ(table.find(-1), AndroidTouchSlotTable::InvalidSlot);
    EXPECT_EQ(table.activeCount(), 0U);
}

// Android KEYCODE_* values, restated here rather than shared with the implementation on purpose: a
// shared constant would make the test agree with the code by construction and prove nothing. These
// come from Android's public documentation.
constexpr i32 KeycodeBack = 4;
constexpr i32 KeycodeDpadUp = 19;
constexpr i32 KeycodeDpadCenter = 23;
constexpr i32 KeycodeA = 29;
constexpr i32 KeycodeZ = 54;
constexpr i32 Keycode0 = 7;
constexpr i32 Keycode9 = 16;
constexpr i32 KeycodeEnter = 66;
constexpr i32 KeycodeDel = 67;
constexpr i32 KeycodeForwardDel = 112;
constexpr i32 KeycodeEscape = 111;
constexpr i32 KeycodeF1 = 131;
constexpr i32 KeycodeF12 = 142;
constexpr i32 KeycodeVolumeUp = 24;

// The letter and digit runs are mapped as ranges, so the endpoints are what a transcription error would
// break. Checking both ends plus one interior value catches an off-by-one in either direction.
TEST(AndroidKeyMappingTest, MapsContiguousLetterDigitAndFunctionRuns)
{
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeA), Key::A);
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeA + 1), Key::B);
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeZ), Key::Z);

    EXPECT_EQ(androidKeyFromKeyCode(Keycode0), Key::Digit0);
    EXPECT_EQ(androidKeyFromKeyCode(Keycode9), Key::Digit9);

    EXPECT_EQ(androidKeyFromKeyCode(KeycodeF1), Key::F1);
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeF12), Key::F12);
}

// DEL is backspace and FORWARD_DEL is forward delete. Swapping them is the classic slip, and it would
// make text editing delete the wrong side of the caret -- which no compiler or type checker catches.
TEST(AndroidKeyMappingTest, DoesNotSwapDeleteAndBackspace)
{
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeDel), Key::Backspace);
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeForwardDel), Key::Delete);
}

// Back and DpadCenter are deliberately folded onto existing keys rather than getting mobile-only
// enumerators, so every consumer handles one spelling of each intent.
TEST(AndroidKeyMappingTest, FoldsMobileKeysOntoTheirDesktopEquivalents)
{
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeBack), Key::Escape)
        << "Back is Android's cancel gesture, which is what Escape means";
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeEscape), Key::Escape);
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeDpadCenter), Key::Enter)
        << "DpadCenter is the confirm button on a D-pad or remote";
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeEnter), Key::Enter);
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeDpadUp), Key::Up);
}

// Unmapped codes must report Unknown so the caller can drop them. Returning a guess would route an
// unrecognised key to whatever enumerator happened to sit at that index.
TEST(AndroidKeyMappingTest, ReportsUnknownForUnmappedCodes)
{
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeVolumeUp), Key::Unknown)
        << "volume keys belong to the system, not the engine";
    EXPECT_EQ(androidKeyFromKeyCode(0), Key::Unknown);
    EXPECT_EQ(androidKeyFromKeyCode(-1), Key::Unknown);
    EXPECT_EQ(androidKeyFromKeyCode(99999), Key::Unknown);
}

// The ranges must not leak past their endpoints: the code just after Z is not a letter, and treating it
// as one would silently produce an out-of-range Key.
TEST(AndroidKeyMappingTest, RangesDoNotOverrunTheirEndpoints)
{
    EXPECT_NE(androidKeyFromKeyCode(KeycodeZ + 1), Key::Z);
    EXPECT_NE(androidKeyFromKeyCode(KeycodeA - 1), Key::A);
    EXPECT_NE(androidKeyFromKeyCode(Keycode9 + 1), Key::Digit9);
    EXPECT_EQ(androidKeyFromKeyCode(KeycodeF12 + 1), Key::Unknown);
}

// Text is validated on the producer side, because the JNI layer is where a Java string is converted and
// so the only place that can report the failure to whoever caused it.
TEST(AndroidTextEventTest, AcceptsValidUtf8AndOwnsItsBytes)
{
    AndroidTextEvent event{};
    ASSERT_TRUE(makeAndroidTextEvent("hello", event));
    EXPECT_EQ(event.byteCount, 5U);
    EXPECT_EQ(std::string_view(event.utf8.data(), event.byteCount), "hello");

    // Multi-byte sequences are counted in bytes, not characters: the engine's transition carries bytes.
    ASSERT_TRUE(makeAndroidTextEvent("中文", event));
    EXPECT_EQ(event.byteCount, 6U);
    EXPECT_EQ(std::string_view(event.utf8.data(), event.byteCount), "中文");
}

// PlatformFrameBuilder applies the same strict-UTF-8 rule when the transition is appended, and it
// rejects the *whole frame* rather than one transition. Catching it here keeps one bad commit from
// costing every other input in that frame.
TEST(AndroidTextEventTest, RejectsWhatTheFrameBuilderWouldReject)
{
    AndroidTextEvent event{};
    EXPECT_FALSE(makeAndroidTextEvent("", event)) << "an empty commit carries no information";

    // Embedded NUL: legal in a Java string, rejected by the engine's strict validator.
    EXPECT_FALSE(makeAndroidTextEvent(std::string_view{"a\0b", 3}, event));

    // Truncated and overlong sequences, which is what modified UTF-8 and CESU-8 produce.
    EXPECT_FALSE(makeAndroidTextEvent("\xE4\xB8", event)) << "truncated multi-byte sequence";
    EXPECT_FALSE(makeAndroidTextEvent("\xC0\x80", event)) << "overlong encoding of NUL";
    EXPECT_FALSE(makeAndroidTextEvent("\xFF", event)) << "not a valid lead byte";
}

// One slot is a fixed size, so the boundary has to be usable and the byte past it rejected.
TEST(AndroidTextEventTest, TheAdvertisedCapacityIsUsableAndTheNextByteIsNot)
{
    AndroidTextEvent event{};
    const std::string exact(AndroidTextCommitBytes, 'x');
    EXPECT_TRUE(makeAndroidTextEvent(exact, event));
    EXPECT_EQ(event.byteCount, AndroidTextCommitBytes);

    const std::string tooLong(AndroidTextCommitBytes + 1, 'x');
    EXPECT_FALSE(makeAndroidTextEvent(tooLong, event));
}

// The UTF-16 entry point is what the JNI layer actually uses, because GetStringUTFChars returns
// modified UTF-8 where an emoji arrives as two invalid CESU-8 sequences and is silently lost.
TEST(AndroidTextEventTest, ConvertsUtf16IncludingEmoji)
{
    AndroidTextEvent event{};
    ASSERT_TRUE(makeAndroidTextEventFromUtf16(std::u16string{u'h', u'i'}, event));
    EXPECT_EQ(std::string_view(event.utf8.data(), event.byteCount), "hi");

    // U+1F600 as the surrogate pair Java stores. One four-byte sequence, not two three-byte ones.
    ASSERT_TRUE(makeAndroidTextEventFromUtf16(std::u16string{u'\xD83D', u'\xDE00'}, event));
    EXPECT_EQ(event.byteCount, 4U);
    EXPECT_EQ(std::string_view(event.utf8.data(), event.byteCount), "\xF0\x9F\x98\x80");

    // And the result must pass the same validator the frame builder applies.
    EXPECT_TRUE(Core::isStrictUtf8WithoutNul(std::string_view(event.utf8.data(), event.byteCount)));
}

TEST(AndroidTextEventTest, RejectsUtf16ThatCannotBeRepresented)
{
    AndroidTextEvent event{};
    EXPECT_FALSE(makeAndroidTextEventFromUtf16(std::u16string{}, event)) << "empty carries no information";
    EXPECT_FALSE(makeAndroidTextEventFromUtf16(std::u16string{u'\xD83D'}, event)) << "unpaired surrogate";
    EXPECT_FALSE(makeAndroidTextEventFromUtf16(std::u16string{u'a', u'\0'}, event)) << "embedded NUL";

    // One slot holds AndroidTextCommitBytes; a longer commit must fail rather than truncate.
    const std::u16string tooLong(AndroidTextCommitBytes + 1, u'x');
    EXPECT_FALSE(makeAndroidTextEventFromUtf16(tooLong, event));
}

// --- Composition events ---

// An empty preedit is meaningful, unlike an empty commit: it is how Android says the composing region
// was emptied, which the session turns into a cancel. Rejecting it here would make "the user deleted
// their pinyin" indistinguishable from "the conversion failed".
TEST(AndroidCompositionEventTest, AcceptsAnEmptyPreeditUnlikeACommit)
{
    AndroidCompositionEvent event{};
    ASSERT_TRUE(makeAndroidCompositionEventFromUtf16({}, 0, AndroidCompositionAction::SetText, event));
    EXPECT_EQ(event.byteCount, 0U);
    EXPECT_EQ(event.cursorCodepoint, 0U);
    EXPECT_EQ(event.action, AndroidCompositionAction::SetText);
}

// The cursor arrives in UTF-16 code units and must be published in codepoints, because
// PlatformFrameBuilder validates it against the preedit's codepoint count -- and the two diverge the
// moment an emoji is in the composing region.
TEST(AndroidCompositionEventTest, ConvertsTheCursorFromUtf16UnitsToCodepoints)
{
    AndroidCompositionEvent event{};
    // Surrogate pair then 'a': 3 UTF-16 units, 2 codepoints.
    const std::u16string text{u'\xD83D', u'\xDE00', u'a'};
    ASSERT_TRUE(makeAndroidCompositionEventFromUtf16(text, 3, AndroidCompositionAction::SetText, event));
    EXPECT_EQ(event.cursorCodepoint, 2U);

    // A cursor inside the pair counts the pair as one, not zero: it is one character on screen.
    ASSERT_TRUE(makeAndroidCompositionEventFromUtf16(text, 1, AndroidCompositionAction::SetText, event));
    EXPECT_EQ(event.cursorCodepoint, 1U);
}

// Clamped rather than rejected, because IMEs routinely pass 1 to mean "after the text" regardless of its
// length, and negative offsets are legal in Android's API. Rejecting would drop legitimate preedit; an
// unclamped value would cost the entire frame, since the frame builder rejects the whole payload.
TEST(AndroidCompositionEventTest, ClampsAnOutOfRangeCursorRatherThanRejecting)
{
    AndroidCompositionEvent event{};
    ASSERT_TRUE(makeAndroidCompositionEventFromUtf16(std::u16string{u"ni"}, 99,
                                                     AndroidCompositionAction::SetText, event));
    EXPECT_EQ(event.cursorCodepoint, 2U) << "clamped to the codepoint count";

    ASSERT_TRUE(makeAndroidCompositionEventFromUtf16(std::u16string{u"ni"}, -5,
                                                     AndroidCompositionAction::SetText, event));
    EXPECT_EQ(event.cursorCodepoint, 0U) << "a negative offset is the start, not a huge unsigned value";
}

// The preedit slot is deliberately larger than a commit slot, and it has to match the UI's own limit:
// accepting more than UIImeCompositionState::MaximumPreeditBytes would publish a preedit that
// routeTextComposition then rejects with CapacityExceeded, failing the frame rather than this one event.
TEST(AndroidCompositionEventTest, TheAdvertisedPreeditCapacityIsUsableAndTheNextByteIsNot)
{
    static_assert(AndroidCompositionPreeditBytes == 512,
                  "must match UI::Detail::UIImeCompositionState::MaximumPreeditBytes");

    AndroidCompositionEvent event{};
    const std::u16string exact(AndroidCompositionPreeditBytes, u'x');
    EXPECT_TRUE(makeAndroidCompositionEventFromUtf16(exact, 0, AndroidCompositionAction::SetText, event));
    EXPECT_EQ(event.byteCount, AndroidCompositionPreeditBytes);

    const std::u16string tooLong(AndroidCompositionPreeditBytes + 1, u'x');
    EXPECT_FALSE(makeAndroidCompositionEventFromUtf16(tooLong, 0, AndroidCompositionAction::SetText, event))
        << "overflow must be rejected, never truncated: half a multi-byte character is invalid UTF-8";
}

TEST(AndroidCompositionEventTest, RejectsPreeditThatCannotBeRepresented)
{
    AndroidCompositionEvent event{};
    EXPECT_FALSE(makeAndroidCompositionEventFromUtf16(std::u16string{u'\xD83D'}, 0,
                                                      AndroidCompositionAction::SetText, event))
        << "unpaired surrogate -- exactly what CESU-8 would have produced";
    EXPECT_FALSE(makeAndroidCompositionEventFromUtf16(std::u16string{u'a', u'\0'}, 0,
                                                      AndroidCompositionAction::SetText, event))
        << "embedded NUL";
}

TEST(AndroidCompositionEventQueueTest, SharesTheBoundedLossyContract)
{
    AndroidCompositionEventQueue queue;
    AndroidCompositionEvent event{};
    EXPECT_FALSE(queue.tryPop(event));

    AndroidCompositionEvent preedit{};
    ASSERT_TRUE(makeAndroidCompositionEventFromUtf16(std::u16string{u"ni"}, 2,
                                                     AndroidCompositionAction::SetText, preedit));
    for (usize index = 0; index < AndroidCompositionEventCapacity; ++index)
    {
        ASSERT_TRUE(queue.tryPush(preedit)) << "the advertised capacity must be usable in full";
    }
    EXPECT_FALSE(queue.tryPush(preedit));
    EXPECT_EQ(queue.droppedEventCount(), 1U);

    ASSERT_TRUE(queue.tryPop(event));
    EXPECT_EQ(std::string_view(event.utf8.data(), event.byteCount), "ni");
    EXPECT_EQ(event.cursorCodepoint, 2U) << "the cursor survives the queue hop with the bytes";
}

TEST(AndroidTextEventQueueTest, SharesTheBoundedLossyContract)
{
    AndroidTextEventQueue queue;
    AndroidTextEvent event{};
    EXPECT_FALSE(queue.tryPop(event));

    AndroidTextEvent commit{};
    ASSERT_TRUE(makeAndroidTextEvent("a", commit));
    for (usize index = 0; index < AndroidTextEventCapacity; ++index)
    {
        ASSERT_TRUE(queue.tryPush(commit)) << "the advertised capacity must be usable in full";
    }
    EXPECT_FALSE(queue.tryPush(commit));
    EXPECT_EQ(queue.droppedEventCount(), 1U);

    // The bytes survive the queue hop, which is the whole reason the event owns them rather than
    // referencing a Java string that dies when the JNI call returns.
    ASSERT_TRUE(queue.tryPop(event));
    EXPECT_EQ(std::string_view(event.utf8.data(), event.byteCount), "a");
}

TEST(AndroidKeyEventQueueTest, SharesTheBoundedLossyContractWithTouch)
{
    AndroidKeyEventQueue queue;
    AndroidKeyEvent event{};
    EXPECT_FALSE(queue.tryPop(event));

    for (usize index = 0; index < AndroidKeyEventCapacity; ++index)
    {
        ASSERT_TRUE(queue.tryPush(AndroidKeyEvent{
            .action = AndroidKeyAction::Down, .androidKeyCode = static_cast<i32>(index), .repeat = false}))
            << "the advertised capacity must be usable in full, at index " << index;
    }
    EXPECT_EQ(queue.droppedEventCount(), 0U);

    EXPECT_FALSE(queue.tryPush(AndroidKeyEvent{.androidKeyCode = 999}));
    EXPECT_EQ(queue.droppedEventCount(), 1U);

    // FIFO, and the repeat flag survives the round trip.
    ASSERT_TRUE(queue.tryPop(event));
    EXPECT_EQ(event.androidKeyCode, 0);
    EXPECT_FALSE(event.repeat);
}

[[nodiscard]] AndroidTouchEvent downAt(u8 slot, float x, float y) noexcept
{
    return AndroidTouchEvent{
        .action = AndroidTouchAction::Down, .pointerSlot = slot, .physicalX = x, .physicalY = y};
}

TEST(AndroidTouchEventQueueTest, PopsInPushOrderAndReportsEmpty)
{
    AndroidTouchEventQueue queue;
    AndroidTouchEvent event{};
    EXPECT_FALSE(queue.tryPop(event)) << "an empty queue must report empty, not a stale slot";

    ASSERT_TRUE(queue.tryPush(downAt(0, 10.0F, 20.0F)));
    ASSERT_TRUE(queue.tryPush(downAt(1, 30.0F, 40.0F)));

    ASSERT_TRUE(queue.tryPop(event));
    EXPECT_EQ(event.pointerSlot, 0U);
    EXPECT_FLOAT_EQ(event.physicalX, 10.0F);
    ASSERT_TRUE(queue.tryPop(event));
    EXPECT_EQ(event.pointerSlot, 1U);
    EXPECT_FALSE(queue.tryPop(event));
}

// Bounded and lossy on purpose: growing would break the fixed-capacity invariant, and blocking
// would freeze Android's UI thread behind a stalled engine thread. Drops must be counted so an
// undersized capacity is diagnosable rather than silently degrading.
TEST(AndroidTouchEventQueueTest, DropsAndCountsWhenFull)
{
    AndroidTouchEventQueue queue;
    for (usize index = 0; index < AndroidTouchEventCapacity; ++index)
    {
        ASSERT_TRUE(queue.tryPush(downAt(0, static_cast<float>(index), 0.0F)))
            << "the advertised capacity must be usable in full, at index " << index;
    }
    EXPECT_EQ(queue.droppedEventCount(), 0U);

    EXPECT_FALSE(queue.tryPush(downAt(0, 999.0F, 0.0F)));
    EXPECT_EQ(queue.droppedEventCount(), 1U);
    EXPECT_FALSE(queue.tryPush(downAt(0, 999.0F, 0.0F)));
    EXPECT_EQ(queue.droppedEventCount(), 2U);

    // Draining one makes room again: a full queue must not be permanently wedged.
    AndroidTouchEvent event{};
    ASSERT_TRUE(queue.tryPop(event));
    EXPECT_FLOAT_EQ(event.physicalX, 0.0F) << "the oldest event must survive, not the newest";
    EXPECT_TRUE(queue.tryPush(downAt(0, 1000.0F, 0.0F)));
}

// The ring wraps, so pushing and popping far more than capacity must stay in order and lose
// nothing while the consumer keeps up.
TEST(AndroidTouchEventQueueTest, WrapsWithoutLosingOrDuplicatingEvents)
{
    AndroidTouchEventQueue queue;
    constexpr usize TotalEvents = AndroidTouchEventCapacity * 4;

    usize popped = 0;
    for (usize index = 0; index < TotalEvents; ++index)
    {
        ASSERT_TRUE(queue.tryPush(downAt(0, static_cast<float>(index), 0.0F)));
        AndroidTouchEvent event{};
        ASSERT_TRUE(queue.tryPop(event));
        EXPECT_FLOAT_EQ(event.physicalX, static_cast<float>(index));
        ++popped;
    }
    EXPECT_EQ(popped, TotalEvents);
    EXPECT_EQ(queue.droppedEventCount(), 0U);
}

// The real topology: Android's UI thread produces while the engine owner thread consumes. Byte
// conservation is asserted unconditionally -- whatever arrives must arrive in order, with nothing
// duplicated, even though how much gets dropped depends on scheduling.
TEST(AndroidTouchEventQueueTest, SurvivesConcurrentProducerAndConsumer)
{
    AndroidTouchEventQueue queue;
    constexpr usize TotalEvents = 20000;

    std::atomic<bool> producerDone{false};
    std::vector<float> received;
    received.reserve(TotalEvents);

    std::thread consumer([&] {
        AndroidTouchEvent event{};
        while (!producerDone.load(std::memory_order_acquire) || queue.tryPop(event))
        {
            if (queue.tryPop(event))
            {
                received.push_back(event.physicalX);
            }
        }
        while (queue.tryPop(event))
        {
            received.push_back(event.physicalX);
        }
    });

    usize pushed = 0;
    for (usize index = 0; index < TotalEvents; ++index)
    {
        if (queue.tryPush(downAt(0, static_cast<float>(index), 0.0F)))
        {
            ++pushed;
        }
    }
    producerDone.store(true, std::memory_order_release);
    consumer.join();

    EXPECT_EQ(pushed + queue.droppedEventCount(), TotalEvents)
        << "every event must be either accepted or counted as dropped";
    EXPECT_LE(received.size(), pushed);
    // Order must hold across the whole run, which is what a lost release/acquire pairing breaks.
    EXPECT_TRUE(std::is_sorted(received.begin(), received.end()))
        << "events must be observed in push order";
}

} // namespace
} // namespace Tina::Platform
