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
    using Underlying = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<Underlying>(lhs) | static_cast<Underlying>(rhs));
}

template <EnumFlagsEnabled Enum>
[[nodiscard]] constexpr Enum operator&(Enum lhs, Enum rhs) noexcept
{
    using Underlying = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<Underlying>(lhs) & static_cast<Underlying>(rhs));
}

template <EnumFlagsEnabled Enum>
[[nodiscard]] constexpr Enum operator^(Enum lhs, Enum rhs) noexcept
{
    using Underlying = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<Underlying>(lhs) ^ static_cast<Underlying>(rhs));
}

template <EnumFlagsEnabled Enum>
[[nodiscard]] constexpr Enum operator~(Enum value) noexcept
{
    using Underlying = std::underlying_type_t<Enum>;
    return static_cast<Enum>(~static_cast<Underlying>(value));
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
    using Underlying = std::underlying_type_t<Enum>;
    return (static_cast<Underlying>(flags) & static_cast<Underlying>(mask)) != 0;
}

template <typename Enum>
    requires std::is_enum_v<Enum>
[[nodiscard]] constexpr bool hasAllFlags(Enum flags, Enum mask) noexcept
{
    using Underlying = std::underlying_type_t<Enum>;
    const auto maskBits = static_cast<Underlying>(mask);
    return (static_cast<Underlying>(flags) & maskBits) == maskBits;
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
    using Underlying = std::underlying_type_t<Enum>;
    const auto flagBits = static_cast<Underlying>(flag);
    auto bits = static_cast<Underlying>(flags);
    bits = enabled ? static_cast<Underlying>(bits | flagBits)
                   : static_cast<Underlying>(bits & static_cast<Underlying>(~flagBits));
    flags = static_cast<Enum>(bits);
}

} // namespace Tina

#define TINA_ENABLE_ENUM_FLAGS(EnumType) \
    template <>                         \
    struct Tina::EnableEnumFlags<EnumType> : std::true_type {}
