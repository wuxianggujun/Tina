#pragma once

#include <tina/core/base/Types.hpp>

#include <array>
#include <compare>
#include <initializer_list>
#include <optional>

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

enum class UIFlexWrap : u8 {
    NoWrap,
    Wrap,
};

enum class UIContainerLayout : u8 {
    Flex,
    Grid,
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
    UIFlexWrap wrap = UIFlexWrap::NoWrap;
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

inline constexpr usize UIGridTrackCapacity = 8U;
inline constexpr u8 UIGridAutoIndex = 0xffU;

enum class UIGridTrackUnit : u8 {
    Px,
    Auto,
    Fraction,
};

struct UIGridTrack final {
    UIGridTrackUnit unit = UIGridTrackUnit::Auto;
    float value = 0.0F;

    [[nodiscard]] static constexpr UIGridTrack Px(
        float logicalPixels) noexcept
    {
        return UIGridTrack{
            .unit = UIGridTrackUnit::Px,
            .value = logicalPixels,
        };
    }

    [[nodiscard]] static constexpr UIGridTrack Auto() noexcept
    {
        return UIGridTrack{};
    }

    [[nodiscard]] static constexpr UIGridTrack Fr(
        float fraction = 1.0F) noexcept
    {
        return UIGridTrack{
            .unit = UIGridTrackUnit::Fraction,
            .value = fraction,
        };
    }

    auto operator<=>(const UIGridTrack&) const = default;
};

struct UIGridTrackList final {
    std::array<UIGridTrack, UIGridTrackCapacity> tracks{};
    u8 count = 0U;

    [[nodiscard]] static constexpr UIGridTrackList Of(
        std::initializer_list<UIGridTrack> values) noexcept
    {
        UIGridTrackList result{};
        if (values.size() > result.tracks.size())
        {
            result.count = UIGridAutoIndex;
            return result;
        }
        for (const UIGridTrack value : values)
        {
            result.tracks[result.count++] = value;
        }
        return result;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return count == 0U;
    }

    auto operator<=>(const UIGridTrackList&) const = default;
};

// Properties interpreted by a Grid container. Empty track lists create
// implicit Auto tracks as children are placed. Explicit and implicit tracks
// share the same fixed per-axis capacity.
struct UIGridContainerStyle final {
    UIGridTrackList columns{};
    UIGridTrackList rows{};
    UILayoutGap gap{};
    UIAxisAlignment justifyItems = UIAxisAlignment::Stretch;
    UIAxisAlignment alignItems = UIAxisAlignment::Stretch;

    auto operator<=>(const UIGridContainerStyle&) const = default;
};

// Zero-based row/column indices are interpreted by the parent Grid. Auto
// placement is row-major and skips occupied cells. Explicit items may overlap.
struct UIGridItemStyle final {
    u8 row = UIGridAutoIndex;
    u8 column = UIGridAutoIndex;
    u8 rowSpan = 1U;
    u8 columnSpan = 1U;
    UIAlignSelf justifySelf = UIAlignSelf::Auto;
    UIAlignSelf alignSelf = UIAlignSelf::Auto;

    auto operator<=>(const UIGridItemStyle&) const = default;
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

inline constexpr usize UIResponsiveLayoutRuleCapacity = 4U;

// Responsive overrides deliberately cover only structural layout decisions.
// Sizes, spacing, placement and item participation remain in the base style so
// a bounded rule cannot silently replace the element's full authored contract.
struct UIResponsiveLayoutOverrides final {
    std::optional<UIContainerLayout> containerLayout{};
    std::optional<UIFlexDirection> flexDirection{};
    std::optional<UIGridTrackList> gridColumns{};
    std::optional<UIGridTrackList> gridRows{};
    std::optional<UIVisibility> visibility{};

    auto operator<=>(const UIResponsiveLayoutOverrides&) const = default;
};

// Half-open parent-content-width interval [minParentWidth, maxParentWidth).
// Normalization requires finite, non-negative, ordered and non-overlapping
// intervals. No matching interval leaves the base UILayoutStyle unchanged.
struct UIResponsiveLayoutRule final {
    float minParentWidth = 0.0F;
    float maxParentWidth = 0.0F;
    UIResponsiveLayoutOverrides overrides{};

    auto operator<=>(const UIResponsiveLayoutRule&) const = default;
};

struct UIResponsiveLayoutRuleList final {
    std::array<UIResponsiveLayoutRule, UIResponsiveLayoutRuleCapacity> rules{};
    u8 count = 0U;

    [[nodiscard]] static constexpr UIResponsiveLayoutRuleList Of(
        std::initializer_list<UIResponsiveLayoutRule> values) noexcept
    {
        UIResponsiveLayoutRuleList result{};
        if (values.size() > result.rules.size())
        {
            result.count = UIGridAutoIndex;
            return result;
        }
        for (const UIResponsiveLayoutRule value : values)
        {
            result.rules[result.count++] = value;
        }
        return result;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return count == 0U;
    }

    auto operator<=>(const UIResponsiveLayoutRuleList&) const = default;
};

struct UILayoutStyle final {
    UILayoutSizeSpec size{};
    UILayoutMinMaxSpec minMax{};
    UIEdgeSpacing margin{};
    UIEdgeSpacing padding{};
    UIFlexContainerStyle flexContainer{};
    UIFlexItemStyle flexItem{};
    UIGridContainerStyle gridContainer{};
    UIGridItemStyle gridItem{};
    UIOverlayStyle overlay{};
    UIResponsiveLayoutRuleList responsiveRules{};
    UIContainerLayout containerLayout = UIContainerLayout::Flex;
    UILayoutPlacement placement = UILayoutPlacement::Flow;
    UIVisibility visibility = UIVisibility::Visible;
    // Clips ordinary in-tree descendants to this element's axis-aligned border
    // box. Viewport-level Popup placement keeps its dedicated clip policy. This
    // does not create a rounded clip or alter this element's own paint clip.
    bool clipDescendants = false;

    auto operator<=>(const UILayoutStyle&) const = default;
};

} // namespace Tina::UI
