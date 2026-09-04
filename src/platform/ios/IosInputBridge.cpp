#include <tina/platform/ios/IosInputBridge.hpp>

#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <span>

namespace Tina::Platform {

IosTouchSlotTable::IosTouchSlotTable() noexcept
{
    touchIdentities_.fill(UnusedIdentity);
}

u8 IosTouchSlotTable::acquire(std::uintptr_t touchIdentity) noexcept
{
    // 0 is the sentinel, so it cannot also be a tracked identity. No real UITouch lives at address
    // 0, and a caller passing it is passing nil.
    if (touchIdentity == UnusedIdentity)
    {
        return InvalidSlot;
    }

    // An already-mapped identity keeps its slot. A duplicated Began means the matching Ended was
    // lost, and returning InvalidSlot here would strand that finger for the rest of the gesture --
    // the cocos2d-x failure this table exists to prevent. Reusing the slot at worst restarts the
    // finger; dropping it makes the finger permanently invisible.
    const u8 existing = find(touchIdentity);
    if (existing != InvalidSlot)
    {
        return existing;
    }

    for (usize slot = 0; slot < touchIdentities_.size(); ++slot)
    {
        if (touchIdentities_[slot] == UnusedIdentity)
        {
            touchIdentities_[slot] = touchIdentity;
            return static_cast<u8>(slot);
        }
    }
    // Genuinely out of slots: more simultaneous fingers than PointerCapacity. iPad reports up to
    // eleven. Reporting it lets the caller drop the event rather than evicting a finger that is
    // still down.
    return InvalidSlot;
}

u8 IosTouchSlotTable::find(std::uintptr_t touchIdentity) const noexcept
{
    if (touchIdentity == UnusedIdentity)
    {
        return InvalidSlot;
    }
    for (usize slot = 0; slot < touchIdentities_.size(); ++slot)
    {
        if (touchIdentities_[slot] == touchIdentity)
        {
            return static_cast<u8>(slot);
        }
    }
    return InvalidSlot;
}

void IosTouchSlotTable::release(std::uintptr_t touchIdentity) noexcept
{
    const u8 slot = find(touchIdentity);
    if (slot != InvalidSlot)
    {
        touchIdentities_[slot] = UnusedIdentity;
    }
}

void IosTouchSlotTable::releaseAll() noexcept
{
    touchIdentities_.fill(UnusedIdentity);
}

usize IosTouchSlotTable::activeCount() const noexcept
{
    return static_cast<usize>(std::count_if(
        touchIdentities_.begin(), touchIdentities_.end(),
        [](const std::uintptr_t identity) noexcept { return identity != UnusedIdentity; }));
}

bool makeIosTextEvent(std::string_view utf8, IosTextEvent& event) noexcept
{
    if (utf8.empty() || utf8.size() > IosTextCommitBytes)
    {
        return false;
    }
    // Reuses the engine's own validator rather than hand-rolling one: PlatformFrameBuilder applies
    // exactly this check when the transition is appended, and a second implementation would
    // eventually disagree with it -- at which point the frame is rejected wholesale instead of this
    // one commit. It also rejects embedded NUL, which an NSString can legitimately contain.
    if (!Core::countStrictUtf8CodepointsWithoutNul(utf8).has_value())
    {
        return false;
    }

    std::copy(utf8.begin(), utf8.end(), event.utf8.begin());
    event.byteCount = static_cast<u16>(utf8.size());
    return true;
}

namespace {

// UTF-16 code units -> codepoints, for converting UITextInput's selectedRange offset.
//
// A surrogate pair is two units but one codepoint, so the two counts diverge the moment an emoji is
// in the marked text -- and Tina's cursorCodepoint is validated against the *codepoint* count
// (PlatformFrameBuilder rejects the whole payload otherwise). NSRange is always in UTF-16 units, so
// treating them as codepoints would put the caret past the end of any preedit containing an
// astral-plane character.
[[nodiscard]] u32 codepointsInUtf16Prefix(std::u16string_view utf16, usize unitCount) noexcept
{
    const usize limit = (std::min)(unitCount, utf16.size());
    u32 codepoints = 0;
    for (usize index = 0; index < limit; ++index)
    {
        const char16_t unit = utf16[index];
        // Only a *complete* pair inside the prefix collapses into one codepoint. A high surrogate at
        // the prefix boundary counts as one on its own; the conversion below rejects the string
        // outright if it is genuinely unpaired, so this only affects where a caret lands.
        if (unit >= 0xD800U && unit <= 0xDBFFU && index + 1 < limit && utf16[index + 1] >= 0xDC00U &&
            utf16[index + 1] <= 0xDFFFU)
        {
            ++index;
        }
        ++codepoints;
    }
    return codepoints;
}

} // namespace

bool makeIosCompositionEventFromUtf16(std::u16string_view utf16, i32 cursorUtf16Offset,
                                      IosCompositionAction action, IosCompositionEvent& event) noexcept
{
    event.action = action;

    // An empty preedit is meaningful here, unlike a commit: it is how UIKit says the marked region
    // was emptied, which the session turns into a cancel.
    if (utf16.empty())
    {
        event.byteCount = 0;
        event.cursorCodepoint = 0;
        return true;
    }

    const auto written = Core::convertUtf16ToStrictUtf8(utf16, std::span<char>{event.utf8});
    if (!written || *written == 0)
    {
        return false;
    }
    event.byteCount = static_cast<u16>(*written);

    // Clamped rather than rejected. UITextInput's selectedRange is relative to the marked text and
    // NSNotFound arrives as a huge value, which is how UIKit says "no selection". Rejecting would
    // drop legitimate preedit; clamping only moves the caret, and an unclamped value costs the
    // entire frame because PlatformFrameBuilder validates it against the codepoint count.
    const usize cursorUnits = cursorUtf16Offset <= 0 ? 0U : static_cast<usize>(cursorUtf16Offset);
    event.cursorCodepoint = codepointsInUtf16Prefix(utf16, cursorUnits);
    return true;
}

bool makeIosTextEventFromUtf16(std::u16string_view utf16, IosTextEvent& event) noexcept
{
    if (utf16.empty())
    {
        return false;
    }
    // The converter enforces the rest: unpaired surrogates, embedded NUL, and output overflow all
    // come back as nullopt, so there is no separate length pre-check here. It writes shortest-form
    // sequences only, which is exactly what the strict validator downstream requires.
    const auto written = Core::convertUtf16ToStrictUtf8(utf16, std::span<char>{event.utf8});
    if (!written || *written == 0)
    {
        return false;
    }
    event.byteCount = static_cast<u16>(*written);
    return true;
}

bool makeIosCommitEventsFromUtf16(std::u16string_view utf16, std::span<IosCompositionEvent> events,
                                  usize& eventCount) noexcept
{
    eventCount = 0;
    if (utf16.empty())
    {
        return false;
    }

    usize offset = 0;
    while (offset < utf16.size())
    {
        if (eventCount == events.size())
        {
            eventCount = 0;
            return false;
        }

        const usize chunkStart = offset;
        usize chunkBytes = 0;
        while (offset < utf16.size())
        {
            const char16_t first = utf16[offset];
            if (first == 0 || (first >= 0xDC00U && first <= 0xDFFFU))
            {
                eventCount = 0;
                return false;
            }

            usize codeUnits = 1;
            usize utf8Bytes = 0;
            if (first >= 0xD800U && first <= 0xDBFFU)
            {
                if (offset + 1 >= utf16.size() || utf16[offset + 1] < 0xDC00U ||
                    utf16[offset + 1] > 0xDFFFU)
                {
                    eventCount = 0;
                    return false;
                }
                codeUnits = 2;
                utf8Bytes = 4;
            } else if (first <= 0x7FU)
            {
                utf8Bytes = 1;
            } else if (first <= 0x7FFU)
            {
                utf8Bytes = 2;
            } else
            {
                utf8Bytes = 3;
            }

            // Break on the boundary rather than mid-codepoint: a split surrogate pair would convert
            // to nothing and lose the character entirely.
            if (chunkBytes + utf8Bytes > IosCompositionPreeditBytes)
            {
                break;
            }
            chunkBytes += utf8Bytes;
            offset += codeUnits;
        }

        IosCompositionEvent& event = events[eventCount];
        if (!makeIosCompositionEventFromUtf16(utf16.substr(chunkStart, offset - chunkStart), 0,
                                              IosCompositionAction::Commit, event) ||
            event.byteCount == 0)
        {
            eventCount = 0;
            return false;
        }
        ++eventCount;
    }
    return true;
}

namespace {

// UIKeyboardHIDUsage values, spelled out rather than included from <UIKit/UIKit.h>.
//
// That header only exists when compiling against the iOS SDK, and this translation unit belongs to
// tina_platform_ios, which deliberately carries no Apple SDK dependency so it stays buildable and
// testable on a non-Apple host. These are USB HID Usage Table (Usage Page 0x07,
// Keyboard/Keypad) code points, which UIKeyboardHIDUsage mirrors numerically -- a published,
// version-stable standard rather than an Apple-internal numbering.
namespace HidUsage {
inline constexpr i32 KeyboardA = 0x04;
inline constexpr i32 Keyboard1 = 0x1E;
inline constexpr i32 Keyboard0 = 0x27;
inline constexpr i32 KeyboardReturnOrEnter = 0x28;
inline constexpr i32 KeyboardEscape = 0x29;
inline constexpr i32 KeyboardDeleteOrBackspace = 0x2A;
inline constexpr i32 KeyboardTab = 0x2B;
inline constexpr i32 KeyboardSpacebar = 0x2C;
inline constexpr i32 KeyboardHyphen = 0x2D;
inline constexpr i32 KeyboardEqualSign = 0x2E;
inline constexpr i32 KeyboardOpenBracket = 0x2F;
inline constexpr i32 KeyboardCloseBracket = 0x30;
inline constexpr i32 KeyboardBackslash = 0x31;
inline constexpr i32 KeyboardSemicolon = 0x33;
inline constexpr i32 KeyboardQuote = 0x34;
inline constexpr i32 KeyboardGraveAccentAndTilde = 0x35;
inline constexpr i32 KeyboardComma = 0x36;
inline constexpr i32 KeyboardPeriod = 0x37;
inline constexpr i32 KeyboardSlash = 0x38;
inline constexpr i32 KeyboardCapsLock = 0x39;
inline constexpr i32 KeyboardF1 = 0x3A;
inline constexpr i32 KeyboardPrintScreen = 0x46;
inline constexpr i32 KeyboardScrollLock = 0x47;
inline constexpr i32 KeyboardPause = 0x48;
inline constexpr i32 KeyboardInsert = 0x49;
inline constexpr i32 KeyboardHome = 0x4A;
inline constexpr i32 KeyboardPageUp = 0x4B;
inline constexpr i32 KeyboardDeleteForward = 0x4C;
inline constexpr i32 KeyboardEnd = 0x4D;
inline constexpr i32 KeyboardPageDown = 0x4E;
inline constexpr i32 KeyboardRightArrow = 0x4F;
inline constexpr i32 KeyboardLeftArrow = 0x50;
inline constexpr i32 KeyboardDownArrow = 0x51;
inline constexpr i32 KeyboardUpArrow = 0x52;
inline constexpr i32 KeypadNumLock = 0x53;
inline constexpr i32 KeypadSlash = 0x54;
inline constexpr i32 KeypadAsterisk = 0x55;
inline constexpr i32 KeypadHyphen = 0x56;
inline constexpr i32 KeypadPlus = 0x57;
inline constexpr i32 KeypadEnter = 0x58;
inline constexpr i32 Keypad1 = 0x59;
inline constexpr i32 Keypad0 = 0x62;
inline constexpr i32 KeypadPeriod = 0x63;
inline constexpr i32 KeyboardApplication = 0x65;
inline constexpr i32 KeypadEqualSign = 0x67;
inline constexpr i32 KeyboardLeftControl = 0xE0;
inline constexpr i32 KeyboardLeftShift = 0xE1;
inline constexpr i32 KeyboardLeftAlt = 0xE2;
inline constexpr i32 KeyboardLeftGui = 0xE3;
inline constexpr i32 KeyboardRightControl = 0xE4;
inline constexpr i32 KeyboardRightShift = 0xE5;
inline constexpr i32 KeyboardRightAlt = 0xE6;
inline constexpr i32 KeyboardRightGui = 0xE7;
} // namespace HidUsage

// The contiguous runs below are mapped as ranges, which only holds while both sides order them the
// same way. Asserted rather than trusted: writing 26 letter cases by hand invites exactly the
// transcription error this function exists to prevent, and a reordering of Tina's Key enum would
// otherwise silently misroute every letter.
static_assert(static_cast<u16>(Key::B) == static_cast<u16>(Key::A) + 1);
static_assert(static_cast<u16>(Key::Z) == static_cast<u16>(Key::A) + 25);
static_assert(static_cast<u16>(Key::Digit1) == static_cast<u16>(Key::Digit0) + 1);
static_assert(static_cast<u16>(Key::Digit9) == static_cast<u16>(Key::Digit0) + 9);
static_assert(static_cast<u16>(Key::F2) == static_cast<u16>(Key::F1) + 1);
static_assert(static_cast<u16>(Key::F12) == static_cast<u16>(Key::F1) + 11);
static_assert(static_cast<u16>(Key::Keypad1) == static_cast<u16>(Key::Keypad0) + 1);
static_assert(static_cast<u16>(Key::Keypad9) == static_cast<u16>(Key::Keypad0) + 9);

} // namespace

Key iosKeyFromHidUsage(i32 hidUsage) noexcept
{
    if (hidUsage >= HidUsage::KeyboardA && hidUsage <= HidUsage::KeyboardA + 25)
    {
        return static_cast<Key>(static_cast<u16>(Key::A) + (hidUsage - HidUsage::KeyboardA));
    }
    // HID puts 1..9 before 0, unlike ASCII and unlike Tina's Key. Mapping the run as
    // Keyboard1 -> Digit0 is the obvious slip and would shift every digit by one.
    if (hidUsage >= HidUsage::Keyboard1 && hidUsage <= HidUsage::Keyboard1 + 8)
    {
        return static_cast<Key>(static_cast<u16>(Key::Digit1) + (hidUsage - HidUsage::Keyboard1));
    }
    if (hidUsage >= HidUsage::KeyboardF1 && hidUsage <= HidUsage::KeyboardF1 + 11)
    {
        return static_cast<Key>(static_cast<u16>(Key::F1) + (hidUsage - HidUsage::KeyboardF1));
    }
    // Keypad digits carry the same 1..9-then-0 ordering.
    if (hidUsage >= HidUsage::Keypad1 && hidUsage <= HidUsage::Keypad1 + 8)
    {
        return static_cast<Key>(static_cast<u16>(Key::Keypad1) + (hidUsage - HidUsage::Keypad1));
    }

    switch (hidUsage)
    {
    case HidUsage::Keyboard0:
        return Key::Digit0;
    case HidUsage::Keypad0:
        return Key::Keypad0;
    case HidUsage::KeyboardSpacebar:
        return Key::Space;
    case HidUsage::KeyboardQuote:
        return Key::Apostrophe;
    case HidUsage::KeyboardComma:
        return Key::Comma;
    case HidUsage::KeyboardHyphen:
        return Key::Minus;
    case HidUsage::KeyboardPeriod:
        return Key::Period;
    case HidUsage::KeyboardSlash:
        return Key::Slash;
    case HidUsage::KeyboardSemicolon:
        return Key::Semicolon;
    case HidUsage::KeyboardEqualSign:
        return Key::Equal;
    case HidUsage::KeyboardOpenBracket:
        return Key::LeftBracket;
    case HidUsage::KeyboardBackslash:
        return Key::Backslash;
    case HidUsage::KeyboardCloseBracket:
        return Key::RightBracket;
    case HidUsage::KeyboardGraveAccentAndTilde:
        return Key::GraveAccent;
    case HidUsage::KeyboardEscape:
        return Key::Escape;
    case HidUsage::KeyboardReturnOrEnter:
        return Key::Enter;
    case HidUsage::KeyboardTab:
        return Key::Tab;
    // HID's DeleteOrBackspace is backspace and DeleteForward is forward delete, matching the labels
    // on an Apple keyboard rather than the words. Swapping these is a classic transcription slip,
    // and it would make text editing delete the wrong side of the caret.
    case HidUsage::KeyboardDeleteOrBackspace:
        return Key::Backspace;
    case HidUsage::KeyboardDeleteForward:
        return Key::Delete;
    case HidUsage::KeyboardInsert:
        return Key::Insert;
    case HidUsage::KeyboardRightArrow:
        return Key::Right;
    case HidUsage::KeyboardLeftArrow:
        return Key::Left;
    case HidUsage::KeyboardDownArrow:
        return Key::Down;
    case HidUsage::KeyboardUpArrow:
        return Key::Up;
    case HidUsage::KeyboardPageUp:
        return Key::PageUp;
    case HidUsage::KeyboardPageDown:
        return Key::PageDown;
    case HidUsage::KeyboardHome:
        return Key::Home;
    case HidUsage::KeyboardEnd:
        return Key::End;
    case HidUsage::KeyboardCapsLock:
        return Key::CapsLock;
    case HidUsage::KeyboardScrollLock:
        return Key::ScrollLock;
    case HidUsage::KeypadNumLock:
        return Key::NumLock;
    case HidUsage::KeyboardPrintScreen:
        return Key::PrintScreen;
    case HidUsage::KeyboardPause:
        return Key::Pause;
    case HidUsage::KeypadPeriod:
        return Key::KeypadDecimal;
    case HidUsage::KeypadSlash:
        return Key::KeypadDivide;
    case HidUsage::KeypadAsterisk:
        return Key::KeypadMultiply;
    case HidUsage::KeypadHyphen:
        return Key::KeypadSubtract;
    case HidUsage::KeypadPlus:
        return Key::KeypadAdd;
    case HidUsage::KeypadEnter:
        return Key::KeypadEnter;
    case HidUsage::KeypadEqualSign:
        return Key::KeypadEqual;
    case HidUsage::KeyboardLeftShift:
        return Key::LeftShift;
    case HidUsage::KeyboardRightShift:
        return Key::RightShift;
    case HidUsage::KeyboardLeftControl:
        return Key::LeftControl;
    case HidUsage::KeyboardRightControl:
        return Key::RightControl;
    case HidUsage::KeyboardLeftAlt:
        return Key::LeftAlt;
    case HidUsage::KeyboardRightAlt:
        return Key::RightAlt;
    // GUI is the Command key on Apple hardware, which is Super here. Naming it Super rather than
    // adding a Command enumerator keeps one spelling of the modifier across platforms.
    case HidUsage::KeyboardLeftGui:
        return Key::LeftSuper;
    case HidUsage::KeyboardRightGui:
        return Key::RightSuper;
    case HidUsage::KeyboardApplication:
        return Key::Menu;
    default:
        // Unmapped, and the caller drops it. Guessing would route an unknown usage to whatever
        // enumerator happens to sit at that index.
        return Key::Unknown;
    }
}

} // namespace Tina::Platform
