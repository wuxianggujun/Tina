#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::UI {

// Theme recipe identity is independent from behavior and semantics. None means
// the Element has no product-theme recipe until local visual properties are set.
enum class UIStyleRoleId : u8 {
    None = 0,
    PanelSurface,
    PanelElevated,
    ModalSurface,
    PopupSurface,
    TextBody,
    TextTitle,
    TextSecondary,
    TextAccent,
    ButtonPrimary,
    ButtonDanger,
    Checkbox,
    Slider,
    TextInput,
    ProgressBar,
    RadioButton,
    ScrollView,
    Dropdown,
    CollectionItem,
    ListView,
    TreeView,
};

// A local setter detaches one property from its role. clearOverride() restores
// selected properties from the active product theme without touching others.
enum class UIStyleOverride : u16 {
    None = 0,
    BoxPaint = 1U << 0U,
    TextStyle = 1U << 1U,
    ButtonPaint = 1U << 2U,
    CheckboxPaint = 1U << 3U,
    SliderPaint = 1U << 4U,
    ProgressBarPaint = 1U << 5U,
    RadioButtonPaint = 1U << 6U,
    ScrollViewPaint = 1U << 7U,
    DropdownPaint = 1U << 8U,
    ListViewPaint = 1U << 9U,
    TreeViewPaint = 1U << 10U,
    All = (1U << 11U) - 1U,
};

[[nodiscard]] constexpr UIStyleOverride operator|(UIStyleOverride left, UIStyleOverride right) noexcept
{
    return static_cast<UIStyleOverride>(static_cast<u16>(left) | static_cast<u16>(right));
}

[[nodiscard]] constexpr UIStyleOverride operator&(UIStyleOverride left, UIStyleOverride right) noexcept
{
    return static_cast<UIStyleOverride>(static_cast<u16>(left) & static_cast<u16>(right));
}

constexpr UIStyleOverride& operator|=(UIStyleOverride& left, UIStyleOverride right) noexcept
{
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool hasStyleOverride(UIStyleOverride set, UIStyleOverride property) noexcept
{
    return (set & property) == property;
}

} // namespace Tina::UI
