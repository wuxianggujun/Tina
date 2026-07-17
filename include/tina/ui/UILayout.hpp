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

    // Percent is expressed as 0..100, not 0..1. Validation and finite checks
    // happen when styles are normalized by the layout implementation.
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

struct UILayoutInsets final {
    UILayoutLength left = UILayoutLength::Auto();
    UILayoutLength top = UILayoutLength::Auto();
    UILayoutLength right = UILayoutLength::Auto();
    UILayoutLength bottom = UILayoutLength::Auto();

    [[nodiscard]] static constexpr UILayoutInsets All(UILayoutLength value) noexcept
    {
        return UILayoutInsets{.left = value, .top = value, .right = value, .bottom = value};
    }

    [[nodiscard]] static constexpr UILayoutInsets HorizontalVertical(
        UILayoutLength horizontal,
        UILayoutLength vertical) noexcept
    {
        return UILayoutInsets{
            .left = horizontal,
            .top = vertical,
            .right = horizontal,
            .bottom = vertical,
        };
    }

    auto operator<=>(const UILayoutInsets&) const = default;
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

enum class UIAlignItems : u8 {
    Start,
    Center,
    End,
    Stretch,
};

enum class UILayoutPositionMode : u8 {
    InFlow,
    AbsoluteOverlay,
};

enum class UIVisibility : u8 {
    Visible,
    Hidden,
    Collapsed,
};

struct UIFlexStyle final {
    UIFlexDirection direction = UIFlexDirection::Column;
    UIJustifyContent justify = UIJustifyContent::Start;
    UIAlignItems alignItems = UIAlignItems::Stretch;
    float grow = 0.0F;
    UILayoutGap gap{};

    auto operator<=>(const UIFlexStyle&) const = default;
};

struct UILayoutStyle final {
    UILayoutSizeSpec size{};
    UILayoutMinMaxSpec minMax{};
    UIEdgeSpacing margin{};
    UIEdgeSpacing padding{};
    UILayoutInsets absoluteInset{};
    UIFlexStyle flex{};
    UILayoutPositionMode position = UILayoutPositionMode::InFlow;
    UIVisibility visibility = UIVisibility::Visible;

    auto operator<=>(const UILayoutStyle&) const = default;
};

} // namespace Tina::UI
