#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIDirty.hpp>
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
    ButtonTonal,
    ButtonOutlined,
    ButtonText,
    SegmentedButton,
    TooltipSurface,
    Tab,
    MenuSurface,
    MenuItem,
    DividerSubtle,
    DividerStrong,
    DividerAccent,
    BadgeNeutral,
    BadgeAccent,
    BadgeDanger,
    ToggleSwitch,
    Splitter,
    ModalScrim,
    TextInputInvalid,
    TextError,
    IconOnSurface,
    IconOnPrimary,
    IconOnError,
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
    FocusVisible = 1U << 8U,
    Disabled = 1U << 3U,
    Checked = 1U << 4U,
    Selected = 1U << 5U,
    Open = 1U << 6U,
    Dragging = 1U << 7U,
    All = (1U << 9U) - 1U,
};

// The retained focus owner is shared by pointer, keyboard, gamepad and UIA.
// Modality only controls whether a focused node receives the visible focus
// treatment; it never creates a second focus store.
enum class UIInputModality : u8 {
    Pointer = 0,
    Keyboard,
    Gamepad,
    Accessibility,
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
    TabPaint = 1U << 13U,
    SplitterPaint = 1U << 14U,
    All = (1U << 15U) - 1U,
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

// Static dirty metadata for style/visual mutations (ADR 0023 / UI-STYLE-001).
// Call sites map property kinds through this table instead of inventing ad-hoc
// phase combinations. Flags are OR-able UIDirty bits; publication still goes
// through UIContext dirty-queue helpers (no bypass of capacity/atomicity).
enum class UIStylePropertyKind : u8 {
    // Box fill / chrome / ColorToken-backed fill / image tint/opacity.
    ColorOrOpacity = 0,
    // Runtime ColorToken reverse-path: paint snapshot only (no semantics phase).
    ColorToken = 1,
    // Font metrics / text style that affect intrinsic measure.
    TextStyle = 2,
    // Content placement inside an already-measured box.
    ContentAlignment = 3,
    // Hit policy only (hover clear may also request ColorOrOpacity separately).
    PointerHitPolicy = 4,
    // Full layout style (size/flex/padding/etc.).
    LayoutStyle = 5,
    // Single-line overflow policy. Truncation is resolved against the already
    // committed content box, so intrinsic measure is unchanged and only the
    // glyph run is rebuilt.
    TextOverflow = 6,
};

[[nodiscard]] constexpr UIDirty dirtyFlagsForStyleProperty(UIStylePropertyKind kind) noexcept
{
    switch (kind) {
    case UIStylePropertyKind::ColorOrOpacity:
        // Matches markPaintDirty: paint + semantics phase.
        return UIDirty::Paint | UIDirty::Semantics;
    case UIStylePropertyKind::ColorToken:
        return UIDirty::Paint;
    case UIStylePropertyKind::TextStyle:
        return UIDirty::Style | UIDirty::Measure | UIDirty::Arrange | UIDirty::Composite |
               UIDirty::HitTest | UIDirty::Paint | UIDirty::Semantics;
    case UIStylePropertyKind::ContentAlignment:
        // Intrinsic content placement: remeasure path + paint (current setter).
        return UIDirty::Style | UIDirty::Measure | UIDirty::Arrange | UIDirty::Composite |
               UIDirty::HitTest | UIDirty::Paint | UIDirty::Semantics;
    case UIStylePropertyKind::PointerHitPolicy:
        return UIDirty::HitTest;
    case UIStylePropertyKind::LayoutStyle:
        // Matches markLayoutDirtyBatch changed-node mask (incl. Semantics).
        return UIDirty::Style | UIDirty::Measure | UIDirty::Arrange | UIDirty::Composite |
               UIDirty::HitTest | UIDirty::Semantics;
    case UIStylePropertyKind::TextOverflow:
        // Paint only: the accessibility name keeps publishing the full text.
        return UIDirty::Paint;
    }
    return UIDirty::None;
}

[[nodiscard]] constexpr bool stylePropertyDirtiesLayout(UIStylePropertyKind kind) noexcept
{
    return hasDirty(dirtyFlagsForStyleProperty(kind),
                    UIDirty::Measure | UIDirty::Arrange | UIDirty::Style);
}

[[nodiscard]] constexpr bool stylePropertyDirtiesPaint(UIStylePropertyKind kind) noexcept
{
    return hasDirty(dirtyFlagsForStyleProperty(kind), UIDirty::Paint);
}

[[nodiscard]] constexpr bool stylePropertyDirtiesHit(UIStylePropertyKind kind) noexcept
{
    return hasDirty(dirtyFlagsForStyleProperty(kind), UIDirty::HitTest);
}

[[nodiscard]] constexpr bool stylePropertyDirtiesSemantics(UIStylePropertyKind kind) noexcept
{
    return hasDirty(dirtyFlagsForStyleProperty(kind), UIDirty::Semantics);
}

// Local paint overrides (BoxPaint / ImageTint / control paints) are paint-only.
[[nodiscard]] constexpr UIDirty dirtyFlagsForStyleOverride(UIStyleOverride property) noexcept
{
    if (property == UIStyleOverride::None) {
        return UIDirty::None;
    }
    if (hasStyleOverride(property, UIStyleOverride::TextStyle)) {
        return dirtyFlagsForStyleProperty(UIStylePropertyKind::TextStyle);
    }
    // Remaining override bits are paint chrome (box/control paints, image tint).
    return dirtyFlagsForStyleProperty(UIStylePropertyKind::ColorOrOpacity);
}

} // namespace Tina::UI
