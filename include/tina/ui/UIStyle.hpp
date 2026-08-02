#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>

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

// Startup-registered stylesheet identities. Zero is invalid so a default
// constructed id never aliases a registered class or token.
struct UIStyleClassId final {
    u32 value = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return value != 0;
    }

    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }

    auto operator<=>(const UIStyleClassId&) const = default;
};

struct UIStyleTokenId final {
    u32 value = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return value != 0;
    }

    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }

    auto operator<=>(const UIStyleTokenId&) const = default;
};

// Node-local pseudo states are a mask. Style matching must derive this mask
// from retained state; it does not create a second interaction state machine.
enum class UIStyleState : u16 {
    None = 0,
    Hovered = 1U << 0U,
    Pressed = 1U << 1U,
    Focused = 1U << 2U,
    Disabled = 1U << 3U,
    Checked = 1U << 4U,
    Selected = 1U << 5U,
    Open = 1U << 6U,
    Dragging = 1U << 7U,
    All = (1U << 8U) - 1U,
};

[[nodiscard]] constexpr UIStyleState operator|(UIStyleState left, UIStyleState right) noexcept
{
    return static_cast<UIStyleState>(static_cast<u16>(left) | static_cast<u16>(right));
}

[[nodiscard]] constexpr UIStyleState operator&(UIStyleState left, UIStyleState right) noexcept
{
    return static_cast<UIStyleState>(static_cast<u16>(left) & static_cast<u16>(right));
}

constexpr UIStyleState& operator|=(UIStyleState& left, UIStyleState right) noexcept
{
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool hasStyleState(UIStyleState set, UIStyleState state) noexcept
{
    return (set & state) == state;
}

// First stylesheet declaration slice. Rules are copied and precompiled by the
// owning UIContext; caller storage is borrowed only for installStyleSheet().
// A non-zero colorToken/imageTintToken selects its startup-registered value and
// requires the matching literal color to remain default initialized, avoiding
// ambiguous literal/token precedence. Box fill and image tint later-rule-wins
// independently when matching the same role/class/state chain.
struct UIStyleBoxFillRule final {
    UIStyleRoleId role = UIStyleRoleId::None;
    UIStyleClassId styleClass{};
    UIStyleState requiredStates = UIStyleState::None;
    UIStraightSrgba8Color color{};
    UIStyleTokenId colorToken{};
    // Optional Image/Icon tint. Applied only when imageTintToken is set or
    // imageTint is non-default; otherwise the rule does not touch image tint.
    UIStraightSrgba8Color imageTint{};
    UIStyleTokenId imageTintToken{};
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
    TextEditPaint = 1U << 11U,
    ImageTint = 1U << 12U,
    All = (1U << 13U) - 1U,
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
