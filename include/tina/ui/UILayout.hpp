#pragma once

#include <tina/core/base/Types.hpp>

#include <compare>

namespace Tina::UI {

enum class UILayoutLengthUnit : u8 {
    Px,
    Percent,
    Auto,
};

struct UILayoutLength final {
    UILayoutLengthUnit unit = UILayoutLengthUnit::Auto;
    float value = 0.0F;

    [[nodiscard]] static constexpr UILayoutLength Px(float logicalPixels) noexcept
    {
        return UILayoutLength{.unit = UILayoutLengthUnit::Px, .value = logicalPixels};
    }

    // Percent is expressed in percentage points, not 0..1. Sizes use 0..100;
    // Overlay offsets additionally allow -100..100. Validation and finite
    // checks happen when styles are normalized by the layout implementation.
    [[nodiscard]] static constexpr UILayoutLength Percent(float percent) noexcept
    {
        return UILayoutLength{.unit = UILayoutLengthUnit::Percent, .value = percent};
    }

    [[nodiscard]] static constexpr UILayoutLength Auto() noexcept
    {
        return UILayoutLength{};
    }

    [[nodiscard]] constexpr bool isAuto() const noexcept
    {
        return unit == UILayoutLengthUnit::Auto;
    }

    [[nodiscard]] constexpr bool isPx() const noexcept
    {
        return unit == UILayoutLengthUnit::Px;
    }

    [[nodiscard]] constexpr bool isPercent() const noexcept
    {
        return unit == UILayoutLengthUnit::Percent;
    }

    auto operator<=>(const UILayoutLength&) const = default;
};

struct UILogicalPoint final {
    float x = 0.0F;
    float y = 0.0F;

    auto operator<=>(const UILogicalPoint&) const = default;
};

struct UILogicalSize final {
    float width = 0.0F;
    float height = 0.0F;

    auto operator<=>(const UILogicalSize&) const = default;
};

struct UILogicalRect final {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] constexpr UILogicalPoint origin() const noexcept
    {
        return UILogicalPoint{.x = x, .y = y};
    }

    [[nodiscard]] constexpr UILogicalSize size() const noexcept
    {
        return UILogicalSize{.width = width, .height = height};
    }

    [[nodiscard]] constexpr float right() const noexcept
    {
        return x + width;
    }

    [[nodiscard]] constexpr float bottom() const noexcept
    {
        return y + height;
    }

    auto operator<=>(const UILogicalRect&) const = default;
};

struct UILayoutSizeSpec final {
    UILayoutLength width = UILayoutLength::Auto();
    UILayoutLength height = UILayoutLength::Auto();

    auto operator<=>(const UILayoutSizeSpec&) const = default;
};

struct UILayoutMinMaxSpec final {
    UILayoutLength minWidth = UILayoutLength::Auto();
    UILayoutLength minHeight = UILayoutLength::Auto();
    UILayoutLength maxWidth = UILayoutLength::Auto();
    UILayoutLength maxHeight = UILayoutLength::Auto();

    auto operator<=>(const UILayoutMinMaxSpec&) const = default;
};

struct UIEdgeSpacing final {
    // Logical pixels; normalization rejects non-finite or negative values.
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;

    [[nodiscard]] static constexpr UIEdgeSpacing All(float logicalPixels) noexcept
    {
        return UIEdgeSpacing{
            .left = logicalPixels,
            .top = logicalPixels,
            .right = logicalPixels,
            .bottom = logicalPixels,
        };
    }

    [[nodiscard]] static constexpr UIEdgeSpacing HorizontalVertical(
        float horizontalLogicalPixels,
        float verticalLogicalPixels) noexcept
    {
        return UIEdgeSpacing{
            .left = horizontalLogicalPixels,
            .top = verticalLogicalPixels,
            .right = horizontalLogicalPixels,
            .bottom = verticalLogicalPixels,
        };
    }

    auto operator<=>(const UIEdgeSpacing&) const = default;
};

struct UILayoutGap final {
    // Logical pixels; normalization rejects non-finite or negative values.
    float row = 0.0F;
    float column = 0.0F;

    [[nodiscard]] static constexpr UILayoutGap All(float logicalPixels) noexcept
    {
        return UILayoutGap{.row = logicalPixels, .column = logicalPixels};
    }

    auto operator<=>(const UILayoutGap&) const = default;
};

enum class UIFlexDirection : u8 {
    Row,
    Column,
};

enum class UIJustifyContent : u8 {
    Start,
    Center,
    End,
    SpaceBetween,
};

// Shared physical-axis alignment used by Flex containers, overlay placement,
// and retained content placement. Content alignment rejects Stretch because
// text and other intrinsic content cannot be stretched without a new measure.
enum class UIAxisAlignment : u8 {
    Start,
    Center,
    End,
    Stretch,
};

enum class UIAlignSelf : u8 {
    Auto,
    Start,
    Center,
    End,
    Stretch,
};

enum class UILayoutPlacement : u8 {
    Flow,
    Overlay,
};

enum class UIVisibility : u8 {
    Visible,
    Hidden,
    Collapsed,
};

// Properties interpreted by an element while arranging its children.
struct UIFlexContainerStyle final {
    UIFlexDirection direction = UIFlexDirection::Column;
    UIJustifyContent justifyContent = UIJustifyContent::Start;
    UIAxisAlignment alignItems = UIAxisAlignment::Stretch;
    UILayoutGap gap{};

    auto operator<=>(const UIFlexContainerStyle&) const = default;
};

// Properties interpreted by the parent Flex container for this element.
struct UIFlexItemStyle final {
    float grow = 0.0F;
    float shrink = 1.0F;
    UILayoutLength basis = UILayoutLength::Auto();
    UIAlignSelf alignSelf = UIAlignSelf::Auto;

    auto operator<=>(const UIFlexItemStyle&) const = default;
};

struct UIOverlayOffset final {
    UILayoutLength x = UILayoutLength::Px(0.0F);
    UILayoutLength y = UILayoutLength::Px(0.0F);

    auto operator<=>(const UIOverlayOffset&) const = default;
};

// Overlay deliberately models alignment plus a finite logical offset instead
// of CSS-like left/top/right/bottom coordinates. Use margin with Stretch to
// create edge insets. Popups keep their anchored placement policy internally.
struct UIOverlayStyle final {
    UIAxisAlignment horizontal = UIAxisAlignment::Start;
    UIAxisAlignment vertical = UIAxisAlignment::Start;
    UIOverlayOffset offset{};

    auto operator<=>(const UIOverlayStyle&) const = default;
};

struct UILayoutStyle final {
    UILayoutSizeSpec size{};
    UILayoutMinMaxSpec minMax{};
    UIEdgeSpacing margin{};
    UIEdgeSpacing padding{};
    UIFlexContainerStyle flexContainer{};
    UIFlexItemStyle flexItem{};
    UIOverlayStyle overlay{};
    UILayoutPlacement placement = UILayoutPlacement::Flow;
    UIVisibility visibility = UIVisibility::Visible;
    // Clips ordinary in-tree descendants to this element's axis-aligned border
    // box. Viewport-level Popup placement keeps its dedicated clip policy. This
    // does not create a rounded clip or alter this element's own paint clip.
    bool clipDescendants = false;

    auto operator<=>(const UILayoutStyle&) const = default;
};

} // namespace Tina::UI
