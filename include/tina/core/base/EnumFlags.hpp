#pragma once

#include <type_traits>
#include <utility>

namespace Tina {

template <typename Enum>
    requires std::is_enum_v<Enum>
[[nodiscard]] constexpr bool hasAnyFlag(Enum flags, Enum mask) noexcept
{
    return (std::to_underlying(flags) & std::to_underlying(mask)) != 0;
}

template <typename Enum>
    requires std::is_enum_v<Enum>
[[nodiscard]] constexpr bool hasAllFlags(Enum flags, Enum mask) noexcept
{
    const auto maskBits = std::to_underlying(mask);
    return (std::to_underlying(flags) & maskBits) == maskBits;
}

template <typename Enum>
    requires std::is_enum_v<Enum>
[[nodiscard]] constexpr bool isFlagSet(Enum flags, Enum flag) noexcept
{
    return hasAllFlags(flags, flag);
}

template <typename Enum>
    requires std::is_enum_v<Enum>
constexpr void setFlag(Enum& flags, Enum flag, bool enabled) noexcept
{
    const auto flagBits = std::to_underlying(flag);
    auto bits = std::to_underlying(flags);
    bits = enabled ? static_cast<decltype(bits)>(bits | flagBits)
                   : static_cast<decltype(bits)>(bits & static_cast<decltype(bits)>(~flagBits));
    flags = static_cast<Enum>(bits);
}

} // namespace Tina

// Defines the bitwise operators for a flag enum. Expand it inside the namespace that
// declares the enum: for an enum type, ADL considers only its innermost enclosing
// namespace, so operators defined in `Tina` are unreachable for a caller that writes
// `Tina::UI::UIDirty::Paint | Tina::UI::UIDirty::Style` from anywhere else.
//
// `operator~` is deliberately absent. None of these enums fill their underlying type,
// so a raw complement yields bits outside the declared set; a correct one needs an
// all-bits mask this macro cannot know. Define it by hand where it is actually needed.
#define TINA_ENUM_FLAG_OPERATORS(EnumType)                                                 \
    [[nodiscard]] constexpr EnumType operator|(EnumType left, EnumType right) noexcept     \
    {                                                                                      \
        return static_cast<EnumType>(std::to_underlying(left) | std::to_underlying(right)); \
    }                                                                                      \
    [[nodiscard]] constexpr EnumType operator&(EnumType left, EnumType right) noexcept     \
    {                                                                                      \
        return static_cast<EnumType>(std::to_underlying(left) & std::to_underlying(right)); \
    }                                                                                      \
    [[nodiscard]] constexpr EnumType operator^(EnumType left, EnumType right) noexcept     \
    {                                                                                      \
        return static_cast<EnumType>(std::to_underlying(left) ^ std::to_underlying(right)); \
    }                                                                                      \
    constexpr EnumType& operator|=(EnumType& left, EnumType right) noexcept                \
    {                                                                                      \
        left = left | right;                                                               \
        return left;                                                                       \
    }                                                                                      \
    constexpr EnumType& operator&=(EnumType& left, EnumType right) noexcept                \
    {                                                                                      \
        left = left & right;                                                               \
        return left;                                                                       \
    }                                                                                      \
    constexpr EnumType& operator^=(EnumType& left, EnumType right) noexcept                \
    {                                                                                      \
        left = left ^ right;                                                               \
        return left;                                                                       \
    }                                                                                      \
    static_assert(std::is_enum_v<EnumType>, "TINA_ENUM_FLAG_OPERATORS requires an enum type")
