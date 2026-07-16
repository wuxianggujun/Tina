#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace Tina {

template <typename Enum>
struct EnableEnumFlags : std::false_type {
};

template <typename Enum>
concept EnumFlagsEnabled = std::is_enum_v<Enum> && EnableEnumFlags<Enum>::value;

template <EnumFlagsEnabled Enum>
[[nodiscard]] constexpr Enum operator|(Enum lhs, Enum rhs) noexcept
{
    return static_cast<Enum>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

template <EnumFlagsEnabled Enum>
[[nodiscard]] constexpr Enum operator&(Enum lhs, Enum rhs) noexcept
{
    return static_cast<Enum>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

template <EnumFlagsEnabled Enum>
[[nodiscard]] constexpr Enum operator^(Enum lhs, Enum rhs) noexcept
{
    return static_cast<Enum>(std::to_underlying(lhs) ^ std::to_underlying(rhs));
}

template <EnumFlagsEnabled Enum>
[[nodiscard]] constexpr Enum operator~(Enum value) noexcept
{
    return static_cast<Enum>(~std::to_underlying(value));
}

template <EnumFlagsEnabled Enum>
constexpr Enum& operator|=(Enum& lhs, Enum rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

template <EnumFlagsEnabled Enum>
constexpr Enum& operator&=(Enum& lhs, Enum rhs) noexcept
{
    lhs = lhs & rhs;
    return lhs;
}

template <EnumFlagsEnabled Enum>
constexpr Enum& operator^=(Enum& lhs, Enum rhs) noexcept
{
    lhs = lhs ^ rhs;
    return lhs;
}

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

#define TINA_ENABLE_ENUM_FLAGS(EnumType) \
    template <>                         \
    struct Tina::EnableEnumFlags<EnumType> : std::true_type {}
