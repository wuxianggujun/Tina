#include <tina/platform/android/AndroidInputBridge.hpp>

#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <span>

namespace Tina::Platform {

AndroidTouchSlotTable::AndroidTouchSlotTable() noexcept
{
    androidPointerIds_.fill(UnusedPointerId);
}

u8 AndroidTouchSlotTable::acquire(i32 androidPointerId) noexcept
{
    if (androidPointerId < 0)
    {
        return InvalidSlot;
    }

    // An already-mapped id keeps its slot. A duplicated Down means the matching Up was lost, and
    // returning InvalidSlot here would strand that finger for the rest of the gesture -- the
    // cocos2d-x failure this table exists to prevent. Reusing the slot at worst restarts the
    // finger; dropping it makes the finger permanently invisible.
    const u8 existing = find(androidPointerId);
    if (existing != InvalidSlot)
    {
        return existing;
    }

    for (usize slot = 0; slot < androidPointerIds_.size(); ++slot)
    {
        if (androidPointerIds_[slot] == UnusedPointerId)
        {
            androidPointerIds_[slot] = androidPointerId;
            return static_cast<u8>(slot);
        }
    }
    // Genuinely out of slots: more simultaneous fingers than PointerCapacity. Reporting it lets
    // the caller drop the event, rather than evicting a finger that is still down.
    return InvalidSlot;
}

u8 AndroidTouchSlotTable::find(i32 androidPointerId) const noexcept
{
    if (androidPointerId < 0)
    {
        return InvalidSlot;
    }
    for (usize slot = 0; slot < androidPointerIds_.size(); ++slot)
    {
        if (androidPointerIds_[slot] == androidPointerId)
        {
            return static_cast<u8>(slot);
        }
    }
    return InvalidSlot;
}

void AndroidTouchSlotTable::release(i32 androidPointerId) noexcept
{
    const u8 slot = find(androidPointerId);
    if (slot != InvalidSlot)
    {
        androidPointerIds_[slot] = UnusedPointerId;
    }
}

void AndroidTouchSlotTable::releaseAll() noexcept
{
    androidPointerIds_.fill(UnusedPointerId);
}

usize AndroidTouchSlotTable::activeCount() const noexcept
{
    return static_cast<usize>(
        std::count_if(androidPointerIds_.begin(), androidPointerIds_.end(),
                      [](const i32 id) noexcept { return id != UnusedPointerId; }));
}

bool makeAndroidTextEvent(std::string_view utf8, AndroidTextEvent& event) noexcept
{
    if (utf8.empty() || utf8.size() > AndroidTextCommitBytes)
    {
        return false;
    }
    // Reuses the engine's own validator rather than hand-rolling one: PlatformFrameBuilder applies
    // exactly this check when the transition is appended, and a second implementation would eventually
    // disagree with it -- at which point the frame is rejected wholesale instead of this one commit.
    // It also rejects embedded NUL, which Java strings can legitimately contain.
    if (!Core::countStrictUtf8CodepointsWithoutNul(utf8).has_value())
    {
        return false;
    }

    std::copy(utf8.begin(), utf8.end(), event.utf8.begin());
    event.byteCount = static_cast<u16>(utf8.size());
    return true;
}

namespace {

// UTF-16 code units -> codepoints, for converting Android's cursor offset.
//
// A surrogate pair is two units but one codepoint, so the two counts diverge the moment an emoji is in
// the preedit -- and Tina's cursorCodepoint is validated against the *codepoint* count
// (PlatformFrameBuilder rejects the whole payload otherwise). Counting units as codepoints would put
// the caret past the end of any preedit containing an astral-plane character.
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

bool makeAndroidCompositionEventFromUtf16(std::u16string_view utf16, i32 cursorUtf16Offset,
                                         AndroidCompositionAction action,
                                         AndroidCompositionEvent& event) noexcept
{
    event.action = action;

    // An empty preedit is meaningful here, unlike a commit: it is how Android says the composing
    // region was emptied, which the session turns into a cancel.
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

    // Clamped rather than rejected. Android's newCursorPosition is an offset relative to the composing
    // text and is routinely out of range -- IMEs pass 1 to mean "after the text" regardless of its
    // length. Rejecting would drop legitimate preedit; clamping only moves the caret, and an
    // unclamped value costs the entire frame because PlatformFrameBuilder validates it against the
    // preedit's codepoint count.
    const usize cursorUnits = cursorUtf16Offset <= 0 ? 0U : static_cast<usize>(cursorUtf16Offset);
    event.cursorCodepoint = codepointsInUtf16Prefix(utf16, cursorUnits);
    return true;
}

bool makeAndroidTextEventFromUtf16(std::u16string_view utf16, AndroidTextEvent& event) noexcept
{
    if (utf16.empty())
    {
        return false;
    }
    // The converter enforces the rest: unpaired surrogates, embedded NUL, and output overflow all come
    // back as nullopt, so there is no separate length pre-check here. It writes shortest-form sequences
    // only, which is exactly what the strict validator downstream requires.
    const auto written = Core::convertUtf16ToStrictUtf8(utf16, std::span<char>{event.utf8});
    if (!written || *written == 0)
    {
        return false;
    }
    event.byteCount = static_cast<u16>(*written);
    return true;
}

namespace {

// Android KEYCODE_* values, spelled out rather than included from <android/keycodes.h>.
//
// That header only exists when compiling against the NDK, and this translation unit belongs to
// tina_platform_android, which deliberately carries no Android SDK dependency so it stays testable
// without a device. These are ABI-stable public platform constants.
namespace AndroidKeyCode {
inline constexpr i32 Back = 4;
inline constexpr i32 Digit0 = 7;
inline constexpr i32 DpadUp = 19;
inline constexpr i32 DpadDown = 20;
inline constexpr i32 DpadLeft = 21;
inline constexpr i32 DpadRight = 22;
inline constexpr i32 DpadCenter = 23;
inline constexpr i32 A = 29;
inline constexpr i32 Comma = 55;
inline constexpr i32 Period = 56;
inline constexpr i32 AltLeft = 57;
inline constexpr i32 AltRight = 58;
inline constexpr i32 ShiftLeft = 59;
inline constexpr i32 ShiftRight = 60;
inline constexpr i32 Tab = 61;
inline constexpr i32 Space = 62;
inline constexpr i32 Enter = 66;
inline constexpr i32 Del = 67;
inline constexpr i32 Grave = 68;
inline constexpr i32 Minus = 69;
inline constexpr i32 Equals = 70;
inline constexpr i32 LeftBracket = 71;
inline constexpr i32 RightBracket = 72;
inline constexpr i32 Backslash = 73;
inline constexpr i32 Semicolon = 74;
inline constexpr i32 Apostrophe = 75;
inline constexpr i32 Slash = 76;
inline constexpr i32 PageUp = 92;
inline constexpr i32 PageDown = 93;
inline constexpr i32 Escape = 111;
inline constexpr i32 ForwardDel = 112;
inline constexpr i32 CtrlLeft = 113;
inline constexpr i32 CtrlRight = 114;
inline constexpr i32 MoveHome = 122;
inline constexpr i32 MoveEnd = 123;
inline constexpr i32 F1 = 131;
} // namespace AndroidKeyCode

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

} // namespace

Key androidKeyFromKeyCode(i32 androidKeyCode) noexcept
{
    if (androidKeyCode >= AndroidKeyCode::A && androidKeyCode <= AndroidKeyCode::A + 25)
    {
        return static_cast<Key>(static_cast<u16>(Key::A) + (androidKeyCode - AndroidKeyCode::A));
    }
    if (androidKeyCode >= AndroidKeyCode::Digit0 && androidKeyCode <= AndroidKeyCode::Digit0 + 9)
    {
        return static_cast<Key>(static_cast<u16>(Key::Digit0) + (androidKeyCode - AndroidKeyCode::Digit0));
    }
    if (androidKeyCode >= AndroidKeyCode::F1 && androidKeyCode <= AndroidKeyCode::F1 + 11)
    {
        return static_cast<Key>(static_cast<u16>(Key::F1) + (androidKeyCode - AndroidKeyCode::F1));
    }

    switch (androidKeyCode)
    {
    case AndroidKeyCode::Space:
        return Key::Space;
    case AndroidKeyCode::Apostrophe:
        return Key::Apostrophe;
    case AndroidKeyCode::Comma:
        return Key::Comma;
    case AndroidKeyCode::Minus:
        return Key::Minus;
    case AndroidKeyCode::Period:
        return Key::Period;
    case AndroidKeyCode::Slash:
        return Key::Slash;
    case AndroidKeyCode::Semicolon:
        return Key::Semicolon;
    case AndroidKeyCode::Equals:
        return Key::Equal;
    case AndroidKeyCode::LeftBracket:
        return Key::LeftBracket;
    case AndroidKeyCode::Backslash:
        return Key::Backslash;
    case AndroidKeyCode::RightBracket:
        return Key::RightBracket;
    case AndroidKeyCode::Grave:
        return Key::GraveAccent;
    case AndroidKeyCode::Escape:
    // Back maps to Escape rather than getting its own enumerator: it is Android's cancel/dismiss
    // gesture, which is what Escape means everywhere else. A mobile-only key would force every
    // consumer to handle two spellings of one intent.
    case AndroidKeyCode::Back:
        return Key::Escape;
    case AndroidKeyCode::Enter:
    // DpadCenter is the confirm button on a D-pad or TV remote, so it is Enter for the same reason.
    case AndroidKeyCode::DpadCenter:
        return Key::Enter;
    case AndroidKeyCode::Tab:
        return Key::Tab;
    // Android's DEL is backspace and FORWARD_DEL is forward delete. Swapping these is a classic
    // transcription slip, and it would make text editing delete the wrong side of the caret.
    case AndroidKeyCode::Del:
        return Key::Backspace;
    case AndroidKeyCode::ForwardDel:
        return Key::Delete;
    case AndroidKeyCode::DpadRight:
        return Key::Right;
    case AndroidKeyCode::DpadLeft:
        return Key::Left;
    case AndroidKeyCode::DpadDown:
        return Key::Down;
    case AndroidKeyCode::DpadUp:
        return Key::Up;
    case AndroidKeyCode::PageUp:
        return Key::PageUp;
    case AndroidKeyCode::PageDown:
        return Key::PageDown;
    case AndroidKeyCode::MoveHome:
        return Key::Home;
    case AndroidKeyCode::MoveEnd:
        return Key::End;
    case AndroidKeyCode::ShiftLeft:
        return Key::LeftShift;
    case AndroidKeyCode::ShiftRight:
        return Key::RightShift;
    case AndroidKeyCode::CtrlLeft:
        return Key::LeftControl;
    case AndroidKeyCode::CtrlRight:
        return Key::RightControl;
    case AndroidKeyCode::AltLeft:
        return Key::LeftAlt;
    case AndroidKeyCode::AltRight:
        return Key::RightAlt;
    default:
        // Unmapped, and the caller drops it. Guessing would route an unknown code to whatever
        // enumerator happens to sit at that index.
        return Key::Unknown;
    }
}

} // namespace Tina::Platform
