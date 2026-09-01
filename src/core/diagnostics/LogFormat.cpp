#include <tina/core/diagnostics/LogFormat.hpp>

#include <cstdio>
#include <cstring>

namespace Tina::Core::Diagnostics::Format {
namespace {

// Copies as much of text as fits. Sets overflow when anything was dropped.
usize appendText(char* buffer, usize capacity, usize offset, std::string_view text, bool& overflow) noexcept
{
    if (offset >= capacity) {
        overflow = overflow || !text.empty();
        return 0;
    }
    const usize room = capacity - offset;
    usize copied = text.size();
    if (copied > room) {
        copied = room;
        overflow = true;
    }
    if (copied > 0) {
        std::memcpy(buffer + offset, text.data(), copied);
    }
    return copied;
}

// to_chars writes into a scratch buffer first: it needs contiguous room for the
// whole value and reports failure rather than a partial write, so writing it
// straight into a nearly-full destination would lose the value entirely instead
// of truncating it.
template <typename Value>
usize appendNumber(char* buffer, usize capacity, usize offset, Value value, bool& overflow) noexcept
{
    // 64-bit integers need at most 20 digits plus sign; doubles in shortest
    // round-trip form stay well inside this.
    char scratch[64]{};
    const auto conversion = std::to_chars(scratch, scratch + sizeof(scratch), value);
    if (conversion.ec != std::errc{}) {
        return appendText(buffer, capacity, offset, "(number)", overflow);
    }
    const auto produced = static_cast<usize>(conversion.ptr - scratch);
    return appendText(buffer, capacity, offset, std::string_view{scratch, produced}, overflow);
}

// Floating point goes through snprintf rather than to_chars. Whether libc++
// ships the to_chars floating-point half on NDK 28 was not verified here, and
// ParseFloat.hpp only documents the from_chars gap -- so this uses the facility
// that is unambiguously available. Locale sensitivity is harmless for the same
// reason ParseFloat.hpp accepts strtof: Tina never calls setlocale or imbues,
// so the process stays in the "C" locale for its whole lifetime.
usize appendReal(char* buffer, usize capacity, usize offset, double value, bool& overflow) noexcept
{
    char scratch[64]{};
    const int produced = std::snprintf(scratch, sizeof(scratch), "%g", value);
    if (produced <= 0) {
        return appendText(buffer, capacity, offset, "(real)", overflow);
    }
    auto length = static_cast<usize>(produced);
    if (length >= sizeof(scratch)) {
        // snprintf reports what it *would* have written; the buffer holds a
        // truncated, NUL-terminated prefix.
        length = sizeof(scratch) - 1;
        overflow = true;
    }
    return appendText(buffer, capacity, offset, std::string_view{scratch, length}, overflow);
}

usize appendPointer(char* buffer, usize capacity, usize offset, const void* value, bool& overflow) noexcept
{
    if (value == nullptr) {
        return appendText(buffer, capacity, offset, "nullptr", overflow);
    }
    char scratch[2 + (sizeof(void*) * 2)]{};
    scratch[0] = '0';
    scratch[1] = 'x';
    const auto address = reinterpret_cast<uintptr>(value);
    const auto conversion = std::to_chars(scratch + 2, scratch + sizeof(scratch), address, 16);
    if (conversion.ec != std::errc{}) {
        return appendText(buffer, capacity, offset, "(pointer)", overflow);
    }
    const auto produced = static_cast<usize>(conversion.ptr - scratch);
    return appendText(buffer, capacity, offset, std::string_view{scratch, produced}, overflow);
}

} // namespace

usize Argument::appendTo(char* buffer, const usize capacity, const usize offset, bool& overflow) const noexcept
{
    switch (m_kind) {
    case Kind::Empty:
        return 0;
    case Kind::Boolean:
        return appendText(buffer, capacity, offset, m_boolean ? "true" : "false", overflow);
    case Kind::Character:
        return appendText(buffer, capacity, offset, std::string_view{&m_character, 1}, overflow);
    case Kind::Text:
        return appendText(buffer, capacity, offset, m_text, overflow);
    case Kind::Signed:
        return appendNumber(buffer, capacity, offset, m_signed, overflow);
    case Kind::Unsigned:
        return appendNumber(buffer, capacity, offset, m_unsigned, overflow);
    case Kind::Real:
        return appendReal(buffer, capacity, offset, m_real, overflow);
    case Kind::Pointer:
        return appendPointer(buffer, capacity, offset, m_pointer, overflow);
    }
    return 0;
}

Result format(
    char* const buffer,
    const usize capacity,
    const std::string_view pattern,
    const Argument* const arguments,
    const usize argumentCount) noexcept
{
    if (buffer == nullptr || capacity == 0) {
        return Result{.length = 0, .truncated = !pattern.empty()};
    }

    usize written = 0;
    usize consumed = 0;
    bool overflow = false;

    for (usize index = 0; index < pattern.size(); ++index) {
        const char current = pattern[index];

        if (current == '{') {
            if (index + 1 < pattern.size() && pattern[index + 1] == '{') {
                written += appendText(buffer, capacity, written, "{", overflow);
                ++index;
                continue;
            }
            if (index + 1 < pattern.size() && pattern[index + 1] == '}') {
                if (arguments != nullptr && consumed < argumentCount) {
                    written += arguments[consumed].appendTo(buffer, capacity, written, overflow);
                    ++consumed;
                } else {
                    written += appendText(buffer, capacity, written, "{?}", overflow);
                }
                ++index;
                continue;
            }
        }

        // }} collapses the same way {{ does, so a pattern can close a literal
        // brace it opened. A lone } is passed through instead of being an error:
        // a logger that refuses a malformed pattern loses the diagnostic that
        // prompted the call.
        if (current == '}' && index + 1 < pattern.size() && pattern[index + 1] == '}') {
            written += appendText(buffer, capacity, written, "}", overflow);
            ++index;
            continue;
        }

        written += appendText(buffer, capacity, written, std::string_view{&pattern[index], 1}, overflow);
    }

    // Surplus arguments are reported rather than dropped: a mismatched count is
    // a defect at the call site, and a silently shorter line hides it.
    if (arguments != nullptr && consumed < argumentCount) {
        written += appendText(buffer, capacity, written, " {extra:", overflow);
        written += appendNumber(buffer, capacity, written, argumentCount - consumed, overflow);
        written += appendText(buffer, capacity, written, "}", overflow);
    }

    return Result{.length = written, .truncated = overflow};
}

} // namespace Tina::Core::Diagnostics::Format
