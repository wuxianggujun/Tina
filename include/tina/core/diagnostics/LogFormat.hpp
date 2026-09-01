#pragma once

#include <tina/core/base/Types.hpp>

#include <charconv>
#include <cstddef>
#include <string_view>
#include <type_traits>

// Minimal {}-substitution formatter for log messages.
//
// std::format is deliberately not used. The three Base types in core/base exist
// because libc++ (which the Android NDK uses) lacks the corresponding
// facilities, and naming an unavailable facility in a public header makes the
// whole module uncompilable for Android. <format> has not been tested on NDK
// 28/29 here, so this stays with facilities the repository already ships on
// Android: integral std::to_chars (text/ParseFloat.hpp documents that the
// integral half is present) and std::snprintf for floating point.
//
// Never allocates, never throws, never uses locales. Output is truncated rather
// than grown; callers observe truncation through the returned flag.
namespace Tina::Core::Diagnostics::Format {

struct Result final {
    usize length = 0;
    bool truncated = false;
};

// A single formattable value. Type erasure keeps the variadic frontend out of
// the sink path: arguments are converted at the call site, and the substitution
// loop below is not a template.
class Argument final {
  public:
    Argument() noexcept = default;

    // NOLINTBEGIN(google-explicit-constructor) -- implicit conversion is the
    // point; it lets the macro forward call-site values without spelling types.
    Argument(bool value) noexcept : m_kind(Kind::Boolean), m_boolean(value) {}
    Argument(char value) noexcept : m_kind(Kind::Character), m_character(value) {}
    Argument(std::string_view value) noexcept : m_kind(Kind::Text), m_text(value) {}
    Argument(const char* value) noexcept
        : m_kind(Kind::Text), m_text(value != nullptr ? std::string_view{value} : std::string_view{"(null)"})
    {
    }

    template <typename Value>
        requires std::is_integral_v<Value> && (!std::is_same_v<std::remove_cv_t<Value>, bool>)
                 && (!std::is_same_v<std::remove_cv_t<Value>, char>)
    Argument(Value value) noexcept
    {
        if constexpr (std::is_signed_v<Value>) {
            m_kind = Kind::Signed;
            m_signed = static_cast<i64>(value);
        } else {
            m_kind = Kind::Unsigned;
            m_unsigned = static_cast<u64>(value);
        }
    }

    template <typename Value>
        requires std::is_floating_point_v<Value>
    Argument(Value value) noexcept : m_kind(Kind::Real), m_real(static_cast<double>(value))
    {
    }

    // Constrained to void pointers so it does not swallow const char*.
    Argument(const void* value) noexcept : m_kind(Kind::Pointer), m_pointer(value) {}
    // NOLINTEND(google-explicit-constructor)

    // Appends this value to buffer at offset. Returns bytes written; sets
    // overflow when the value did not fit entirely.
    [[nodiscard]] usize appendTo(char* buffer, usize capacity, usize offset, bool& overflow) const noexcept;

  private:
    enum class Kind : u8 {
        Empty = 0,
        Boolean,
        Character,
        Text,
        Signed,
        Unsigned,
        Real,
        Pointer,
    };

    Kind m_kind = Kind::Empty;
    union {
        bool m_boolean;
        char m_character;
        i64 m_signed;
        u64 m_unsigned;
        double m_real;
        const void* m_pointer;
    };
    std::string_view m_text{};
};

// Substitutes each {} in pattern with the next argument, in order.
//
// "{{" emits a literal '{'. A {} with no argument left emits "{?}" and a
// leftover argument is appended as " {extra:N}" -- both are visible in the
// output rather than silently dropped, because a wrong argument count is a
// programming error that a log line should expose, not hide.
[[nodiscard]] Result format(
    char* buffer,
    usize capacity,
    std::string_view pattern,
    const Argument* arguments,
    usize argumentCount) noexcept;

} // namespace Tina::Core::Diagnostics::Format
