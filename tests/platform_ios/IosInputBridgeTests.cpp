#include <tina/platform/ios/IosInputBridge.hpp>

#include <tina/core/text/Utf8.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace Tina::Platform {
namespace {

// Identity 0 is the unused sentinel, so it cannot also be a tracked UITouch. A zero-initialised
// table that treated 0 as live would claim every slot already tracks nil.
TEST(IosTouchSlotTableTest, ZeroIsNotATrackableIdentity)
{
    IosTouchSlotTable table;
    EXPECT_EQ(table.activeCount(), 0U);
    EXPECT_EQ(table.find(0), IosTouchSlotTable::InvalidSlot);
    EXPECT_EQ(table.acquire(0), IosTouchSlotTable::InvalidSlot);
    EXPECT_EQ(table.activeCount(), 0U);
}

TEST(IosTouchSlotTableTest, AssignsDenseSlotsForArbitraryTouchIdentities)
{
    IosTouchSlotTable table;
    // UITouch addresses are sparse and reused; engine slots must come out dense.
    const u8 first = table.acquire(0x1000);
    const u8 second = table.acquire(0x2000);
    const u8 third = table.acquire(0xABCD);
    EXPECT_EQ(first, 0U);
    EXPECT_EQ(second, 1U);
    EXPECT_EQ(third, 2U);
    EXPECT_EQ(table.activeCount(), 3U);
}

// The exact cocos2d-x defect ADR 0032 cites: its handleTouchesBegin had no else branch for an
// identity already in the map, so it dropped the event -- and because UIKit reuses UITouch objects,
// one stale entry made every future finger at that address permanently invisible.
TEST(IosTouchSlotTableTest, ADuplicateBeganKeepsTheSlotInsteadOfStrandingTheFinger)
{
    IosTouchSlotTable table;
    const u8 first = table.acquire(0x50);
    ASSERT_NE(first, IosTouchSlotTable::InvalidSlot);

    const u8 again = table.acquire(0x50);
    EXPECT_EQ(again, first);
    EXPECT_EQ(table.activeCount(), 1U) << "a duplicate Began must not consume a second slot";
}

TEST(IosTouchSlotTableTest, AReleasedSlotServesTheNextFinger)
{
    IosTouchSlotTable table;
    const u8 first = table.acquire(0x11);
    table.release(0x11);
    EXPECT_EQ(table.activeCount(), 0U);
    EXPECT_EQ(table.find(0x11), IosTouchSlotTable::InvalidSlot);

    const u8 reused = table.acquire(0x12);
    EXPECT_EQ(reused, first);
}

TEST(IosTouchSlotTableTest, RejectsMoreSimultaneousFingersThanCapacity)
{
    IosTouchSlotTable table;
    for (usize index = 0; index < PointerCapacity; ++index)
    {
        ASSERT_NE(table.acquire(0x100 + index), IosTouchSlotTable::InvalidSlot);
    }
    EXPECT_EQ(table.activeCount(), PointerCapacity);

    // Refused rather than evicting a finger that is still down: eviction is how two fingers end up
    // sharing one slot's state. iPad reports up to eleven simultaneous touches.
    EXPECT_EQ(table.acquire(0x999), IosTouchSlotTable::InvalidSlot);
    EXPECT_EQ(table.activeCount(), PointerCapacity);
}

TEST(IosTouchSlotTableTest, ReleaseAllClearsEveryMapping)
{
    IosTouchSlotTable table;
    (void)table.acquire(0x1);
    (void)table.acquire(0x2);
    (void)table.acquire(0x3);
    ASSERT_EQ(table.activeCount(), 3U);

    table.releaseAll();
    EXPECT_EQ(table.activeCount(), 0U);
    EXPECT_EQ(table.find(0x2), IosTouchSlotTable::InvalidSlot);
    EXPECT_EQ(table.acquire(0x4), 0U);
}

TEST(IosTouchSlotTableTest, ReleasingAnUnknownIdentityIsANoOp)
{
    IosTouchSlotTable table;
    (void)table.acquire(0x10);
    table.release(0x99);
    EXPECT_EQ(table.activeCount(), 1U);
    EXPECT_EQ(table.find(0x10), 0U);
}

// USB HID Usage Page 0x07 values, restated here rather than shared with the implementation: a shared
// constant would make the test agree with the code by construction and prove nothing. These come
// from the USB HID Usage Tables, which UIKeyboardHIDUsage mirrors numerically.
constexpr i32 HidKeyboardA = 0x04;
constexpr i32 HidKeyboardZ = 0x1D;
constexpr i32 HidKeyboard1 = 0x1E;
constexpr i32 HidKeyboard9 = 0x26;
constexpr i32 HidKeyboard0 = 0x27;
constexpr i32 HidKeyboardReturnOrEnter = 0x28;
constexpr i32 HidKeyboardEscape = 0x29;
constexpr i32 HidKeyboardDeleteOrBackspace = 0x2A;
constexpr i32 HidKeyboardDeleteForward = 0x4C;
constexpr i32 HidKeyboardF1 = 0x3A;
constexpr i32 HidKeyboardF12 = 0x45;
constexpr i32 HidKeyboardLeftGui = 0xE3;
constexpr i32 HidKeyboardRightGui = 0xE7;
constexpr i32 HidKeyboardApplication = 0x65;
constexpr i32 HidKeypad1 = 0x59;
constexpr i32 HidKeypad9 = 0x61;
constexpr i32 HidKeypad0 = 0x62;
constexpr i32 HidKeyboardVolumeUp = 0x80;

TEST(IosKeyMappingTest, MapsContiguousLetterAndFunctionRuns)
{
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardA), Key::A);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardA + 1), Key::B);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardZ), Key::Z);

    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardF1), Key::F1);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardF12), Key::F12);
}

// HID puts 1..9 before 0, unlike ASCII and unlike Tina's Key. Mapping the run as Keyboard1 -> Digit0
// is the obvious slip and would shift every digit by one -- which no compiler catches.
TEST(IosKeyMappingTest, DoesNotShiftDigitsByOne)
{
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboard1), Key::Digit1);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboard9), Key::Digit9);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboard0), Key::Digit0);

    EXPECT_EQ(iosKeyFromHidUsage(HidKeypad1), Key::Keypad1);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeypad9), Key::Keypad9);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeypad0), Key::Keypad0);
}

// DeleteOrBackspace is backspace and DeleteForward is forward delete, matching the labels on an
// Apple keyboard rather than the words. Swapping them would make text editing delete the wrong side
// of the caret -- which no compiler or type checker catches.
TEST(IosKeyMappingTest, DoesNotSwapDeleteAndBackspace)
{
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardDeleteOrBackspace), Key::Backspace);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardDeleteForward), Key::Delete);
}

TEST(IosKeyMappingTest, MapsEnterEscapeAndCommand)
{
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardReturnOrEnter), Key::Enter);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardEscape), Key::Escape);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardLeftGui), Key::LeftSuper)
        << "GUI is the Command key, which is Super here rather than a Command enumerator";
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardRightGui), Key::RightSuper);
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardApplication), Key::Menu);
}

TEST(IosKeyMappingTest, ReportsUnknownForUnmappedUsages)
{
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardVolumeUp), Key::Unknown)
        << "volume keys belong to the system, not the engine";
    EXPECT_EQ(iosKeyFromHidUsage(0), Key::Unknown);
    EXPECT_EQ(iosKeyFromHidUsage(-1), Key::Unknown);
    EXPECT_EQ(iosKeyFromHidUsage(99999), Key::Unknown);
}

TEST(IosKeyMappingTest, RangesDoNotOverrunTheirEndpoints)
{
    EXPECT_NE(iosKeyFromHidUsage(HidKeyboardZ + 1), Key::Z);
    EXPECT_NE(iosKeyFromHidUsage(HidKeyboardA - 1), Key::A);
    // 0x46 is PrintScreen in the USB HID tables, which is what proves the F-key run stopped at F12:
    // an off-by-one in the range check would report an F key here instead. Not Key::Unknown -- the
    // usage really is mapped, just not to a function key.
    EXPECT_EQ(iosKeyFromHidUsage(HidKeyboardF12 + 1), Key::PrintScreen);
}

TEST(IosTextEventTest, AcceptsValidUtf8AndOwnsItsBytes)
{
    IosTextEvent event{};
    ASSERT_TRUE(makeIosTextEvent("hello", event));
    EXPECT_EQ(event.byteCount, 5U);
    EXPECT_EQ(std::string_view(event.utf8.data(), event.byteCount), "hello");

    ASSERT_TRUE(makeIosTextEvent("中文", event));
    EXPECT_EQ(event.byteCount, 6U);
    EXPECT_EQ(std::string_view(event.utf8.data(), event.byteCount), "中文");
}

TEST(IosTextEventTest, RejectsWhatTheFrameBuilderWouldReject)
{
    IosTextEvent event{};
    EXPECT_FALSE(makeIosTextEvent("", event)) << "an empty commit carries no information";
    EXPECT_FALSE(makeIosTextEvent(std::string_view{"a\0b", 3}, event));
    EXPECT_FALSE(makeIosTextEvent("\xE4\xB8", event)) << "truncated multi-byte sequence";
    EXPECT_FALSE(makeIosTextEvent("\xC0\x80", event)) << "overlong encoding of NUL";
    EXPECT_FALSE(makeIosTextEvent("\xFF", event)) << "not a valid lead byte";
}

TEST(IosTextEventTest, TheAdvertisedCapacityIsUsableAndTheNextByteIsNot)
{
    IosTextEvent event{};
    const std::string exact(IosTextCommitBytes, 'x');
    EXPECT_TRUE(makeIosTextEvent(exact, event));
    EXPECT_EQ(event.byteCount, IosTextCommitBytes);

    const std::string tooLong(IosTextCommitBytes + 1, 'x');
    EXPECT_FALSE(makeIosTextEvent(tooLong, event));
}

TEST(IosTextEventTest, ConvertsUtf16IncludingEmoji)
{
    IosTextEvent event{};
    ASSERT_TRUE(makeIosTextEventFromUtf16(std::u16string{u'h', u'i'}, event));
    EXPECT_EQ(std::string_view(event.utf8.data(), event.byteCount), "hi");

    // U+1F600 as the surrogate pair an NSString stores. One four-byte sequence, not two three-byte
    // ones -- going through UTF-8 via NSString's UTF8String would have produced CESU-8.
    ASSERT_TRUE(makeIosTextEventFromUtf16(std::u16string{u'\xD83D', u'\xDE00'}, event));
    EXPECT_EQ(event.byteCount, 4U);
    EXPECT_EQ(std::string_view(event.utf8.data(), event.byteCount), "\xF0\x9F\x98\x80");
    EXPECT_TRUE(Core::isStrictUtf8WithoutNul(std::string_view(event.utf8.data(), event.byteCount)));
}

TEST(IosTextEventTest, RejectsUtf16ThatCannotBeRepresented)
{
    IosTextEvent event{};
    EXPECT_FALSE(makeIosTextEventFromUtf16(std::u16string{}, event));
    EXPECT_FALSE(makeIosTextEventFromUtf16(std::u16string{u'\xD83D'}, event)) << "unpaired surrogate";
    EXPECT_FALSE(makeIosTextEventFromUtf16(std::u16string{u'a', u'\0'}, event)) << "embedded NUL";

    const std::u16string tooLong(IosTextCommitBytes + 1, u'x');
    EXPECT_FALSE(makeIosTextEventFromUtf16(tooLong, event));
}

TEST(IosCompositionEventTest, AcceptsAnEmptyPreeditUnlikeACommit)
{
    IosCompositionEvent event{};
    ASSERT_TRUE(makeIosCompositionEventFromUtf16({}, 0, IosCompositionAction::SetMarkedText, event));
    EXPECT_EQ(event.byteCount, 0U);
    EXPECT_EQ(event.cursorCodepoint, 0U);
    EXPECT_EQ(event.action, IosCompositionAction::SetMarkedText);
}

TEST(IosCompositionEventTest, ConvertsTheCursorFromUtf16UnitsToCodepoints)
{
    IosCompositionEvent event{};
    const std::u16string text{u'\xD83D', u'\xDE00', u'a'};
    ASSERT_TRUE(makeIosCompositionEventFromUtf16(text, 3, IosCompositionAction::SetMarkedText, event));
    EXPECT_EQ(event.cursorCodepoint, 2U);

    ASSERT_TRUE(makeIosCompositionEventFromUtf16(text, 1, IosCompositionAction::SetMarkedText, event));
    EXPECT_EQ(event.cursorCodepoint, 1U);
}

TEST(IosCompositionEventTest, ClampsAnOutOfRangeCursorRatherThanRejecting)
{
    IosCompositionEvent event{};
    ASSERT_TRUE(makeIosCompositionEventFromUtf16(std::u16string{u"ni"}, 99,
                                                 IosCompositionAction::SetMarkedText, event));
    EXPECT_EQ(event.cursorCodepoint, 2U);

    ASSERT_TRUE(makeIosCompositionEventFromUtf16(std::u16string{u"ni"}, -5,
                                                 IosCompositionAction::SetMarkedText, event));
    EXPECT_EQ(event.cursorCodepoint, 0U);
}

TEST(IosCompositionEventTest, TheAdvertisedPreeditCapacityIsUsableAndTheNextByteIsNot)
{
    static_assert(IosCompositionPreeditBytes == 512,
                  "must match UI::Detail::UIImeCompositionState::MaximumPreeditBytes");

    IosCompositionEvent event{};
    const std::u16string exact(IosCompositionPreeditBytes, u'x');
    EXPECT_TRUE(makeIosCompositionEventFromUtf16(exact, 0, IosCompositionAction::SetMarkedText, event));
    EXPECT_EQ(event.byteCount, IosCompositionPreeditBytes);

    const std::u16string tooLong(IosCompositionPreeditBytes + 1, u'x');
    EXPECT_FALSE(makeIosCompositionEventFromUtf16(tooLong, 0, IosCompositionAction::SetMarkedText, event));
}

TEST(IosCommitEventsTest, SplitsALongCommitOnCodepointBoundaries)
{
    // One slot is 512 bytes of UTF-8. 600 ASCII characters therefore become two events, and the
    // split must not sit inside a multi-byte character.
    const std::u16string longText(600, u'x');
    std::array<IosCompositionEvent, 4> events{};
    usize eventCount = 0;
    ASSERT_TRUE(makeIosCommitEventsFromUtf16(longText, events, eventCount));
    ASSERT_EQ(eventCount, 2U);
    EXPECT_EQ(events[0].action, IosCompositionAction::Commit);
    EXPECT_EQ(events[0].byteCount, IosCompositionPreeditBytes);
    EXPECT_EQ(events[1].byteCount, 600U - IosCompositionPreeditBytes);
}

TEST(IosCommitEventsTest, RejectsEmptyInvalidOrInsufficientCapacity)
{
    std::array<IosCompositionEvent, 1> events{};
    usize eventCount = 99;
    EXPECT_FALSE(makeIosCommitEventsFromUtf16({}, events, eventCount));
    EXPECT_EQ(eventCount, 0U);

    EXPECT_FALSE(makeIosCommitEventsFromUtf16(std::u16string{u'\xD83D'}, events, eventCount));
    EXPECT_EQ(eventCount, 0U);

    const std::u16string tooLongForOneSlot(IosCompositionPreeditBytes + 1, u'x');
    EXPECT_FALSE(makeIosCommitEventsFromUtf16(tooLongForOneSlot, events, eventCount))
        << "insufficient output span must fail rather than expose a prefix";
    EXPECT_EQ(eventCount, 0U);
}

TEST(IosEventQueueTest, DropsAndCountsWhenFullAndStaysFifo)
{
    IosTouchEventQueue queue;
    IosTouchEvent event{};
    EXPECT_FALSE(queue.tryPop(event));

    for (usize index = 0; index < IosTouchEventCapacity; ++index)
    {
        ASSERT_TRUE(queue.tryPush(IosTouchEvent{.phase = IosTouchPhase::Began,
                                                .pointerSlot = 0,
                                                .pointX = static_cast<float>(index),
                                                .pointY = 0.0F}));
    }
    EXPECT_EQ(queue.droppedEventCount(), 0U);
    EXPECT_FALSE(queue.tryPush(IosTouchEvent{.pointX = 999.0F}));
    EXPECT_EQ(queue.droppedEventCount(), 1U);

    ASSERT_TRUE(queue.tryPop(event));
    EXPECT_FLOAT_EQ(event.pointX, 0.0F) << "the oldest event must survive, not the newest";
    EXPECT_TRUE(queue.tryPush(IosTouchEvent{.pointX = 1000.0F}));
}

TEST(IosEventQueueTest, TryPushBatchIsAllOrNothing)
{
    IosCompositionEventQueue queue;
    IosCompositionEvent first{};
    IosCompositionEvent second{};
    ASSERT_TRUE(makeIosCompositionEventFromUtf16(u"aa", 0, IosCompositionAction::Commit, first));
    ASSERT_TRUE(makeIosCompositionEventFromUtf16(u"bb", 0, IosCompositionAction::Commit, second));

    for (usize index = 0; index < IosCompositionEventCapacity - 1; ++index)
    {
        ASSERT_TRUE(queue.tryPush(first));
    }

    const IosCompositionEvent batch[] = {first, second};
    EXPECT_FALSE(queue.tryPushBatch(batch))
        << "a batch that does not fit must expose none of its slots";
    EXPECT_EQ(queue.droppedEventCount(), 2U);

    IosCompositionEvent popped{};
    usize remaining = 0;
    while (queue.tryPop(popped))
    {
        ++remaining;
        EXPECT_EQ(std::string_view(popped.utf8.data(), popped.byteCount), "aa");
    }
    EXPECT_EQ(remaining, IosCompositionEventCapacity - 1);
}

} // namespace
} // namespace Tina::Platform
