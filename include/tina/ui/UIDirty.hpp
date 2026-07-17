#pragma once

#include <tina/core/base/Types.hpp>

#include <type_traits>

namespace Tina::UI {

enum class UIDirty : u16 {
    None = 0,
    Structure = 1u << 0,
    Style = 1u << 1,
    Measure = 1u << 2,
    Arrange = 1u << 3,
    Transform = 1u << 4,
    Clip = 1u << 5,
    Paint = 1u << 6,
    Composite = 1u << 7,
    HitTest = 1u << 8,
    Order = 1u << 9,
    Semantics = 1u << 10,
};

inline constexpr UIDirty UIDirtyMaskAll = static_cast<UIDirty>((1u << 11) - 1u);

[[nodiscard]] constexpr u16 dirtyMaskValue(UIDirty value) noexcept
{
    return static_cast<std::underlying_type_t<UIDirty>>(value);
}

[[nodiscard]] constexpr UIDirty operator|(UIDirty left, UIDirty right) noexcept
{
    return static_cast<UIDirty>(dirtyMaskValue(left) | dirtyMaskValue(right));
}

[[nodiscard]] constexpr UIDirty operator&(UIDirty left, UIDirty right) noexcept
{
    return static_cast<UIDirty>(dirtyMaskValue(left) & dirtyMaskValue(right));
}

[[nodiscard]] constexpr UIDirty operator^(UIDirty left, UIDirty right) noexcept
{
    return static_cast<UIDirty>(dirtyMaskValue(left) ^ dirtyMaskValue(right));
}

[[nodiscard]] constexpr UIDirty operator~(UIDirty value) noexcept
{
    return static_cast<UIDirty>(dirtyMaskValue(UIDirtyMaskAll) & ~dirtyMaskValue(value));
}

constexpr UIDirty& operator|=(UIDirty& left, UIDirty right) noexcept
{
    left = left | right;
    return left;
}

constexpr UIDirty& operator&=(UIDirty& left, UIDirty right) noexcept
{
    left = left & right;
    return left;
}

constexpr UIDirty& operator^=(UIDirty& left, UIDirty right) noexcept
{
    left = left ^ right;
    return left;
}

[[nodiscard]] constexpr bool anyDirty(UIDirty value) noexcept
{
    return value != UIDirty::None;
}

[[nodiscard]] constexpr bool hasDirty(UIDirty value, UIDirty flags) noexcept
{
    return anyDirty(value & flags);
}

[[nodiscard]] constexpr bool hasAllDirty(UIDirty value, UIDirty flags) noexcept
{
    return (dirtyMaskValue(value) & dirtyMaskValue(flags)) == dirtyMaskValue(flags);
}

[[nodiscard]] constexpr UIDirty clearDirty(UIDirty value, UIDirty flags) noexcept
{
    return value & ~flags;
}

} // namespace Tina::UI
