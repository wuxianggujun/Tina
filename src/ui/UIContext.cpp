#include <tina/ui/UIContext.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/id/GenerationPool.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIDirty.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/text/UIGlyphAtlas.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include "detail/UIButtonActionRegistry.hpp"
#include "detail/UICommandPressLatch.hpp"
#include "detail/UIContextLifetimeControl.hpp"
#include "detail/UIDefaultActionPressState.hpp"
#include "detail/UIImeCompositionState.hpp"
#include "detail/UIRoutedPointerListenerRegistry.hpp"
#include "detail/UISliderChangeCallbackRegistry.hpp"
#include "detail/UITextStorage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <expected>
#include <initializer_list>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::UI {
namespace {

using NodeStorageId = Core::GenerationId<Detail::UINodeRegistryTag>;

inline constexpr u32 InvalidNodeIndex = NodeStorageId::InvalidIndex;

struct NormalizedCapacityConfig final {
    usize nodeCapacity = 0;
    usize rootCapacity = 0;
    usize dirtyQueueCapacity = 0;
    usize layoutSnapshotCapacity = 0;
    usize hitSnapshotCapacity = 0;
    usize paintSnapshotCapacity = 0;
    usize routePathCapacity = 0;
    usize routedPointerListenerCapacity = 0;
    usize buttonActionCapacity = 0;
    usize textByteCapacity = 0;
    bool applyDefaultProductChrome = true;
};

using TextByteAllocation = Detail::UITextStorage::Allocation;

struct WidgetTextState final {
    TextByteAllocation allocation{};
    u32 length = 0;
    UITextStyle style{};
    UITextMetrics metrics{};
    bool hasContent = false;
};

struct TextEditState final {
    UITextSelection selection{};
};

struct ProgressBarState final {
    float minValue = 0.0F;
    float maxValue = 1.0F;
    float value = 0.0F;
    UIProgressBarPaint paint{};
};

struct RadioButtonState final {
    UIRadioButtonPaint paint{};
    bool selected = false;
};

struct ScrollViewState final {
    UIScrollViewStyle style{};
    UIScrollViewPaint paint{};
    UIScrollOffset requestedOffset{};
    UIScrollViewMetrics committedMetrics{};
    UILogicalRect committedViewportRect{};
};

struct ScrollViewLayoutScratch final {
    UIScrollViewMetrics metrics{};
    UILogicalRect viewportRect{};
};

struct DropdownState final {
    UINodeId popup{};
    UINodeId selectedItem{};
    UIDropdownPaint paint{};
};

struct PopupState final {
    UIPopupStyle style{};
    UIPopupMetrics committedMetrics{};
    bool open = false;
};

struct PopupLayoutScratch final {
    UIPopupMetrics metrics{};
};

struct ListViewState final {
    UIListViewStyle style{};
    UIListViewPaint paint{};
    UIListViewDataSource dataSource{};
    UIListViewSelection selection{};
    UIListViewMetrics committedMetrics{};
    UILogicalRect committedViewportRect{};
    float requestedScrollOffset = 0.0F;
    u32 materializedItemCapacity = 0;
};

struct ListViewLayoutScratch final {
    UIListViewMetrics metrics{};
    UILogicalRect viewportRect{};
};

struct ListViewItemState final {
    UIListViewItemKey key = InvalidUIListViewItemKey;
    u64 logicalIndex = 0;
    bool bound = false;
    bool enabled = true;
    UIListViewItemKey committedKey = InvalidUIListViewItemKey;
    u64 committedLogicalIndex = 0;
    bool committedBound = false;
    bool committedEnabled = true;
};

struct TreeViewState final {
    UITreeViewStyle style{};
    UITreeViewPaint paint{};
    UITreeViewDataSource dataSource{};
    UITreeViewSelection selection{};
    UITreeViewMetrics committedMetrics{};
    UILogicalRect committedViewportRect{};
    float requestedScrollOffset = 0.0F;
    u32 materializedItemCapacity = 0;
};

struct TreeViewLayoutScratch final {
    UITreeViewMetrics metrics{};
    UILogicalRect viewportRect{};
};

struct TreeViewItemState final {
    UITreeViewItemKey key = InvalidUITreeViewItemKey;
    u64 logicalIndex = 0;
    u32 level = 0;
    bool bound = false;
    bool enabled = true;
    bool expandable = false;
    bool expanded = false;
    UITreeViewItemKey committedKey = InvalidUITreeViewItemKey;
    u64 committedLogicalIndex = 0;
    u32 committedLevel = 0;
    bool committedBound = false;
    bool committedEnabled = true;
    bool committedExpandable = false;
    bool committedExpanded = false;
};

inline constexpr u16 ThemeBindingBoxPaint = 1U << 0U;
inline constexpr u16 ThemeBindingTextStyle = 1U << 1U;
inline constexpr u16 ThemeBindingButtonPaint = 1U << 2U;
inline constexpr u16 ThemeBindingCheckboxPaint = 1U << 3U;
inline constexpr u16 ThemeBindingSliderPaint = 1U << 4U;
inline constexpr u16 ThemeBindingProgressBarPaint = 1U << 5U;
inline constexpr u16 ThemeBindingRadioButtonPaint = 1U << 6U;
inline constexpr u16 ThemeBindingScrollViewPaint = 1U << 7U;
inline constexpr u16 ThemeBindingDropdownPaint = 1U << 8U;
inline constexpr u16 ThemeBindingListViewPaint = 1U << 9U;
inline constexpr u16 ThemeBindingTreeViewPaint = 1U << 10U;

inline constexpr u8 ThemeDirtyPaint = 1U << 0U;
inline constexpr u8 ThemeDirtyLayoutSelf = 1U << 1U;
inline constexpr u8 ThemeDirtyLayoutAncestor = 1U << 2U;

[[nodiscard]] constexpr bool containsLineBreak(std::string_view text) noexcept
{
    return text.find('\r') != std::string_view::npos || text.find('\n') != std::string_view::npos;
}

[[nodiscard]] constexpr usize utf8ByteOffsetForCodepoint(std::string_view text, u32 codepointOffset) noexcept
{
    usize byteOffset = 0;
    u32 codepoint = 0;
    while (byteOffset < text.size() && codepoint < codepointOffset)
    {
        const auto first = static_cast<unsigned char>(text[byteOffset]);
        byteOffset += first <= 0x7FU ? 1U : (first & 0xE0U) == 0xC0U ? 2U : (first & 0xF0U) == 0xE0U ? 3U : 4U;
        ++codepoint;
    }
    return (std::min)(byteOffset, text.size());
}

struct NodeRecord final {
    u32 parentIndex = InvalidNodeIndex;
    u32 firstChildIndex = InvalidNodeIndex;
    u32 lastChildIndex = InvalidNodeIndex;
    u32 previousSiblingIndex = InvalidNodeIndex;
    u32 nextSiblingIndex = InvalidNodeIndex;
    u32 rootIndex = InvalidNodeIndex;
    u32 depth = 0;
    UIWidgetKind kind = UIWidgetKind::Panel;
};

static_assert(sizeof(NodeRecord) <= 48);
static_assert(std::is_nothrow_destructible_v<NodeRecord>);

using NodePool = Core::GenerationPool<NodeRecord, Detail::UINodeRegistryTag>;

struct LayoutPreparedInputs final {
    UIVisibility effectiveVisibility = UIVisibility::Visible;
    bool parentContentWidthDefinite = false;
    bool parentContentHeightDefinite = false;
    float parentContentWidth = 0.0F;
    float parentContentHeight = 0.0F;
    bool contentWidthDefinite = false;
    bool contentHeightDefinite = false;
    float contentWidth = 0.0F;
    float contentHeight = 0.0F;

    auto operator<=>(const LayoutPreparedInputs&) const = default;
};

struct LayoutScratchState final {
    UILogicalSize measuredSize{};
    UILogicalRect localRect{};
    UILogicalRect worldRect{};
    UILogicalRect effectiveClip{};
    // Clip inherited from the nearest clipping container. Ordinary containers
    // pass it through; ScrollView replaces it with its content viewport.
    UILogicalRect descendantClip{};
    UIVisibility effectiveVisibility = UIVisibility::Visible;
    bool inPopupSubtree = false;
    bool parentContentWidthDefinite = false;
    bool parentContentHeightDefinite = false;
    float parentContentWidth = 0.0F;
    float parentContentHeight = 0.0F;
    bool contentWidthDefinite = false;
    bool contentHeightDefinite = false;
    float contentWidth = 0.0F;
    float contentHeight = 0.0F;
    u32 layoutOrdinal = 0;
    u32 paintOrdinal = 0;
    // Prepare inputs remain stable after Arrange. The corresponding working
    // fields above are intentionally updated to final geometry during Arrange.
    LayoutPreparedInputs preparedInputs{};
};

struct LayoutPassStatistics final {
    usize passCount = 0;
    usize measuredNodeCount = 0;
    usize arrangedNodeCount = 0;
    usize percentMeasureFallbackCount = 0;
};

struct CommittedHitBuildResult final {
    usize targetCount = 0;
    u32 activeModalEntryIndex = InvalidUIHitEntryIndex;
};

inline constexpr u8 LayoutWorkMeasure = 1u << 0;
inline constexpr u8 LayoutWorkArrange = 1u << 1;
inline constexpr u8 LayoutWorkMeasureComplete = 1u << 2;
inline constexpr u8 LayoutWorkArrangeComplete = 1u << 3;

[[nodiscard]] constexpr bool hasLayoutWork(u8 work, u8 flag) noexcept
{
    return (work & flag) != 0;
}

[[nodiscard]] constexpr u8 layoutSubtreeCompletionMask(u8 work) noexcept
{
    u8 mask = 0;
    if ((work & LayoutWorkMeasure) != 0)
    {
        mask |= LayoutWorkMeasureComplete;
    }
    if ((work & LayoutWorkArrange) != 0)
    {
        mask |= LayoutWorkArrangeComplete;
    }
    return mask;
}

struct ResolvedLength final {
    bool hasValue = false;
    float value = 0.0F;
};

[[nodiscard]] Core::Error makeError(Core::ErrorCode code, std::string_view message,
                                    Core::SourceLocation location = Core::SourceLocation::current())
{
    return Core::Error{code, message, location};
}

[[nodiscard]] std::unexpected<Core::Error> fail(Core::ErrorCode code, std::string_view message,
                                                Core::SourceLocation location = Core::SourceLocation::current())
{
    return Core::failure(makeError(code, message, location));
}

[[nodiscard]] float normalizeFloat(float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] UIBoxPaint normalizeBoxPaint(UIBoxPaint paint) noexcept
{
    if (paint.solidFill.has_value() && paint.solidFill->color.alpha == 0)
    {
        paint.solidFill.reset();
    }
    if (!(std::isfinite(paint.borderWidth) && paint.borderWidth > 0.0F))
    {
        paint.borderWidth = 0.0F;
        paint.borderLight = {};
        paint.borderDark = {};
    }
    if (paint.borderLight.alpha == 0 && paint.borderDark.alpha == 0)
    {
        paint.borderWidth = 0.0F;
    }
    if (!(std::isfinite(paint.shadowOffsetX) && std::isfinite(paint.shadowOffsetY)) || paint.shadow.alpha == 0)
    {
        paint.shadow = {};
        paint.shadowOffsetX = 0.0F;
        paint.shadowOffsetY = 0.0F;
    }
    return paint;
}

[[nodiscard]] bool isValidScrollAxes(UIScrollAxes axes) noexcept
{
    return axes == UIScrollAxes::None || axes == UIScrollAxes::Horizontal || axes == UIScrollAxes::Vertical ||
           axes == UIScrollAxes::Both;
}

[[nodiscard]] bool isValidScrollBarVisibility(UIScrollBarVisibility visibility) noexcept
{
    return visibility == UIScrollBarVisibility::Auto || visibility == UIScrollBarVisibility::Always ||
           visibility == UIScrollBarVisibility::Hidden;
}

[[nodiscard]] Core::Result<UIScrollViewStyle> normalizeScrollViewStyle(UIScrollViewStyle style)
{
    if (!isValidScrollAxes(style.axes) || !isValidScrollBarVisibility(style.scrollBarVisibility) ||
        !(std::isfinite(style.wheelStep) && style.wheelStep > 0.0F))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI ScrollView axes/visibility must be valid and wheel step must be finite and positive");
    }
    style.wheelStep = normalizeFloat(style.wheelStep);
    return style;
}

[[nodiscard]] Core::Result<UIScrollViewPaint> normalizeScrollViewPaint(UIScrollViewPaint paint)
{
    if (!(std::isfinite(paint.thickness) && paint.thickness > 0.0F) ||
        !(std::isfinite(paint.minThumbExtent) && paint.minThumbExtent > 0.0F))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI ScrollView scrollbar thickness and minimum thumb extent must be finite and positive");
    }
    paint.thickness = normalizeFloat(paint.thickness);
    paint.minThumbExtent = normalizeFloat(paint.minThumbExtent);
    return paint;
}

[[nodiscard]] Core::Result<UIListViewCreateConfig> normalizeListViewCreateConfig(UIListViewCreateConfig config)
{
    if (config.materializedItemCapacity == 0 ||
        config.materializedItemCapacity > UIListViewCreateConfig::MaximumMaterializedItemCapacity)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI ListView materialized item capacity must be within the supported range");
    }
    return config;
}

[[nodiscard]] Core::Result<UIListViewStyle> normalizeListViewStyle(UIListViewStyle style)
{
    if (!(std::isfinite(style.rowHeight) && style.rowHeight > 0.0F) ||
        !isValidScrollBarVisibility(style.scrollBarVisibility) ||
        !(std::isfinite(style.wheelStep) && style.wheelStep > 0.0F))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI ListView row height/wheel step must be finite and positive and visibility must be valid");
    }
    style.rowHeight = normalizeFloat(style.rowHeight);
    style.wheelStep = normalizeFloat(style.wheelStep);
    return style;
}

[[nodiscard]] Core::Result<UIListViewPaint> normalizeListViewPaint(UIListViewPaint paint)
{
    auto scrollBar = normalizeScrollViewPaint(paint.scrollBar);
    if (!scrollBar)
    {
        return Core::failure(scrollBar.error());
    }
    paint.scrollBar = *scrollBar;
    return paint;
}

[[nodiscard]] constexpr bool isValidListViewScrollAlignment(UIListViewScrollAlignment alignment) noexcept
{
    return alignment == UIListViewScrollAlignment::Nearest || alignment == UIListViewScrollAlignment::Start ||
           alignment == UIListViewScrollAlignment::Center || alignment == UIListViewScrollAlignment::End;
}

[[nodiscard]] Core::Result<UITreeViewCreateConfig> normalizeTreeViewCreateConfig(UITreeViewCreateConfig config)
{
    if (config.materializedItemCapacity == 0 ||
        config.materializedItemCapacity > UITreeViewCreateConfig::MaximumMaterializedItemCapacity)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TreeView materialized item capacity must be within the supported range");
    }
    return config;
}

[[nodiscard]] Core::Result<UITreeViewStyle> normalizeTreeViewStyle(UITreeViewStyle style)
{
    if (!(std::isfinite(style.rowHeight) && style.rowHeight > 0.0F) ||
        !isValidScrollBarVisibility(style.scrollBarVisibility) ||
        !(std::isfinite(style.wheelStep) && style.wheelStep > 0.0F) ||
        !(std::isfinite(style.indentation) && style.indentation >= 0.0F) ||
        !(std::isfinite(style.disclosureExtent) && style.disclosureExtent > 0.0F) ||
        !(std::isfinite(style.disclosureGap) && style.disclosureGap >= 0.0F))
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI TreeView row, scroll, indentation, and disclosure metrics must be finite and valid");
    }
    style.rowHeight = normalizeFloat(style.rowHeight);
    style.wheelStep = normalizeFloat(style.wheelStep);
    style.indentation = normalizeFloat(style.indentation);
    style.disclosureExtent = normalizeFloat(style.disclosureExtent);
    style.disclosureGap = normalizeFloat(style.disclosureGap);
    return style;
}

[[nodiscard]] Core::Result<UITreeViewPaint> normalizeTreeViewPaint(UITreeViewPaint paint)
{
    auto scrollBar = normalizeScrollViewPaint(paint.scrollBar);
    if (!scrollBar)
    {
        return Core::failure(scrollBar.error());
    }
    paint.scrollBar = *scrollBar;
    return paint;
}

[[nodiscard]] constexpr bool isValidTreeViewScrollAlignment(UITreeViewScrollAlignment alignment) noexcept
{
    return alignment == UITreeViewScrollAlignment::Nearest || alignment == UITreeViewScrollAlignment::Start ||
           alignment == UITreeViewScrollAlignment::Center || alignment == UITreeViewScrollAlignment::End;
}

[[nodiscard]] Core::Result<UIScrollOffset> normalizeScrollOffset(UIScrollOffset offset)
{
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || offset.x < 0.0F || offset.y < 0.0F)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI ScrollView offset must contain finite non-negative coordinates");
    }
    offset.x = normalizeFloat(offset.x);
    offset.y = normalizeFloat(offset.y);
    return offset;
}

[[nodiscard]] bool isValidPopupPlacement(UIPopupPlacement placement) noexcept
{
    return placement == UIPopupPlacement::Auto || placement == UIPopupPlacement::Below ||
           placement == UIPopupPlacement::Above;
}

[[nodiscard]] Core::Result<UIPopupStyle> normalizePopupStyle(UIPopupStyle style)
{
    if (!isValidPopupPlacement(style.placement) || !std::isfinite(style.anchorGap) || style.anchorGap < 0.0F)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Popup placement must be valid and anchor gap must be finite and non-negative");
    }
    style.anchorGap = normalizeFloat(style.anchorGap);
    return style;
}

[[nodiscard]] Core::Result<UIDropdownPaint> normalizeDropdownPaint(UIDropdownPaint paint)
{
    if (!(std::isfinite(paint.indicatorWidth) && paint.indicatorWidth > 0.0F) ||
        !(std::isfinite(paint.indicatorHeight) && paint.indicatorHeight > 0.0F) ||
        !std::isfinite(paint.indicatorInset) || paint.indicatorInset < 0.0F)
    {
        return fail(UIErrorCode::InvalidControlValue,
                    "UI Dropdown indicator size must be finite and positive and inset must be non-negative");
    }
    paint.indicatorWidth = normalizeFloat(paint.indicatorWidth);
    paint.indicatorHeight = normalizeFloat(paint.indicatorHeight);
    paint.indicatorInset = normalizeFloat(paint.indicatorInset);
    return paint;
}

[[nodiscard]] usize countBoxChromePaintEntries(const UIBoxPaint& paint, const UILogicalRect& worldRect,
                                               bool hasResolvedFill) noexcept
{
    usize count = 0;
    if (hasResolvedFill)
    {
        ++count;
    }
    if (paint.shadow.alpha != 0 && (paint.shadowOffsetX != 0.0F || paint.shadowOffsetY != 0.0F) &&
        worldRect.width > 0.0F && worldRect.height > 0.0F)
    {
        ++count;
    }
    if (paint.borderWidth > 0.0F && worldRect.width > paint.borderWidth * 2.0F &&
        worldRect.height > paint.borderWidth * 2.0F)
    {
        if (paint.borderLight.alpha != 0)
        {
            count += 2; // top + left
        }
        if (paint.borderDark.alpha != 0)
        {
            count += 2; // bottom + right
        }
    }
    return count;
}

void appendBoxChromePaints(std::pmr::vector<UICommittedPaintEntry>& output, UINodeId node,
                           const UILogicalRect& worldRect, const UILogicalRect& effectiveClip, u32& nextPaintOrdinal,
                           const UIBoxPaint& paint, UIPremultipliedRgba8Color resolvedFill) noexcept
{
    if (paint.shadow.alpha != 0 && (paint.shadowOffsetX != 0.0F || paint.shadowOffsetY != 0.0F) &&
        worldRect.width > 0.0F && worldRect.height > 0.0F)
    {
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = normalizeFloat(worldRect.x + paint.shadowOffsetX),
                    .y = normalizeFloat(worldRect.y + paint.shadowOffsetY),
                    .width = worldRect.width,
                    .height = worldRect.height,
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = premultiply(paint.shadow),
        });
        ++nextPaintOrdinal;
    }
    if (!resolvedFill.isTransparent())
    {
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect = worldRect,
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = resolvedFill,
        });
        ++nextPaintOrdinal;
    }
    if (!(paint.borderWidth > 0.0F) || worldRect.width <= paint.borderWidth * 2.0F ||
        worldRect.height <= paint.borderWidth * 2.0F)
    {
        return;
    }
    const float bw = paint.borderWidth;
    if (paint.borderLight.alpha != 0)
    {
        const UIPremultipliedRgba8Color light = premultiply(paint.borderLight);
        // Top
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = worldRect.x,
                    .y = worldRect.y,
                    .width = worldRect.width,
                    .height = normalizeFloat(bw),
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = light,
        });
        ++nextPaintOrdinal;
        // Left
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = worldRect.x,
                    .y = normalizeFloat(worldRect.y + bw),
                    .width = normalizeFloat(bw),
                    .height = normalizeFloat(worldRect.height - bw),
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = light,
        });
        ++nextPaintOrdinal;
    }
    if (paint.borderDark.alpha != 0)
    {
        const UIPremultipliedRgba8Color dark = premultiply(paint.borderDark);
        // Bottom
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = worldRect.x,
                    .y = normalizeFloat(worldRect.y + worldRect.height - bw),
                    .width = worldRect.width,
                    .height = normalizeFloat(bw),
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = dark,
        });
        ++nextPaintOrdinal;
        // Right
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect =
                UILogicalRect{
                    .x = normalizeFloat(worldRect.x + worldRect.width - bw),
                    .y = worldRect.y,
                    .width = normalizeFloat(bw),
                    .height = normalizeFloat(worldRect.height - bw),
                },
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal,
            .solidFill = dark,
        });
        ++nextPaintOrdinal;
    }
}

[[nodiscard]] bool isFiniteNonNegative(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] Core::Status validateProductTheme(const UITheme& theme)
{
    if (!isFiniteNonNegative(theme.panelBorderWidth) || !std::isfinite(theme.panelShadowOffsetX) ||
        !std::isfinite(theme.panelShadowOffsetY) || !isFiniteNonNegative(theme.checkboxIndicatorInset) ||
        !isFiniteNonNegative(theme.radioSelectedInset) || !isFiniteNonNegative(theme.radioLabelGap) ||
        !isFiniteNonNegative(theme.sliderContentInset) || !isFiniteNonNegative(theme.sliderThumbWidth) ||
        !(std::isfinite(theme.buttonTextSize) && theme.buttonTextSize > 0.0F) ||
        !(std::isfinite(theme.bodyTextSize) && theme.bodyTextSize > 0.0F) ||
        !(std::isfinite(theme.titleTextSize) && theme.titleTextSize > 0.0F))
    {
        return fail(UIErrorCode::InvalidTheme,
                    "UI Theme metrics must be finite; sizes must be positive and insets non-negative");
    }
    return Core::success();
}

[[nodiscard]] Core::Status normalizeLayoutLength(UILayoutLength& length, bool allowAuto)
{
    switch (length.unit)
    {
    case UILayoutLengthUnit::Auto:
        if (!allowAuto)
        {
            return fail(UIErrorCode::InvalidLayout, "UI layout length cannot be Auto here");
        }
        length.value = 0.0F;
        return Core::success();
    case UILayoutLengthUnit::Px:
        if (!isFiniteNonNegative(length.value))
        {
            return fail(UIErrorCode::InvalidLayout, "UI layout length must be finite and non-negative");
        }
        length.value = normalizeFloat(length.value);
        return Core::success();
    case UILayoutLengthUnit::Percent:
        if (!isFiniteNonNegative(length.value) || length.value > 100.0F)
        {
            return fail(UIErrorCode::InvalidLayout, "UI layout percent must be finite and within 0..100");
        }
        length.value = normalizeFloat(length.value);
        return Core::success();
    }

    return fail(UIErrorCode::InvalidLayout, "UI layout length unit is invalid");
}

[[nodiscard]] Core::Status normalizeSpacing(float& value)
{
    if (!isFiniteNonNegative(value))
    {
        return fail(UIErrorCode::InvalidLayout, "UI layout spacing must be finite and non-negative");
    }
    value = normalizeFloat(value);
    return Core::success();
}

[[nodiscard]] Core::Status normalizeEdgeSpacing(UIEdgeSpacing& spacing)
{
    if (Core::Status status = normalizeSpacing(spacing.left); !status)
    {
        return status;
    }
    if (Core::Status status = normalizeSpacing(spacing.top); !status)
    {
        return status;
    }
    if (Core::Status status = normalizeSpacing(spacing.right); !status)
    {
        return status;
    }
    return normalizeSpacing(spacing.bottom);
}

[[nodiscard]] bool isValidFlexDirection(UIFlexDirection value) noexcept
{
    return value == UIFlexDirection::Row || value == UIFlexDirection::Column;
}

[[nodiscard]] bool isValidJustifyContent(UIJustifyContent value) noexcept
{
    return value == UIJustifyContent::Start || value == UIJustifyContent::Center || value == UIJustifyContent::End ||
           value == UIJustifyContent::SpaceBetween;
}

[[nodiscard]] bool isValidAlignItems(UIAlignItems value) noexcept
{
    return value == UIAlignItems::Start || value == UIAlignItems::Center || value == UIAlignItems::End ||
           value == UIAlignItems::Stretch;
}

[[nodiscard]] bool isValidPositionMode(UILayoutPositionMode value) noexcept
{
    return value == UILayoutPositionMode::InFlow || value == UILayoutPositionMode::AbsoluteOverlay;
}

[[nodiscard]] bool isValidVisibility(UIVisibility value) noexcept
{
    return value == UIVisibility::Visible || value == UIVisibility::Hidden || value == UIVisibility::Collapsed;
}

[[nodiscard]] Core::Result<UILayoutStyle> normalizeLayoutStyle(UILayoutStyle style)
{
    if (Core::Status status = normalizeLayoutLength(style.size.width, true); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.size.height, true); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.minMax.minWidth, true); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.minMax.minHeight, true); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.minMax.maxWidth, true); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.minMax.maxHeight, true); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeEdgeSpacing(style.margin); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeEdgeSpacing(style.padding); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.absoluteInset.left, true); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.absoluteInset.top, true); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.absoluteInset.right, true); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.absoluteInset.bottom, true); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeSpacing(style.flex.grow); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeSpacing(style.flex.gap.row); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeSpacing(style.flex.gap.column); !status)
    {
        return Core::failure(status.error());
    }
    if (!isValidFlexDirection(style.flex.direction) || !isValidJustifyContent(style.flex.justify) ||
        !isValidAlignItems(style.flex.alignItems) || !isValidPositionMode(style.position) ||
        !isValidVisibility(style.visibility))
    {
        return fail(UIErrorCode::InvalidLayout, "UI layout enum value is invalid");
    }

    return style;
}

[[nodiscard]] ResolvedLength resolveLength(UILayoutLength length, bool basisDefinite, float basis,
                                           LayoutPassStatistics& statistics) noexcept
{
    if (length.unit == UILayoutLengthUnit::Px)
    {
        return ResolvedLength{.hasValue = true, .value = length.value};
    }
    if (length.unit == UILayoutLengthUnit::Percent)
    {
        if (basisDefinite && isFiniteNonNegative(basis))
        {
            return ResolvedLength{
                .hasValue = true,
                .value = normalizeFloat(basis * (length.value * 0.01F)),
            };
        }
        ++statistics.percentMeasureFallbackCount;
    }
    return {};
}

[[nodiscard]] ResolvedLength resolveLengthNoFallbackCount(UILayoutLength length, bool basisDefinite,
                                                          float basis) noexcept
{
    if (length.unit == UILayoutLengthUnit::Px)
    {
        return ResolvedLength{.hasValue = true, .value = length.value};
    }
    if (length.unit == UILayoutLengthUnit::Percent && basisDefinite && isFiniteNonNegative(basis))
    {
        return ResolvedLength{
            .hasValue = true,
            .value = normalizeFloat(basis * (length.value * 0.01F)),
        };
    }
    return {};
}

[[nodiscard]] float clampWithMinMax(float value, UILayoutLength minLength, UILayoutLength maxLength, bool basisDefinite,
                                    float basis, LayoutPassStatistics& statistics) noexcept
{
    const ResolvedLength minValue = resolveLength(minLength, basisDefinite, basis, statistics);
    ResolvedLength maxValue = resolveLength(maxLength, basisDefinite, basis, statistics);
    if (minValue.hasValue && maxValue.hasValue && maxValue.value < minValue.value)
    {
        maxValue.value = minValue.value;
    }
    if (maxValue.hasValue)
    {
        value = (std::min)(value, maxValue.value);
    }
    if (minValue.hasValue)
    {
        value = (std::max)(value, minValue.value);
    }
    return normalizeFloat((std::max)(0.0F, value));
}

[[nodiscard]] float horizontalMargin(const UIEdgeSpacing& margin) noexcept
{
    return margin.left + margin.right;
}

[[nodiscard]] float verticalMargin(const UIEdgeSpacing& margin) noexcept
{
    return margin.top + margin.bottom;
}

[[nodiscard]] UILogicalRect intersectRects(UILogicalRect first, UILogicalRect second) noexcept
{
    const float left = (std::max)(first.x, second.x);
    const float top = (std::max)(first.y, second.y);
    const float right = (std::min)(first.right(), second.right());
    const float bottom = (std::min)(first.bottom(), second.bottom());
    return UILogicalRect{
        .x = normalizeFloat(left),
        .y = normalizeFloat(top),
        .width = normalizeFloat((std::max)(0.0F, right - left)),
        .height = normalizeFloat((std::max)(0.0F, bottom - top)),
    };
}

[[nodiscard]] UIVisibility combineVisibility(UIVisibility parent, UIVisibility local) noexcept
{
    if (parent == UIVisibility::Collapsed || local == UIVisibility::Collapsed)
    {
        return UIVisibility::Collapsed;
    }
    if (parent == UIVisibility::Hidden || local == UIVisibility::Hidden)
    {
        return UIVisibility::Hidden;
    }
    return UIVisibility::Visible;
}

[[nodiscard]] Core::Result<NormalizedCapacityConfig> normalizeCapacity(UIContextCapacityConfig config)
{
    if (Core::Status status = validateUIContextCapacityConfig(config); !status)
    {
        return Core::failure(status.error());
    }
    const usize dirtyQueueCapacity = config.dirtyQueueCapacity == 0 ? config.nodeCapacity : config.dirtyQueueCapacity;
    const usize layoutSnapshotCapacity =
        config.layoutSnapshotCapacity == 0 ? config.nodeCapacity : config.layoutSnapshotCapacity;
    const usize hitSnapshotCapacity =
        config.hitSnapshotCapacity == 0 ? config.nodeCapacity : config.hitSnapshotCapacity;
    const usize paintSnapshotCapacity =
        config.paintSnapshotCapacity == 0 ? config.nodeCapacity : config.paintSnapshotCapacity;
    const usize routePathCapacity = config.routePathCapacity == 0 ? config.nodeCapacity : config.routePathCapacity;
    const usize routedPointerListenerCapacity =
        config.routedPointerListenerCapacity == 0 ? config.nodeCapacity : config.routedPointerListenerCapacity;
    const usize buttonActionCapacity =
        config.buttonActionCapacity == 0 ? config.nodeCapacity : config.buttonActionCapacity;
    const usize textByteCapacity =
        config.textByteCapacity == 0 ? UIContextCapacityConfig::DefaultTextByteCapacity : config.textByteCapacity;
    return NormalizedCapacityConfig{
        .nodeCapacity = config.nodeCapacity,
        .rootCapacity = config.rootCapacity,
        .dirtyQueueCapacity = dirtyQueueCapacity,
        .layoutSnapshotCapacity = layoutSnapshotCapacity,
        .hitSnapshotCapacity = hitSnapshotCapacity,
        .paintSnapshotCapacity = paintSnapshotCapacity,
        .routePathCapacity = routePathCapacity,
        .routedPointerListenerCapacity = routedPointerListenerCapacity,
        .buttonActionCapacity = buttonActionCapacity,
        .textByteCapacity = textByteCapacity,
        .applyDefaultProductChrome = config.applyDefaultProductChrome,
    };
}

[[nodiscard]] bool sameNode(UINodeId left, UINodeId right) noexcept
{
    return left == right;
}

[[nodiscard]] bool isValidPointerHitPolicy(UIPointerHitPolicy policy) noexcept
{
    switch (policy)
    {
    case UIPointerHitPolicy::Ignore:
    case UIPointerHitPolicy::Targetable:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidFocusScopeMode(UIFocusScopeMode mode) noexcept
{
    return mode == UIFocusScopeMode::None || mode == UIFocusScopeMode::Contain;
}

[[nodiscard]] bool isValidRoutedPointerEventKind(UIRoutedPointerEventKind kind) noexcept
{
    switch (kind)
    {
    case UIRoutedPointerEventKind::Move:
    case UIRoutedPointerEventKind::ButtonDown:
    case UIRoutedPointerEventKind::ButtonUp:
    case UIRoutedPointerEventKind::Wheel:
    case UIRoutedPointerEventKind::PointerCancel:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidEventPhaseMask(UIEventPhaseMask phases) noexcept
{
    const auto bits = static_cast<u8>(phases);
    const auto allBits = static_cast<u8>(UIEventPhaseMask::All);
    return bits != 0 && (bits & static_cast<u8>(~allBits)) == 0;
}

[[nodiscard]] UIEventPhaseMask phaseMaskFor(UIEventPhase phase) noexcept
{
    switch (phase)
    {
    case UIEventPhase::Capture:
        return UIEventPhaseMask::Capture;
    case UIEventPhase::Target:
        return UIEventPhaseMask::Target;
    case UIEventPhase::Bubble:
        return UIEventPhaseMask::Bubble;
    }
    return UIEventPhaseMask::None;
}

[[nodiscard]] bool containsPointHalfOpen(UILogicalRect rect, UILogicalPoint point) noexcept
{
    return rect.width > 0.0F && rect.height > 0.0F && point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.right() && point.y < rect.bottom();
}

struct ScrollBarGeometry final {
    UILogicalRect track{};
    UILogicalRect thumb{};
    bool visible = false;
};

struct ScrollBarPointerHit final {
    UINodeId scrollView{};
    UIScrollAxes axis = UIScrollAxes::None;
    ScrollBarGeometry geometry{};
    bool thumb = false;

    [[nodiscard]] bool hasValue() const noexcept
    {
        return scrollView.hasValue() && axis != UIScrollAxes::None;
    }
};

[[nodiscard]] ScrollBarGeometry makeScrollBarGeometry(const UIScrollViewMetrics& metrics,
                                                      UILogicalRect viewportRect,
                                                      const UIScrollViewPaint& paint,
                                                      UIScrollAxes axis) noexcept
{
    const bool horizontal = axis == UIScrollAxes::Horizontal;
    const bool visible = horizontal ? metrics.horizontalScrollBarVisible : metrics.verticalScrollBarVisible;
    if (!visible)
    {
        return {};
    }

    const float viewportExtent = horizontal ? metrics.viewportSize.width : metrics.viewportSize.height;
    const float contentExtent = horizontal ? metrics.contentSize.width : metrics.contentSize.height;
    const float offset = horizontal ? metrics.offset.x : metrics.offset.y;
    const UILogicalRect track = horizontal
                                    ? UILogicalRect{
                                          .x = viewportRect.x,
                                          .y = normalizeFloat(viewportRect.bottom()),
                                          .width = viewportRect.width,
                                          .height = paint.thickness,
                                      }
                                    : UILogicalRect{
                                          .x = normalizeFloat(viewportRect.right()),
                                          .y = viewportRect.y,
                                          .width = paint.thickness,
                                          .height = viewportRect.height,
                                      };
    const float trackExtent = horizontal ? track.width : track.height;
    if (!(trackExtent > 0.0F))
    {
        return ScrollBarGeometry{.track = track, .visible = true};
    }
    const float proportionalExtent = contentExtent > 0.0F ? trackExtent * viewportExtent / contentExtent : trackExtent;
    const float thumbExtent = normalizeFloat((std::clamp)(proportionalExtent, (std::min)(paint.minThumbExtent, trackExtent),
                                                          trackExtent));
    const float maxOffset = (std::max)(0.0F, contentExtent - viewportExtent);
    const float travel = (std::max)(0.0F, trackExtent - thumbExtent);
    const float thumbStart = maxOffset > 0.0F ? travel * ((std::clamp)(offset, 0.0F, maxOffset) / maxOffset) : 0.0F;
    const UILogicalRect thumb = horizontal
                                    ? UILogicalRect{
                                          .x = normalizeFloat(track.x + thumbStart),
                                          .y = track.y,
                                          .width = thumbExtent,
                                          .height = track.height,
                                      }
                                    : UILogicalRect{
                                          .x = track.x,
                                          .y = normalizeFloat(track.y + thumbStart),
                                          .width = track.width,
                                          .height = thumbExtent,
                                      };
    return ScrollBarGeometry{.track = track, .thumb = thumb, .visible = true};
}

[[nodiscard]] ScrollBarGeometry makeListViewScrollBarGeometry(const UIListViewMetrics& metrics,
                                                              UILogicalRect viewportRect,
                                                              const UIScrollViewPaint& paint) noexcept
{
    return makeScrollBarGeometry(
        UIScrollViewMetrics{
            .offset = {.x = 0.0F, .y = metrics.scrollOffset},
            .viewportSize = metrics.viewportSize,
            .contentSize = metrics.contentSize,
            .horizontalScrollBarVisible = false,
            .verticalScrollBarVisible = metrics.verticalScrollBarVisible,
        },
        viewportRect, paint, UIScrollAxes::Vertical);
}

[[nodiscard]] ScrollBarGeometry makeTreeViewScrollBarGeometry(const UITreeViewMetrics& metrics,
                                                              UILogicalRect viewportRect,
                                                              const UIScrollViewPaint& paint) noexcept
{
    return makeScrollBarGeometry(
        UIScrollViewMetrics{
            .offset = {.x = 0.0F, .y = metrics.scrollOffset},
            .viewportSize = metrics.viewportSize,
            .contentSize = metrics.contentSize,
            .horizontalScrollBarVisible = false,
            .verticalScrollBarVisible = metrics.verticalScrollBarVisible,
        },
        viewportRect, paint, UIScrollAxes::Vertical);
}

[[nodiscard]] UILogicalRect makeTreeViewDisclosureRect(UILogicalRect rowRect, const UITreeViewStyle& style,
                                                       u32 level) noexcept
{
    const double logicalX =
        static_cast<double>(rowRect.x) + 8.0 + static_cast<double>(level) * static_cast<double>(style.indentation);
    const float extent = normalizeFloat((std::min)(style.disclosureExtent, rowRect.height));
    const float x = logicalX >= static_cast<double>((std::numeric_limits<float>::max)())
                        ? (std::numeric_limits<float>::max)()
                        : normalizeFloat(static_cast<float>(logicalX));
    return UILogicalRect{
        .x = x,
        .y = normalizeFloat(rowRect.y + (rowRect.height - extent) * 0.5F),
        .width = extent,
        .height = extent,
    };
}

} // namespace

struct UIContext::Impl final {
    Platform::WindowId ownerWindow{};
    UIContextCapacityConfig capacityConfig{};
    std::thread::id ownerThreadId{};
    // Active product theme used when create* installs default control chrome.
    // Not a global singleton; one copy per UIContext. Local set*Paint overrides
    // remain on the node after create.
    UITheme productTheme = makeDefaultProductTheme();
    std::shared_ptr<Detail::UIContextLifetimeControl> lifetime;
    NodePool nodes;
    std::pmr::vector<UINodeId> idsByIndex;
    std::pmr::vector<UILayoutStyle> layoutStylesByIndex;
    std::pmr::vector<UIPointerHitPolicy> pointerHitPoliciesByIndex;
    // Enabled state is index-aligned with the node pool. A byte keeps the
    // side-array deterministic and avoids introducing a packed-bit mutation
    // path for a frequently queried interaction property.
    std::pmr::vector<u8> enabledByNodeIndex;
    std::pmr::vector<UIFocusScopeMode> focusScopeModesByNodeIndex;
    // A Modal stores the focus that should be restored when that committed
    // scope stops being topmost. Storage is fixed and index-aligned.
    std::pmr::vector<UINodeId> focusRestoreByNodeIndex;
    std::pmr::vector<UIBoxPaint> boxPaintsByIndex;
    std::pmr::vector<UIButtonPaint> buttonPaintsByNodeIndex;
    // Each bit tracks one property that still inherits product chrome. Theme
    // staging scratch is index-aligned and allocated once during Create().
    std::pmr::vector<u16> themeBindingsByNodeIndex;
    std::pmr::vector<u8> themeDirtyScratchByNodeIndex;
    std::pmr::vector<UITextMetrics> themeTextMetricsScratchByNodeIndex;
    std::pmr::vector<UIPremultipliedRgba8Color> localSolidFillCacheByIndex;
    std::pmr::vector<UIPremultipliedRgba8Color> localTextColorCacheByIndex;
    std::pmr::vector<WidgetTextState> textStatesByIndex;
    std::pmr::vector<TextEditState> textEditStatesByNodeIndex;
    std::pmr::vector<ProgressBarState> progressBarStatesByNodeIndex;
    std::pmr::vector<RadioButtonState> radioButtonStatesByNodeIndex;
    std::pmr::vector<ScrollViewState> scrollViewStatesByNodeIndex;
    std::pmr::vector<ScrollViewLayoutScratch> scrollViewLayoutScratchByNodeIndex;
    std::pmr::vector<DropdownState> dropdownStatesByNodeIndex;
    std::pmr::vector<PopupState> popupStatesByNodeIndex;
    std::pmr::vector<PopupLayoutScratch> popupLayoutScratchByNodeIndex;
    std::pmr::vector<ListViewState> listViewStatesByNodeIndex;
    std::pmr::vector<ListViewLayoutScratch> listViewLayoutScratchByNodeIndex;
    std::pmr::vector<ListViewItemState> listViewItemStatesByNodeIndex;
    std::pmr::vector<TreeViewState> treeViewStatesByNodeIndex;
    std::pmr::vector<TreeViewLayoutScratch> treeViewLayoutScratchByNodeIndex;
    std::pmr::vector<TreeViewItemState> treeViewItemStatesByNodeIndex;
    Detail::UITextStorage textStorage;
    std::unique_ptr<IUITextRasterizer> textRasterizer;
    UIFontFaceId textFace{};
    std::unique_ptr<UIGlyphAtlas> glyphAtlas;
    std::pmr::vector<UIDirty> dirtyByIndex;
    std::pmr::vector<u8> dirtyQueuedByIndex;
    // Pointer routes reserve the queue entries needed by their post-dispatch
    // state transition before invoking user listeners. A listener may still
    // dirty one of the reserved nodes, but unrelated mutations cannot steal
    // the reserved capacity and make the route fail half-way through.
    std::pmr::vector<u8> dirtyReservedByIndex;
    std::pmr::vector<UINodeId> dirtyQueue;
    std::pmr::vector<UINodeId> routeDirtyReservationScratch;
    std::pmr::vector<u8> routeDirtyReservationCandidateByIndex;
    std::pmr::vector<LayoutScratchState> layoutScratchByIndex;
    std::pmr::vector<u8> layoutWorkByIndex;
    std::pmr::vector<u32> layoutOrderScratch;
    std::pmr::vector<u32> hitEntryIndexByNodeIndex;
    Detail::UIRoutedPointerListenerRegistry routedPointerListenerRegistry;
    std::pmr::vector<u32> routePathScratch;
    // PointerCancel may be synthesized by a mutation performed from inside a
    // routed callback, so it cannot reuse the outer dispatch ancestry scratch.
    std::pmr::vector<u32> pointerCancelRoutePathScratch;
    Detail::UIButtonActionRegistry buttonActionRegistry;
    Detail::UIDefaultActionPressState defaultActionPressState;
    // M11-C0: Checkbox checked bit (index-aligned with nodes; false for non-Checkbox).
    std::pmr::vector<u8> checkboxCheckedByNodeIndex;
    std::pmr::vector<UICheckboxPaint> checkboxPaintsByNodeIndex;
    // M11-C1: Slider range/value/callback (index-aligned; default 0..1).
    struct SliderState final {
        float minValue = 0.0F;
        float maxValue = 1.0F;
        float step = 0.0F;
        float value = 0.0F;
        UISliderPaint paint{};
    };
    std::pmr::vector<SliderState> sliderStatesByNodeIndex;
    Detail::UISliderChangeCallbackRegistry sliderChangeCallbackRegistry;
    std::array<std::pmr::vector<UICommittedNodeEntry>, 2> committedBuffers;
    std::array<std::pmr::vector<UICommittedLayoutEntry>, 2> committedLayoutBuffers;
    std::array<std::pmr::vector<UICommittedHitEntry>, 2> committedHitBuffers;
    std::array<std::pmr::vector<UICommittedPaintEntry>, 2> committedPaintBuffers;
    std::array<std::pmr::vector<UISemanticsEntry>, 2> committedSemanticsBuffers;
    // Semantics entries borrow from the published buffer's private text copy;
    // mutating the live widget text arena must not rewrite an older snapshot.
    std::array<std::pmr::vector<char>, 2> committedSemanticsTextBuffers;
    std::vector<UINodeId> deferredRootDestroyBuffer;
    std::vector<Detail::DeferredRoutedPointerListenerRelease> deferredRoutedPointerListenerReleaseBuffer;
    usize publishedBufferIndex = 0;
    usize publishedLayoutBufferIndex = 0;
    usize publishedHitBufferIndex = 0;
    usize publishedPaintBufferIndex = 0;
    u64 committedRevision = 0;
    u64 committedLayoutRevision = 0;
    u64 committedLayoutStructureRevision = 0;
    u64 committedHitRevision = 0;
    u64 committedHitStructureRevision = 0;
    u64 committedHitLayoutRevision = 0;
    u64 committedHitPaintOrderRevision = 0;
    u64 committedPaintRevision = 0;
    u64 committedPaintStructureRevision = 0;
    u64 committedPaintLayoutRevision = 0;
    u64 committedPaintOrderRevision = 0;
    UILogicalSize committedPaintViewportSize{};
    u64 committedSemanticsRevision = 0;
    u64 committedSemanticsStructureRevision = 0;
    u64 committedSemanticsLayoutRevision = 0;
    UILogicalSize committedSemanticsViewportSize{};
    usize publishedSemanticsBufferIndex = 0;
    UILogicalSize committedViewportSize{};
    bool hasCommittedViewport = false;
    usize liveRootCount = 0;
    u32 firstRootIndex = InvalidNodeIndex;
    u32 lastRootIndex = InvalidNodeIndex;
    // Single phase-level dirty truth. Node-level dirtyByIndex remains the
    // incremental work queue; these bits only say which published snapshots
    // still need a successful commit.
    static constexpr UIDirty PhaseStructure = UIDirty::Structure;
    static constexpr UIDirty PhaseLayout = UIDirty::Measure;
    static constexpr UIDirty PhaseHit = UIDirty::HitTest;
    static constexpr UIDirty PhasePaint = UIDirty::Paint;
    static constexpr UIDirty PhaseSemantics = UIDirty::Semantics;
    UIDirty phaseDirty = UIDirty::None;
    // A failed candidate may have partially mutated layout scratch. The next
    // layout attempt must rebuild from scratch before reuse is enabled again.
    bool layoutReuseCacheValid = false;
    bool layoutReuseInProgress = false;
    LayoutPassStatistics lastLayoutPass{};
    usize dirtyQueueHighWater = 0;
    usize dirtyQueueReservationCount = 0;
    usize committedHitTargetCount = 0;
    u32 committedActiveModalEntryIndex = InvalidUIHitEntryIndex;
    usize lastHitRebuildCount = 0;
    usize lastPaintCacheRebuildCount = 0;
    usize lastPaintSnapshotRebuildCount = 0;
    usize routeDispatchDepth = 0;
    u64 buttonRouteSerial = 0;
    u64 accessibilityActionSequence = 0;
    UINodeId armedPrimaryButton{};
    bool armedPrimaryButtonPressed = false;
    UINodeId hoveredPrimaryButton{};
    // M11-C1: exclusive Primary drag capture for Slider (clears Button arm).
    UINodeId armedSlider{};
    UINodeId armedScrollView{};
    UIScrollAxes armedScrollAxis = UIScrollAxes::None;
    float scrollDragGrabOffset = 0.0F;
    bool scrollThumbDragActive = false;
    UINodeId armedTextEdit{};
    UINodeId capturedPointerNode{};
    UIPointerInputEvent lastPointerInput{};
    bool hasLastPointerInput = false;
    bool pointerCancelDispatchInProgress = false;
    UINodeId activeModalNode{};
    UINodeId activePopupNode{};
    Detail::UICommandPressLatch<UIDropdownCommand, UIDropdownCommand::ExitNext>
        dropdownCommandPressLatch;
    Detail::UICommandPressLatch<UIListViewCommand, UIListViewCommand::Activate>
        listViewCommandPressLatch;
    Detail::UICommandPressLatch<UITreeViewCommand, UITreeViewCommand::Activate>
        treeViewCommandPressLatch;
    bool armedTreeDisclosure = false;
    bool popupDismissPointerBarrierActive = false;
    UINodeId pendingDestroyedModalRestoreFocus{};
    bool hasPendingDestroyedModalRestoreFocus = false;
    // Last Button that received Primary Pointer arm. Keyboard/Gamepad Accept
    // activates this node without requiring a live pointer press.
    UINodeId defaultActionFocusButton{};
    // Focused single-line editor that receives keyboard and IME input.
    UINodeId textInputFocus{};
    Detail::UIImeCompositionState imeComposition;

    Impl(Platform::WindowId owner, UIContextCapacityConfig capacities, std::thread::id threadId,
         std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl, NodePool&& nodePool,
         std::pmr::memory_resource& resource)
        : ownerWindow(owner), capacityConfig(capacities), ownerThreadId(threadId), lifetime(std::move(lifetimeControl)),
          nodes(std::move(nodePool)), idsByIndex(&resource), layoutStylesByIndex(&resource),
          pointerHitPoliciesByIndex(&resource), enabledByNodeIndex(&resource), focusScopeModesByNodeIndex(&resource),
          focusRestoreByNodeIndex(&resource), boxPaintsByIndex(&resource), buttonPaintsByNodeIndex(&resource),
          themeBindingsByNodeIndex(&resource), themeDirtyScratchByNodeIndex(&resource),
          themeTextMetricsScratchByNodeIndex(&resource), localSolidFillCacheByIndex(&resource),
          localTextColorCacheByIndex(&resource), textStatesByIndex(&resource), textEditStatesByNodeIndex(&resource),
          progressBarStatesByNodeIndex(&resource), radioButtonStatesByNodeIndex(&resource),
          scrollViewStatesByNodeIndex(&resource), scrollViewLayoutScratchByNodeIndex(&resource),
          dropdownStatesByNodeIndex(&resource), popupStatesByNodeIndex(&resource),
          popupLayoutScratchByNodeIndex(&resource), listViewStatesByNodeIndex(&resource),
          listViewLayoutScratchByNodeIndex(&resource), listViewItemStatesByNodeIndex(&resource),
          treeViewStatesByNodeIndex(&resource), treeViewLayoutScratchByNodeIndex(&resource),
          treeViewItemStatesByNodeIndex(&resource),
          textStorage(capacities.textByteCapacity, capacities.nodeCapacity, resource),
          dirtyByIndex(&resource), dirtyQueuedByIndex(&resource), dirtyReservedByIndex(&resource),
          dirtyQueue(&resource), routeDirtyReservationScratch(&resource),
          routeDirtyReservationCandidateByIndex(&resource), layoutScratchByIndex(&resource),
          layoutWorkByIndex(&resource), layoutOrderScratch(&resource), hitEntryIndexByNodeIndex(&resource),
          routedPointerListenerRegistry(capacities.nodeCapacity, capacities.routedPointerListenerCapacity, resource),
          routePathScratch(&resource), pointerCancelRoutePathScratch(&resource),
          buttonActionRegistry(capacities.nodeCapacity, capacities.buttonActionCapacity, resource),
          defaultActionPressState(owner),
          checkboxCheckedByNodeIndex(&resource), checkboxPaintsByNodeIndex(&resource),
          sliderStatesByNodeIndex(&resource), sliderChangeCallbackRegistry(capacities.nodeCapacity, resource),
          committedBuffers{std::pmr::vector<UICommittedNodeEntry>(&resource),
                                                                   std::pmr::vector<UICommittedNodeEntry>(&resource)},
          committedLayoutBuffers{std::pmr::vector<UICommittedLayoutEntry>(&resource),
                                 std::pmr::vector<UICommittedLayoutEntry>(&resource)},
          committedHitBuffers{std::pmr::vector<UICommittedHitEntry>(&resource),
                              std::pmr::vector<UICommittedHitEntry>(&resource)},
          committedPaintBuffers{std::pmr::vector<UICommittedPaintEntry>(&resource),
                                std::pmr::vector<UICommittedPaintEntry>(&resource)},
          committedSemanticsBuffers{std::pmr::vector<UISemanticsEntry>(&resource),
                                    std::pmr::vector<UISemanticsEntry>(&resource)},
          committedSemanticsTextBuffers{std::pmr::vector<char>(&resource), std::pmr::vector<char>(&resource)}
    {
    }

    [[nodiscard]] static Core::Result<std::unique_ptr<Impl>>
    Create(Platform::WindowId ownerWindow, NormalizedCapacityConfig normalized,
           std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl, std::pmr::memory_resource& resource)
    {
        auto poolResult = NodePool::Create(normalized.nodeCapacity, resource);
        if (!poolResult)
        {
            const Core::Error& error = poolResult.error();
            if (error.code == Core::CoreErrorCode::CapacityExceeded)
            {
                return fail(UIErrorCode::CapacityExceeded, "UI node pool capacity could not be reserved");
            }
            return Core::failure(error);
        }

        UIContextCapacityConfig capacities{
            .nodeCapacity = normalized.nodeCapacity,
            .rootCapacity = normalized.rootCapacity,
            .dirtyQueueCapacity = normalized.dirtyQueueCapacity,
            .layoutSnapshotCapacity = normalized.layoutSnapshotCapacity,
            .hitSnapshotCapacity = normalized.hitSnapshotCapacity,
            .paintSnapshotCapacity = normalized.paintSnapshotCapacity,
            .routePathCapacity = normalized.routePathCapacity,
            .routedPointerListenerCapacity = normalized.routedPointerListenerCapacity,
            .buttonActionCapacity = normalized.buttonActionCapacity,
            .textByteCapacity = normalized.textByteCapacity,
            .applyDefaultProductChrome = normalized.applyDefaultProductChrome,
        };

        auto impl = std::unique_ptr<Impl>(new Impl(ownerWindow, capacities, std::this_thread::get_id(),
                                                   std::move(lifetimeControl), std::move(*poolResult), resource));
        impl->idsByIndex.resize(normalized.nodeCapacity);
        impl->layoutStylesByIndex.resize(normalized.nodeCapacity);
        impl->pointerHitPoliciesByIndex.resize(normalized.nodeCapacity, UIPointerHitPolicy::Ignore);
        impl->enabledByNodeIndex.resize(normalized.nodeCapacity, 1);
        impl->focusScopeModesByNodeIndex.resize(normalized.nodeCapacity, UIFocusScopeMode::None);
        impl->focusRestoreByNodeIndex.resize(normalized.nodeCapacity);
        impl->boxPaintsByIndex.resize(normalized.nodeCapacity);
        impl->buttonPaintsByNodeIndex.resize(normalized.nodeCapacity);
        impl->themeBindingsByNodeIndex.resize(normalized.nodeCapacity, 0);
        impl->themeDirtyScratchByNodeIndex.resize(normalized.nodeCapacity, 0);
        impl->themeTextMetricsScratchByNodeIndex.resize(normalized.nodeCapacity);
        impl->localSolidFillCacheByIndex.resize(normalized.nodeCapacity);
        impl->localTextColorCacheByIndex.resize(normalized.nodeCapacity);
        impl->textStatesByIndex.resize(normalized.nodeCapacity);
        impl->textEditStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->progressBarStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->radioButtonStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->scrollViewStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->scrollViewLayoutScratchByNodeIndex.resize(normalized.nodeCapacity);
        impl->dropdownStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->popupStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->popupLayoutScratchByNodeIndex.resize(normalized.nodeCapacity);
        impl->listViewStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->listViewLayoutScratchByNodeIndex.resize(normalized.nodeCapacity);
        impl->listViewItemStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->treeViewStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->treeViewLayoutScratchByNodeIndex.resize(normalized.nodeCapacity);
        impl->treeViewItemStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->dirtyByIndex.resize(normalized.nodeCapacity, UIDirty::None);
        impl->dirtyQueuedByIndex.resize(normalized.nodeCapacity, 0);
        impl->dirtyReservedByIndex.resize(normalized.nodeCapacity, 0);
        impl->dirtyQueue.reserve(normalized.dirtyQueueCapacity);
        impl->routeDirtyReservationScratch.reserve(normalized.nodeCapacity);
        impl->routeDirtyReservationCandidateByIndex.resize(normalized.nodeCapacity, 0);
        impl->layoutScratchByIndex.resize(normalized.nodeCapacity);
        impl->layoutWorkByIndex.resize(normalized.nodeCapacity, 0);
        impl->layoutOrderScratch.reserve(normalized.nodeCapacity);
        impl->hitEntryIndexByNodeIndex.resize(normalized.nodeCapacity, InvalidUIHitEntryIndex);
        impl->routePathScratch.reserve(normalized.routePathCapacity);
        impl->pointerCancelRoutePathScratch.reserve(normalized.routePathCapacity);
        impl->checkboxCheckedByNodeIndex.resize(normalized.nodeCapacity, 0);
        impl->checkboxPaintsByNodeIndex.resize(normalized.nodeCapacity);
        impl->sliderStatesByNodeIndex.resize(normalized.nodeCapacity);
        impl->committedBuffers[0].reserve(normalized.nodeCapacity);
        impl->committedBuffers[1].reserve(normalized.nodeCapacity);
        impl->committedLayoutBuffers[0].reserve(normalized.layoutSnapshotCapacity);
        impl->committedLayoutBuffers[1].reserve(normalized.layoutSnapshotCapacity);
        impl->committedHitBuffers[0].reserve(normalized.hitSnapshotCapacity);
        impl->committedHitBuffers[1].reserve(normalized.hitSnapshotCapacity);
        impl->committedPaintBuffers[0].reserve(normalized.paintSnapshotCapacity);
        impl->committedPaintBuffers[1].reserve(normalized.paintSnapshotCapacity);
        // Semantics publishes interactive kinds only; reuse paint snapshot capacity bound.
        impl->committedSemanticsBuffers[0].reserve(normalized.paintSnapshotCapacity);
        impl->committedSemanticsBuffers[1].reserve(normalized.paintSnapshotCapacity);
        impl->committedSemanticsTextBuffers[0].resize(normalized.textByteCapacity, '\0');
        impl->committedSemanticsTextBuffers[1].resize(normalized.textByteCapacity, '\0');
        impl->deferredRootDestroyBuffer.reserve(normalized.rootCapacity);
        impl->deferredRoutedPointerListenerReleaseBuffer.reserve(normalized.routedPointerListenerCapacity);
        return impl;
    }

    void detachLifetime(UIContext* context) noexcept
    {
        if (lifetime && context != nullptr)
        {
            lifetime->detach(*context);
        }
    }

    void buildCommittedStructure(std::pmr::vector<UICommittedNodeEntry>& output) const noexcept
    {
        output.clear();
        u32 ordinal = 0;
        u32 rootIndex = firstRootIndex;
        while (rootIndex != InvalidNodeIndex)
        {
            const NodeRecord* root = recordByIndex(rootIndex);
            const u32 nextRootIndex = root == nullptr ? InvalidNodeIndex : root->nextSiblingIndex;
            appendCommittedTree(rootIndex, ordinal, output);
            rootIndex = nextRootIndex;
        }
    }

    void appendLayoutOrderTree(u32 index, std::pmr::vector<u32>& output) const noexcept
    {
        const u32 rootIndex = index;
        u32 currentIndex = rootIndex;
        while (currentIndex != InvalidNodeIndex)
        {
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }

            output.push_back(currentIndex);

            if (record->firstChildIndex != InvalidNodeIndex)
            {
                currentIndex = record->firstChildIndex;
                continue;
            }

            while (currentIndex != rootIndex)
            {
                record = recordByIndex(currentIndex);
                if (record == nullptr)
                {
                    return;
                }
                if (record->nextSiblingIndex != InvalidNodeIndex)
                {
                    currentIndex = record->nextSiblingIndex;
                    break;
                }
                currentIndex = record->parentIndex;
            }
            if (currentIndex == rootIndex)
            {
                currentIndex = InvalidNodeIndex;
            }
        }
    }

    void buildLayoutOrder(std::pmr::vector<u32>& output) const noexcept
    {
        output.clear();
        u32 rootIndex = firstRootIndex;
        while (rootIndex != InvalidNodeIndex)
        {
            const NodeRecord* root = recordByIndex(rootIndex);
            const u32 nextRootIndex = root == nullptr ? InvalidNodeIndex : root->nextSiblingIndex;
            appendLayoutOrderTree(rootIndex, output);
            rootIndex = nextRootIndex;
        }
    }

    void markLayoutSubtreeWork(u32 rootIndex, u8 work) noexcept
    {
        const u8 completion = layoutSubtreeCompletionMask(work);
        u32 currentIndex = rootIndex;
        while (currentIndex != InvalidNodeIndex)
        {
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }
            layoutWorkByIndex[currentIndex] |= work | completion;

            if (record->firstChildIndex != InvalidNodeIndex)
            {
                currentIndex = record->firstChildIndex;
                continue;
            }

            while (currentIndex != rootIndex)
            {
                record = recordByIndex(currentIndex);
                if (record == nullptr)
                {
                    return;
                }
                if (record->nextSiblingIndex != InvalidNodeIndex)
                {
                    currentIndex = record->nextSiblingIndex;
                    break;
                }
                currentIndex = record->parentIndex;
            }
            if (currentIndex == rootIndex)
            {
                currentIndex = InvalidNodeIndex;
            }
        }
    }

    void ensureLayoutSubtreeWork(u32 rootIndex, u8 work) noexcept
    {
        if (rootIndex >= layoutWorkByIndex.size())
        {
            return;
        }
        const u8 requiredCompletion = layoutSubtreeCompletionMask(work);
        if ((layoutWorkByIndex[rootIndex] & requiredCompletion) == requiredCompletion)
        {
            return;
        }
        markLayoutSubtreeWork(rootIndex, work);
    }

    void markLayoutAncestorsWork(u32 nodeIndex, u8 work) noexcept
    {
        u32 currentIndex = nodeIndex;
        while (currentIndex != InvalidNodeIndex)
        {
            layoutWorkByIndex[currentIndex] |= work;
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }
            currentIndex = record->parentIndex;
        }
    }

    void initializeLayoutWork(const std::pmr::vector<u32>& order, bool allowReuse) noexcept
    {
        for (const u32 index : order)
        {
            layoutWorkByIndex[index] = 0;
        }

        if (!allowReuse)
        {
            for (const u32 index : order)
            {
                layoutWorkByIndex[index] = LayoutWorkMeasure | LayoutWorkArrange;
            }
            return;
        }

        for (const u32 index : order)
        {
            const UIDirty dirty = dirtyByIndex[index];
            if (hasDirty(dirty, UIDirty::Measure))
            {
                layoutWorkByIndex[index] |= LayoutWorkMeasure | LayoutWorkArrange;
            } else if (hasDirty(dirty, UIDirty::Arrange))
            {
                layoutWorkByIndex[index] |= LayoutWorkArrange;
            }
            if (hasDirty(dirty, UIDirty::Style))
            {
                // A direct style change can alter the containing basis or
                // effective visibility of any descendant.
                ensureLayoutSubtreeWork(index, LayoutWorkMeasure | LayoutWorkArrange);
                markLayoutAncestorsWork(index, LayoutWorkArrange);
            }
        }
    }

    [[nodiscard]] static bool samePreparedLayoutInputs(const LayoutPreparedInputs& previous,
                                                       const LayoutPreparedInputs& current) noexcept
    {
        return previous == current;
    }

    void prepareLayoutState(UILogicalSize viewportSize, const std::pmr::vector<u32>& order, bool allowReuse) noexcept
    {
        initializeLayoutWork(order, allowReuse);
        for (const u32 index : order)
        {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr)
            {
                continue;
            }
            const UILayoutStyle& style = layoutStylesByIndex[index];
            UIVisibility ownVisibility = style.visibility;
            if (record->kind == UIWidgetKind::Popup && index < popupStatesByNodeIndex.size() &&
                !popupStatesByNodeIndex[index].open)
            {
                ownVisibility = UIVisibility::Collapsed;
            }
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            const LayoutPreparedInputs previous = scratch.preparedInputs;
            if (!allowReuse)
            {
                scratch = {};
            }

            if (record->parentIndex == InvalidNodeIndex)
            {
                scratch.effectiveVisibility = ownVisibility;
                scratch.inPopupSubtree = record->kind == UIWidgetKind::Popup;
                scratch.parentContentWidthDefinite = true;
                scratch.parentContentHeightDefinite = true;
                scratch.parentContentWidth = viewportSize.width;
                scratch.parentContentHeight = viewportSize.height;
            } else
            {
                const LayoutScratchState& parentScratch = layoutScratchByIndex[record->parentIndex];
                scratch.effectiveVisibility = combineVisibility(parentScratch.effectiveVisibility, ownVisibility);
                scratch.inPopupSubtree =
                    record->kind == UIWidgetKind::Popup || parentScratch.inPopupSubtree;
                scratch.parentContentWidthDefinite = parentScratch.contentWidthDefinite;
                scratch.parentContentHeightDefinite = parentScratch.contentHeightDefinite;
                scratch.parentContentWidth = parentScratch.contentWidth;
                scratch.parentContentHeight = parentScratch.contentHeight;
            }

            const ResolvedLength width = resolveLengthNoFallbackCount(
                style.size.width, scratch.parentContentWidthDefinite, scratch.parentContentWidth);
            const ResolvedLength height = resolveLengthNoFallbackCount(
                style.size.height, scratch.parentContentHeightDefinite, scratch.parentContentHeight);
            const bool isRoot = record->parentIndex == InvalidNodeIndex;
            scratch.contentWidthDefinite = width.hasValue || isRoot;
            scratch.contentHeightDefinite = height.hasValue || isRoot;
            const float outerWidth = width.hasValue ? width.value : viewportSize.width;
            const float outerHeight = height.hasValue ? height.value : viewportSize.height;
            scratch.contentWidth =
                scratch.contentWidthDefinite ? (std::max)(0.0F, outerWidth - horizontalMargin(style.padding)) : 0.0F;
            scratch.contentHeight =
                scratch.contentHeightDefinite ? (std::max)(0.0F, outerHeight - verticalMargin(style.padding)) : 0.0F;

            const LayoutPreparedInputs currentInputs{
                .effectiveVisibility = scratch.effectiveVisibility,
                .parentContentWidthDefinite = scratch.parentContentWidthDefinite,
                .parentContentHeightDefinite = scratch.parentContentHeightDefinite,
                .parentContentWidth = scratch.parentContentWidth,
                .parentContentHeight = scratch.parentContentHeight,
                .contentWidthDefinite = scratch.contentWidthDefinite,
                .contentHeightDefinite = scratch.contentHeightDefinite,
                .contentWidth = scratch.contentWidth,
                .contentHeight = scratch.contentHeight,
            };
            scratch.preparedInputs = currentInputs;

            if (allowReuse && !samePreparedLayoutInputs(previous, currentInputs))
            {
                // Parent constraint or effective visibility changes can
                // invalidate every descendant even when only an ancestor was
                // explicitly queued dirty.
                ensureLayoutSubtreeWork(index, LayoutWorkMeasure | LayoutWorkArrange);
                markLayoutAncestorsWork(index, LayoutWorkArrange);
            }
        }
    }

    [[nodiscard]] float resolvedWidth(const UILayoutStyle& style, const LayoutScratchState& scratch,
                                      LayoutPassStatistics& statistics) const noexcept
    {
        const ResolvedLength value =
            resolveLength(style.size.width, scratch.parentContentWidthDefinite, scratch.parentContentWidth, statistics);
        return value.hasValue ? value.value : -1.0F;
    }

    [[nodiscard]] float resolvedHeight(const UILayoutStyle& style, const LayoutScratchState& scratch,
                                       LayoutPassStatistics& statistics) const noexcept
    {
        const ResolvedLength value = resolveLength(style.size.height, scratch.parentContentHeightDefinite,
                                                   scratch.parentContentHeight, statistics);
        return value.hasValue ? value.value : -1.0F;
    }

    [[nodiscard]] float clampWidth(float value, const UILayoutStyle& style, const LayoutScratchState& scratch,
                                   LayoutPassStatistics& statistics) const noexcept
    {
        return clampWithMinMax(value, style.minMax.minWidth, style.minMax.maxWidth, scratch.parentContentWidthDefinite,
                               scratch.parentContentWidth, statistics);
    }

    [[nodiscard]] float clampHeight(float value, const UILayoutStyle& style, const LayoutScratchState& scratch,
                                    LayoutPassStatistics& statistics) const noexcept
    {
        return clampWithMinMax(value, style.minMax.minHeight, style.minMax.maxHeight,
                               scratch.parentContentHeightDefinite, scratch.parentContentHeight, statistics);
    }

    void measureLayout(UILogicalSize viewportSize, const std::pmr::vector<u32>& order,
                       LayoutPassStatistics& statistics) noexcept
    {
        for (usize reverseIndex = order.size(); reverseIndex > 0; --reverseIndex)
        {
            const u32 index = order[reverseIndex - 1];
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr)
            {
                continue;
            }
            if (!hasLayoutWork(layoutWorkByIndex[index], LayoutWorkMeasure))
            {
                continue;
            }
            const UILayoutStyle& style = layoutStylesByIndex[index];
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            const UILogicalSize previousMeasuredSize = scratch.measuredSize;
            ++statistics.measuredNodeCount;

            if (scratch.effectiveVisibility == UIVisibility::Collapsed)
            {
                scratch.measuredSize = {};
                if (scratch.measuredSize != previousMeasuredSize)
                {
                    ensureLayoutSubtreeWork(index, LayoutWorkArrange);
                }
                continue;
            }

            float autoContentWidth = 0.0F;
            float autoContentHeight = 0.0F;
            float radioLabelWidth = 0.0F;
            bool hasRadioLabel = false;
            bool hasRadioIndicator = false;
            usize flowChildCount = 0;

            u32 childIndex = record->firstChildIndex;
            while (childIndex != InvalidNodeIndex)
            {
                const NodeRecord* childRecord = recordByIndex(childIndex);
                if (childRecord == nullptr)
                {
                    break;
                }
                const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
                const LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
                if (childScratch.effectiveVisibility != UIVisibility::Collapsed &&
                    childStyle.position == UILayoutPositionMode::InFlow)
                {
                    const float childOuterWidth = childScratch.measuredSize.width + horizontalMargin(childStyle.margin);
                    const float childOuterHeight = childScratch.measuredSize.height + verticalMargin(childStyle.margin);
                    if (style.flex.direction == UIFlexDirection::Row)
                    {
                        if (flowChildCount > 0)
                        {
                            autoContentWidth += style.flex.gap.column;
                        }
                        autoContentWidth += childOuterWidth;
                        autoContentHeight = (std::max)(autoContentHeight, childOuterHeight);
                    } else
                    {
                        autoContentWidth = (std::max)(autoContentWidth, childOuterWidth);
                        if (flowChildCount > 0)
                        {
                            autoContentHeight += style.flex.gap.row;
                        }
                        autoContentHeight += childOuterHeight;
                    }
                    ++flowChildCount;
                }
                childIndex = childRecord->nextSiblingIndex;
            }

            if (flowChildCount == 0 && supportsWidgetText(record->kind))
            {
                if (const UITextMetrics* metrics = presentationTextMetricsFor(index); metrics != nullptr)
                {
                    const UILogicalSize textSize = metrics->measuredSize;
                    autoContentWidth = textSize.width;
                    autoContentHeight = textSize.height;
                    if (record->kind == UIWidgetKind::RadioButton && index < radioButtonStatesByNodeIndex.size())
                    {
                        radioLabelWidth = textSize.width;
                        hasRadioLabel = true;
                    }
                }
            }
            if (flowChildCount == 0 && record->kind == UIWidgetKind::Dropdown &&
                index < dropdownStatesByNodeIndex.size())
            {
                const UIDropdownPaint& dropdownPaint = dropdownStatesByNodeIndex[index].paint;
                autoContentWidth += dropdownPaint.indicatorWidth + dropdownPaint.indicatorInset * 2.0F;
                autoContentHeight = (std::max)(autoContentHeight, dropdownPaint.indicatorHeight);
            }

            if (flowChildCount == 0 && record->kind == UIWidgetKind::RadioButton &&
                index < radioButtonStatesByNodeIndex.size())
            {
                hasRadioIndicator = true;
                if (!hasRadioLabel && index < textStatesByIndex.size())
                {
                    const UITextStyle& textStyle = textStatesByIndex[index].style;
                    const float defaultIndicatorExtent = textStyle.logicalSize * textStyle.lineHeightScale;
                    if (std::isfinite(defaultIndicatorExtent) && defaultIndicatorExtent > 0.0F)
                    {
                        autoContentHeight = defaultIndicatorExtent;
                    }
                }
            }

            float outerHeight = resolvedHeight(style, scratch, statistics);
            if (outerHeight < 0.0F)
            {
                outerHeight = record->parentIndex == InvalidNodeIndex
                                  ? (std::max)(autoContentHeight + verticalMargin(style.padding), viewportSize.height)
                                  : autoContentHeight + verticalMargin(style.padding);
            }
            outerHeight = clampHeight(outerHeight, style, scratch, statistics);
            if (hasRadioIndicator)
            {
                autoContentWidth = outerHeight;
                if (hasRadioLabel)
                {
                    autoContentWidth += radioLabelWidth + radioButtonStatesByNodeIndex[index].paint.labelGap;
                }
            }

            float outerWidth = resolvedWidth(style, scratch, statistics);
            if (outerWidth < 0.0F)
            {
                outerWidth = record->parentIndex == InvalidNodeIndex
                                 ? (std::max)(autoContentWidth + horizontalMargin(style.padding), viewportSize.width)
                                 : autoContentWidth + horizontalMargin(style.padding);
            }
            outerWidth = clampWidth(outerWidth, style, scratch, statistics);

            scratch.measuredSize = UILogicalSize{
                .width = normalizeFloat(outerWidth),
                .height = normalizeFloat(outerHeight),
            };
            if (scratch.measuredSize != previousMeasuredSize)
            {
                ensureLayoutSubtreeWork(index, LayoutWorkArrange);
            }
        }
    }

    [[nodiscard]] bool isCrossAxisAuto(const UILayoutStyle& style, UIFlexDirection direction) const noexcept
    {
        return direction == UIFlexDirection::Row ? style.size.height.isAuto() : style.size.width.isAuto();
    }

    void assignLayoutRect(u32 index, UILogicalRect worldRect, UILogicalRect parentWorldRect,
                          UILogicalRect descendantClip) noexcept
    {
        LayoutScratchState& scratch = layoutScratchByIndex[index];
        const UILayoutStyle& style = layoutStylesByIndex[index];
        const UILogicalRect previousWorldRect = scratch.worldRect;
        const UILogicalRect previousLocalRect = scratch.localRect;
        const UILogicalRect previousEffectiveClip = scratch.effectiveClip;
        const UILogicalRect previousDescendantClip = scratch.descendantClip;
        const UIVisibility previousVisibility = scratch.effectiveVisibility;
        if (scratch.effectiveVisibility == UIVisibility::Collapsed)
        {
            worldRect.width = 0.0F;
            worldRect.height = 0.0F;
        }
        scratch.worldRect = worldRect;
        scratch.localRect = UILogicalRect{
            .x = normalizeFloat(worldRect.x - parentWorldRect.x),
            .y = normalizeFloat(worldRect.y - parentWorldRect.y),
            .width = normalizeFloat(worldRect.width),
            .height = normalizeFloat(worldRect.height),
        };
        scratch.descendantClip = descendantClip;
        scratch.effectiveClip = scratch.effectiveVisibility == UIVisibility::Collapsed
                                    ? UILogicalRect{}
                                    : intersectRects(descendantClip, worldRect);
        scratch.contentWidthDefinite = true;
        scratch.contentHeightDefinite = true;
        scratch.contentWidth = normalizeFloat((std::max)(0.0F, worldRect.width - horizontalMargin(style.padding)));
        scratch.contentHeight = normalizeFloat((std::max)(0.0F, worldRect.height - verticalMargin(style.padding)));
        if (layoutReuseInProgress &&
            (previousWorldRect != scratch.worldRect || previousLocalRect != scratch.localRect ||
             previousEffectiveClip != scratch.effectiveClip || previousDescendantClip != scratch.descendantClip ||
             previousVisibility != scratch.effectiveVisibility))
        {
            ensureLayoutSubtreeWork(index, LayoutWorkArrange);
        }
    }

    void refreshMeasuredSizeForParentContent(u32 childIndex, UILogicalRect parentContentRect,
                                             LayoutPassStatistics& statistics) noexcept
    {
        const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
        LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
        const UILogicalSize previousMeasuredSize = childScratch.measuredSize;
        childScratch.parentContentWidthDefinite = true;
        childScratch.parentContentHeightDefinite = true;
        childScratch.parentContentWidth = parentContentRect.width;
        childScratch.parentContentHeight = parentContentRect.height;

        const float resolvedOuterWidth = resolvedWidth(childStyle, childScratch, statistics);
        const float resolvedOuterHeight = resolvedHeight(childStyle, childScratch, statistics);
        const float outerWidth = resolvedOuterWidth >= 0.0F ? resolvedOuterWidth : childScratch.measuredSize.width;
        const float outerHeight = resolvedOuterHeight >= 0.0F ? resolvedOuterHeight : childScratch.measuredSize.height;
        childScratch.measuredSize = UILogicalSize{
            .width = clampWidth(outerWidth, childStyle, childScratch, statistics),
            .height = clampHeight(outerHeight, childStyle, childScratch, statistics),
        };
        if (layoutReuseInProgress && childScratch.measuredSize != previousMeasuredSize)
        {
            ensureLayoutSubtreeWork(childIndex, LayoutWorkArrange);
        }
    }

    [[nodiscard]] float resolveInset(UILayoutLength length, float basis,
                                     LayoutPassStatistics& statistics) const noexcept
    {
        const ResolvedLength resolved = resolveLength(length, true, basis, statistics);
        return resolved.hasValue ? resolved.value : -1.0F;
    }

    void arrangeAbsoluteChild(u32 childIndex, UILogicalRect parentContentRect, UILogicalRect parentWorldRect,
                              UILogicalRect descendantClip, LayoutPassStatistics& statistics) noexcept
    {
        const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
        LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
        refreshMeasuredSizeForParentContent(childIndex, parentContentRect, statistics);
        const float left = resolveInset(childStyle.absoluteInset.left, parentContentRect.width, statistics);
        const float right = resolveInset(childStyle.absoluteInset.right, parentContentRect.width, statistics);
        const float top = resolveInset(childStyle.absoluteInset.top, parentContentRect.height, statistics);
        const float bottom = resolveInset(childStyle.absoluteInset.bottom, parentContentRect.height, statistics);

        float width = childScratch.measuredSize.width;
        float height = childScratch.measuredSize.height;
        if (childStyle.size.width.isAuto() && left >= 0.0F && right >= 0.0F)
        {
            width = (std::max)(0.0F, parentContentRect.width - left - right);
        }
        if (childStyle.size.height.isAuto() && top >= 0.0F && bottom >= 0.0F)
        {
            height = (std::max)(0.0F, parentContentRect.height - top - bottom);
        }
        width = clampWidth(width, childStyle, childScratch, statistics);
        height = clampHeight(height, childStyle, childScratch, statistics);

        const float x = left >= 0.0F
                            ? parentContentRect.x + left
                            : (right >= 0.0F ? parentContentRect.right() - right - width : parentContentRect.x);
        const float y = top >= 0.0F
                            ? parentContentRect.y + top
                            : (bottom >= 0.0F ? parentContentRect.bottom() - bottom - height : parentContentRect.y);

        assignLayoutRect(childIndex,
                         UILogicalRect{
                             .x = normalizeFloat(x),
                             .y = normalizeFloat(y),
                             .width = normalizeFloat((std::max)(0.0F, width)),
                             .height = normalizeFloat((std::max)(0.0F, height)),
                         },
                         parentWorldRect, descendantClip);
    }

    void arrangePopupChild(u32 popupIndex, UILogicalRect anchorRect, UILogicalRect viewportRect,
                           LayoutPassStatistics& statistics) noexcept
    {
        const UILayoutStyle& popupLayoutStyle = layoutStylesByIndex[popupIndex];
        LayoutScratchState& popupScratch = layoutScratchByIndex[popupIndex];
        PopupState& popup = popupStatesByNodeIndex[popupIndex];
        refreshMeasuredSizeForParentContent(popupIndex, viewportRect, statistics);

        float width = popupScratch.measuredSize.width;
        float height = popupScratch.measuredSize.height;
        if (popup.style.matchAnchorWidth)
        {
            width = anchorRect.width;
        }
        width = (std::min)(clampWidth(width, popupLayoutStyle, popupScratch, statistics), viewportRect.width);
        height = (std::min)(clampHeight(height, popupLayoutStyle, popupScratch, statistics), viewportRect.height);

        const float belowY = anchorRect.bottom() + popup.style.anchorGap;
        const float aboveY = anchorRect.y - popup.style.anchorGap - height;
        const bool fitsBelow = belowY + height <= viewportRect.bottom();
        const bool fitsAbove = aboveY >= viewportRect.y;
        const float availableBelow = (std::max)(0.0F, viewportRect.bottom() - belowY);
        const float availableAbove = (std::max)(0.0F, anchorRect.y - popup.style.anchorGap - viewportRect.y);

        UIPopupPlacement resolved = popup.style.placement;
        if (resolved == UIPopupPlacement::Auto)
        {
            resolved = fitsBelow || (!fitsAbove && availableBelow >= availableAbove) ? UIPopupPlacement::Below
                                                                                     : UIPopupPlacement::Above;
        } else if (resolved == UIPopupPlacement::Below && !fitsBelow && fitsAbove)
        {
            resolved = UIPopupPlacement::Above;
        } else if (resolved == UIPopupPlacement::Above && !fitsAbove && fitsBelow)
        {
            resolved = UIPopupPlacement::Below;
        }

        const float maximumX = (std::max)(viewportRect.x, viewportRect.right() - width);
        const float maximumY = (std::max)(viewportRect.y, viewportRect.bottom() - height);
        const float x = (std::clamp)(anchorRect.x, viewportRect.x, maximumX);
        const float requestedY = resolved == UIPopupPlacement::Above ? aboveY : belowY;
        const float y = (std::clamp)(requestedY, viewportRect.y, maximumY);
        const UILogicalRect popupRect{
            .x = normalizeFloat(x),
            .y = normalizeFloat(y),
            .width = normalizeFloat((std::max)(0.0F, width)),
            .height = normalizeFloat((std::max)(0.0F, height)),
        };
        assignLayoutRect(popupIndex, popupRect, anchorRect, viewportRect);
        popupLayoutScratchByNodeIndex[popupIndex].metrics = UIPopupMetrics{
            .anchorRect = anchorRect,
            .popupRect = popupRect,
            .resolvedPlacement = resolved,
            .open = popupScratch.effectiveVisibility == UIVisibility::Visible,
        };
    }

    [[nodiscard]] Core::Status bindListViewItem(u32 itemIndex, u64 logicalIndex,
                                                const UIListViewItemDescriptor& descriptor)
    {
        if (itemIndex >= textStatesByIndex.size() || itemIndex >= listViewItemStatesByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI ListView item side state is out of range");
        }
        if (descriptor.key == InvalidUIListViewItemKey || containsLineBreak(descriptor.label))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI ListView item requires a non-zero key and a single-line label");
        }
        if (descriptor.label.size() > (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI ListView item label is too large");
        }

        WidgetTextState& text = textStatesByIndex[itemIndex];
        auto metrics = measureWidgetText(descriptor.label, text.style);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }

        TextByteAllocation replacement{};
        bool replaceAllocation = false;
        if (!descriptor.label.empty() && text.allocation.capacity < descriptor.label.size())
        {
            auto allocation = textStorage.allocate(static_cast<u32>(descriptor.label.size()));
            if (!allocation)
            {
                return Core::failure(allocation.error());
            }
            replacement = *allocation;
            replaceAllocation = true;
        }
        if (descriptor.label.empty())
        {
            textStorage.release(text.allocation);
            text.allocation = {};
            text.length = 0;
            text.metrics = {};
            text.hasContent = false;
        } else
        {
            if (replaceAllocation)
            {
                textStorage.release(text.allocation);
                text.allocation = replacement;
            }
            textStorage.write(text.allocation, descriptor.label);
            text.length = static_cast<u32>(descriptor.label.size());
            text.metrics = *metrics;
            text.hasContent = true;
        }
        localTextColorCacheByIndex[itemIndex] = text.hasContent ? premultiply(text.style.color)
                                                               : UIPremultipliedRgba8Color{};
        ListViewItemState& item = listViewItemStatesByNodeIndex[itemIndex];
        item.key = descriptor.key;
        item.logicalIndex = logicalIndex;
        item.bound = true;
        item.enabled = descriptor.enabled;
        return Core::success();
    }

    void collapseListViewItems(u32 listViewIndex, UILogicalRect contentRect, UILogicalRect parentWorldRect,
                               UILogicalRect descendantClip) noexcept
    {
        const NodeRecord* listRecord = recordByIndex(listViewIndex);
        u32 childIndex = listRecord == nullptr ? InvalidNodeIndex : listRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            ListViewItemState& item = listViewItemStatesByNodeIndex[childIndex];
            item.key = InvalidUIListViewItemKey;
            item.logicalIndex = 0;
            item.bound = false;
            item.enabled = true;
            layoutScratchByIndex[childIndex].effectiveVisibility = UIVisibility::Collapsed;
            assignLayoutRect(childIndex, contentRect, parentWorldRect, descendantClip);
            childIndex = childRecord->nextSiblingIndex;
        }
    }

    [[nodiscard]] Core::Status arrangeListViewItems(u32 listViewIndex, UILogicalRect unscrolledContentRect,
                                                    UILogicalRect parentWorldRect, UILogicalRect descendantClip)
    {
        ListViewState& state = listViewStatesByNodeIndex[listViewIndex];
        const u64 logicalItemCount = state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
        const double contentHeight64 = static_cast<double>(logicalItemCount) * static_cast<double>(state.style.rowHeight);
        if (!std::isfinite(contentHeight64) || contentHeight64 > (std::numeric_limits<float>::max)())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ListView logical content height is not representable");
        }
        const float logicalContentHeight = normalizeFloat(static_cast<float>(contentHeight64));
        const bool barsHidden = state.style.scrollBarVisibility == UIScrollBarVisibility::Hidden;
        const bool verticalBar =
            !barsHidden && (state.style.scrollBarVisibility == UIScrollBarVisibility::Always ||
                            logicalContentHeight > unscrolledContentRect.height);
        const UILogicalRect viewportRect{
            .x = unscrolledContentRect.x,
            .y = unscrolledContentRect.y,
            .width = normalizeFloat((std::max)(
                0.0F, unscrolledContentRect.width - (verticalBar ? state.paint.scrollBar.thickness : 0.0F))),
            .height = unscrolledContentRect.height,
        };
        const float contentHeight = normalizeFloat((std::max)(logicalContentHeight, viewportRect.height));
        const float maximumOffset = normalizeFloat((std::max)(0.0F, contentHeight - viewportRect.height));
        const float scrollOffset = normalizeFloat((std::clamp)(state.requestedScrollOffset, 0.0F, maximumOffset));

        u64 firstVisibleIndex = 0;
        u64 visibleItemCount64 = 0;
        if (logicalItemCount != 0 && viewportRect.height > 0.0F)
        {
            firstVisibleIndex = (std::min)(logicalItemCount - 1,
                                           static_cast<u64>(std::floor(static_cast<double>(scrollOffset) /
                                                                       state.style.rowHeight)));
            const double visibleEnd = std::ceil((static_cast<double>(scrollOffset) + viewportRect.height) /
                                                state.style.rowHeight);
            const u64 endIndex = (std::min)(logicalItemCount, static_cast<u64>((std::max)(0.0, visibleEnd)));
            visibleItemCount64 = endIndex > firstVisibleIndex ? endIndex - firstVisibleIndex : 0;
        }
        const u64 firstMaterializedIndex =
            firstVisibleIndex > state.style.overscanRows ? firstVisibleIndex - state.style.overscanRows : 0;
        const u64 visibleEndIndex = firstVisibleIndex + visibleItemCount64;
        const u64 materializedEndIndex =
            (std::min)(logicalItemCount, visibleEndIndex + static_cast<u64>(state.style.overscanRows));
        const u64 materializedItemCount64 =
            materializedEndIndex > firstMaterializedIndex ? materializedEndIndex - firstMaterializedIndex : 0;
        if (materializedItemCount64 > state.materializedItemCapacity)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI ListView row pool cannot cover the viewport and configured overscan");
        }

        ListViewLayoutScratch& listLayout = listViewLayoutScratchByNodeIndex[listViewIndex];
        listLayout = ListViewLayoutScratch{
            .metrics =
                UIListViewMetrics{
                    .logicalItemCount = logicalItemCount,
                    .firstVisibleIndex = firstVisibleIndex,
                    .visibleItemCount = static_cast<u32>(visibleItemCount64),
                    .firstMaterializedIndex = firstMaterializedIndex,
                    .materializedItemCount = static_cast<u32>(materializedItemCount64),
                    .materializedItemCapacity = state.materializedItemCapacity,
                    .scrollOffset = scrollOffset,
                    .maxScrollOffset = maximumOffset,
                    .viewportSize = viewportRect.size(),
                    .contentSize = {.width = viewportRect.width, .height = contentHeight},
                    .verticalScrollBarVisible = verticalBar,
                },
            .viewportRect = viewportRect,
        };

        const NodeRecord* listRecord = recordByIndex(listViewIndex);
        u32 childIndex = listRecord == nullptr ? InvalidNodeIndex : listRecord->firstChildIndex;
        u64 materializedOrdinal = 0;
        const UIVisibility rowVisibility = layoutScratchByIndex[listViewIndex].effectiveVisibility;
        const UILogicalRect rowClip = intersectRects(descendantClip, viewportRect);
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            const u32 currentChild = childIndex;
            childIndex = childRecord->nextSiblingIndex;
            if (materializedOrdinal >= materializedItemCount64)
            {
                ListViewItemState& item = listViewItemStatesByNodeIndex[currentChild];
                item.key = InvalidUIListViewItemKey;
                item.logicalIndex = 0;
                item.bound = false;
                item.enabled = true;
                layoutScratchByIndex[currentChild].effectiveVisibility = UIVisibility::Collapsed;
                assignLayoutRect(currentChild, viewportRect, parentWorldRect, rowClip);
                continue;
            }

            const u64 logicalIndex = firstMaterializedIndex + materializedOrdinal;
            auto descriptor = resolveListViewLogicalItem(idForIndex(listViewIndex), logicalIndex);
            if (!descriptor)
            {
                return Core::failure(descriptor.error());
            }
            if (Core::Status bound = bindListViewItem(currentChild, logicalIndex, *descriptor); !bound)
            {
                return bound;
            }
            layoutScratchByIndex[currentChild].effectiveVisibility = rowVisibility;
            const double logicalY = static_cast<double>(logicalIndex) * state.style.rowHeight;
            const float rowY = normalizeFloat(viewportRect.y + static_cast<float>(logicalY) - scrollOffset);
            assignLayoutRect(currentChild,
                             UILogicalRect{
                                 .x = viewportRect.x,
                                 .y = rowY,
                                 .width = viewportRect.width,
                                 .height = state.style.rowHeight,
                             },
                             parentWorldRect, rowClip);
            ++materializedOrdinal;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status bindTreeViewItem(u32 itemIndex, u64 logicalIndex,
                                                const UITreeViewItemDescriptor& descriptor)
    {
        if (itemIndex >= textStatesByIndex.size() || itemIndex >= treeViewItemStatesByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI TreeView item side state is out of range");
        }
        if (descriptor.key == InvalidUITreeViewItemKey || containsLineBreak(descriptor.label) ||
            (descriptor.expanded && !descriptor.expandable))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI TreeView item requires a non-zero key, a single-line label, and valid expansion state");
        }
        if (descriptor.label.size() > (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI TreeView item label is too large");
        }

        WidgetTextState& text = textStatesByIndex[itemIndex];
        auto metrics = measureWidgetText(descriptor.label, text.style);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }

        TextByteAllocation replacement{};
        bool replaceAllocation = false;
        if (!descriptor.label.empty() && text.allocation.capacity < descriptor.label.size())
        {
            auto allocation = textStorage.allocate(static_cast<u32>(descriptor.label.size()));
            if (!allocation)
            {
                return Core::failure(allocation.error());
            }
            replacement = *allocation;
            replaceAllocation = true;
        }
        if (descriptor.label.empty())
        {
            textStorage.release(text.allocation);
            text.allocation = {};
            text.length = 0;
            text.metrics = {};
            text.hasContent = false;
        } else
        {
            if (replaceAllocation)
            {
                textStorage.release(text.allocation);
                text.allocation = replacement;
            }
            textStorage.write(text.allocation, descriptor.label);
            text.length = static_cast<u32>(descriptor.label.size());
            text.metrics = *metrics;
            text.hasContent = true;
        }
        localTextColorCacheByIndex[itemIndex] =
            text.hasContent ? premultiply(text.style.color) : UIPremultipliedRgba8Color{};
        TreeViewItemState& item = treeViewItemStatesByNodeIndex[itemIndex];
        item.key = descriptor.key;
        item.logicalIndex = logicalIndex;
        item.level = descriptor.level;
        item.bound = true;
        item.enabled = descriptor.enabled;
        item.expandable = descriptor.expandable;
        item.expanded = descriptor.expanded;
        return Core::success();
    }

    void collapseTreeViewItems(u32 treeViewIndex, UILogicalRect contentRect, UILogicalRect parentWorldRect,
                               UILogicalRect descendantClip) noexcept
    {
        const NodeRecord* treeRecord = recordByIndex(treeViewIndex);
        u32 childIndex = treeRecord == nullptr ? InvalidNodeIndex : treeRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            TreeViewItemState& item = treeViewItemStatesByNodeIndex[childIndex];
            item = {};
            layoutScratchByIndex[childIndex].effectiveVisibility = UIVisibility::Collapsed;
            assignLayoutRect(childIndex, contentRect, parentWorldRect, descendantClip);
            childIndex = childRecord->nextSiblingIndex;
        }
    }

    [[nodiscard]] Core::Status arrangeTreeViewItems(u32 treeViewIndex, UILogicalRect unscrolledContentRect,
                                                    UILogicalRect parentWorldRect, UILogicalRect descendantClip)
    {
        TreeViewState& state = treeViewStatesByNodeIndex[treeViewIndex];
        const u64 logicalItemCount =
            state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
        const double contentHeight64 =
            static_cast<double>(logicalItemCount) * static_cast<double>(state.style.rowHeight);
        if (!std::isfinite(contentHeight64) || contentHeight64 > (std::numeric_limits<float>::max)())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView logical content height is not representable");
        }
        const float logicalContentHeight = normalizeFloat(static_cast<float>(contentHeight64));
        const bool barsHidden = state.style.scrollBarVisibility == UIScrollBarVisibility::Hidden;
        const bool verticalBar = !barsHidden && (state.style.scrollBarVisibility == UIScrollBarVisibility::Always ||
                                                 logicalContentHeight > unscrolledContentRect.height);
        const UILogicalRect viewportRect{
            .x = unscrolledContentRect.x,
            .y = unscrolledContentRect.y,
            .width = normalizeFloat(
                (std::max)(0.0F, unscrolledContentRect.width - (verticalBar ? state.paint.scrollBar.thickness : 0.0F))),
            .height = unscrolledContentRect.height,
        };
        const float contentHeight = normalizeFloat((std::max)(logicalContentHeight, viewportRect.height));
        const float maximumOffset = normalizeFloat((std::max)(0.0F, contentHeight - viewportRect.height));
        const float scrollOffset = normalizeFloat((std::clamp)(state.requestedScrollOffset, 0.0F, maximumOffset));

        u64 firstVisibleIndex = 0;
        u64 visibleItemCount64 = 0;
        if (logicalItemCount != 0 && viewportRect.height > 0.0F)
        {
            firstVisibleIndex =
                (std::min)(logicalItemCount - 1,
                           static_cast<u64>(std::floor(static_cast<double>(scrollOffset) / state.style.rowHeight)));
            const double visibleEnd =
                std::ceil((static_cast<double>(scrollOffset) + viewportRect.height) / state.style.rowHeight);
            const u64 endIndex = visibleEnd >= static_cast<double>(logicalItemCount)
                                     ? logicalItemCount
                                     : static_cast<u64>((std::max)(0.0, visibleEnd));
            visibleItemCount64 = endIndex > firstVisibleIndex ? endIndex - firstVisibleIndex : 0;
        }
        const u64 firstMaterializedIndex =
            firstVisibleIndex > state.style.overscanRows ? firstVisibleIndex - state.style.overscanRows : 0;
        const u64 visibleEndIndex = firstVisibleIndex + visibleItemCount64;
        const u64 materializedEndIndex =
            (std::min)(logicalItemCount, visibleEndIndex + static_cast<u64>(state.style.overscanRows));
        const u64 materializedItemCount64 =
            materializedEndIndex > firstMaterializedIndex ? materializedEndIndex - firstMaterializedIndex : 0;
        if (materializedItemCount64 > state.materializedItemCapacity)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI TreeView row pool cannot cover the viewport and configured overscan");
        }

        TreeViewLayoutScratch& treeLayout = treeViewLayoutScratchByNodeIndex[treeViewIndex];
        treeLayout = TreeViewLayoutScratch{
            .metrics =
                UITreeViewMetrics{
                    .logicalItemCount = logicalItemCount,
                    .firstVisibleIndex = firstVisibleIndex,
                    .visibleItemCount = static_cast<u32>(visibleItemCount64),
                    .firstMaterializedIndex = firstMaterializedIndex,
                    .materializedItemCount = static_cast<u32>(materializedItemCount64),
                    .materializedItemCapacity = state.materializedItemCapacity,
                    .scrollOffset = scrollOffset,
                    .maxScrollOffset = maximumOffset,
                    .viewportSize = viewportRect.size(),
                    .contentSize = {.width = viewportRect.width, .height = contentHeight},
                    .verticalScrollBarVisible = verticalBar,
                },
            .viewportRect = viewportRect,
        };

        const NodeRecord* treeRecord = recordByIndex(treeViewIndex);
        u32 childIndex = treeRecord == nullptr ? InvalidNodeIndex : treeRecord->firstChildIndex;
        u64 materializedOrdinal = 0;
        const UIVisibility rowVisibility = layoutScratchByIndex[treeViewIndex].effectiveVisibility;
        const UILogicalRect rowClip = intersectRects(descendantClip, viewportRect);
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            const u32 currentChild = childIndex;
            childIndex = childRecord->nextSiblingIndex;
            if (materializedOrdinal >= materializedItemCount64)
            {
                treeViewItemStatesByNodeIndex[currentChild] = {};
                layoutScratchByIndex[currentChild].effectiveVisibility = UIVisibility::Collapsed;
                assignLayoutRect(currentChild, viewportRect, parentWorldRect, rowClip);
                continue;
            }

            const u64 logicalIndex = firstMaterializedIndex + materializedOrdinal;
            auto descriptor = resolveTreeViewLogicalItem(idForIndex(treeViewIndex), logicalIndex);
            if (!descriptor)
            {
                return Core::failure(descriptor.error());
            }
            const double leftPadding = 8.0 + static_cast<double>(descriptor->level) * state.style.indentation +
                                       state.style.disclosureExtent + state.style.disclosureGap;
            if (!std::isfinite(leftPadding) || leftPadding > (std::numeric_limits<float>::max)())
            {
                return fail(UIErrorCode::InvalidControlValue, "UI TreeView item indentation is not representable");
            }
            if (Core::Status bound = bindTreeViewItem(currentChild, logicalIndex, *descriptor); !bound)
            {
                return bound;
            }
            UILayoutStyle& rowStyle = layoutStylesByIndex[currentChild];
            rowStyle.size.height = UILayoutLength::Px(state.style.rowHeight);
            rowStyle.padding.left = normalizeFloat(static_cast<float>(leftPadding));
            rowStyle.padding.right = 8.0F;
            rowStyle.padding.top = 4.0F;
            rowStyle.padding.bottom = 4.0F;
            layoutScratchByIndex[currentChild].effectiveVisibility = rowVisibility;
            const double logicalY = static_cast<double>(logicalIndex) * state.style.rowHeight;
            const float rowY = normalizeFloat(viewportRect.y + static_cast<float>(logicalY) - scrollOffset);
            assignLayoutRect(currentChild,
                             UILogicalRect{
                                 .x = viewportRect.x,
                                 .y = rowY,
                                 .width = viewportRect.width,
                                 .height = state.style.rowHeight,
                             },
                             parentWorldRect, rowClip);
            ++materializedOrdinal;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status arrangeChildren(u32 parentIndex, UILogicalRect viewportRect,
                                               LayoutPassStatistics& statistics)
    {
        const NodeRecord* parentRecord = recordByIndex(parentIndex);
        if (parentRecord == nullptr)
        {
            return Core::success();
        }
        const UILayoutStyle& parentStyle = layoutStylesByIndex[parentIndex];
        const LayoutScratchState& parentScratch = layoutScratchByIndex[parentIndex];
        const UILogicalRect parentWorldRect = parentScratch.worldRect;
        const UILogicalRect unscrolledContentRect{
            .x = normalizeFloat(parentWorldRect.x + parentStyle.padding.left),
            .y = normalizeFloat(parentWorldRect.y + parentStyle.padding.top),
            .width = normalizeFloat((std::max)(0.0F, parentWorldRect.width - horizontalMargin(parentStyle.padding))),
            .height = normalizeFloat((std::max)(0.0F, parentWorldRect.height - verticalMargin(parentStyle.padding))),
        };
        UILogicalRect layoutContentRect = unscrolledContentRect;
        UILogicalRect descendantClip = parentScratch.descendantClip;
        if (parentRecord->kind == UIWidgetKind::Popup)
        {
            descendantClip = intersectRects(descendantClip, parentScratch.worldRect);
        }

        if (parentScratch.effectiveVisibility == UIVisibility::Collapsed)
        {
            if (parentRecord->kind == UIWidgetKind::ScrollView &&
                parentIndex < scrollViewLayoutScratchByNodeIndex.size())
            {
                scrollViewLayoutScratchByNodeIndex[parentIndex] = {};
            }
            if (parentRecord->kind == UIWidgetKind::Popup && parentIndex < popupLayoutScratchByNodeIndex.size())
            {
                popupLayoutScratchByNodeIndex[parentIndex] = {};
            }
            if (parentRecord->kind == UIWidgetKind::ListView && parentIndex < listViewLayoutScratchByNodeIndex.size())
            {
                listViewLayoutScratchByNodeIndex[parentIndex] = {};
                collapseListViewItems(parentIndex, unscrolledContentRect, parentWorldRect, descendantClip);
                return Core::success();
            }
            if (parentRecord->kind == UIWidgetKind::TreeView && parentIndex < treeViewLayoutScratchByNodeIndex.size())
            {
                treeViewLayoutScratchByNodeIndex[parentIndex] = {};
                collapseTreeViewItems(parentIndex, unscrolledContentRect, parentWorldRect, descendantClip);
                return Core::success();
            }
            u32 collapsedChild = parentRecord->firstChildIndex;
            while (collapsedChild != InvalidNodeIndex)
            {
                const NodeRecord* childRecord = recordByIndex(collapsedChild);
                if (childRecord == nullptr)
                {
                    break;
                }
                assignLayoutRect(collapsedChild, unscrolledContentRect, parentWorldRect, descendantClip);
                collapsedChild = childRecord->nextSiblingIndex;
            }
            return Core::success();
        }

        if (parentRecord->kind == UIWidgetKind::ListView && parentIndex < listViewStatesByNodeIndex.size() &&
            parentIndex < listViewLayoutScratchByNodeIndex.size())
        {
            return arrangeListViewItems(parentIndex, unscrolledContentRect, parentWorldRect,
                                        intersectRects(parentScratch.descendantClip, parentScratch.worldRect));
        }
        if (parentRecord->kind == UIWidgetKind::TreeView && parentIndex < treeViewStatesByNodeIndex.size() &&
            parentIndex < treeViewLayoutScratchByNodeIndex.size())
        {
            return arrangeTreeViewItems(parentIndex, unscrolledContentRect, parentWorldRect,
                                        intersectRects(parentScratch.descendantClip, parentScratch.worldRect));
        }

        const bool row = parentStyle.flex.direction == UIFlexDirection::Row;
        const float configuredGap = row ? parentStyle.flex.gap.column : parentStyle.flex.gap.row;

        usize flowChildCount = 0;
        float totalMain = 0.0F;
        float totalCross = 0.0F;
        double totalGrow = 0.0;
        u32 childIndex = parentRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            const UILayoutStyle& childStyle = layoutStylesByIndex[childIndex];
            LayoutScratchState& childScratch = layoutScratchByIndex[childIndex];
            if (childScratch.effectiveVisibility != UIVisibility::Collapsed &&
                childStyle.position == UILayoutPositionMode::InFlow)
            {
                refreshMeasuredSizeForParentContent(childIndex, unscrolledContentRect, statistics);
                if (flowChildCount > 0)
                {
                    totalMain += configuredGap;
                }
                const float childOuterWidth = childScratch.measuredSize.width + horizontalMargin(childStyle.margin);
                const float childOuterHeight = childScratch.measuredSize.height + verticalMargin(childStyle.margin);
                totalMain += row ? childOuterWidth : childOuterHeight;
                totalCross = (std::max)(totalCross, row ? childOuterHeight : childOuterWidth);
                totalGrow += static_cast<double>(childStyle.flex.grow);
                ++flowChildCount;
            }
            childIndex = childRecord->nextSiblingIndex;
        }

        if (parentRecord->kind == UIWidgetKind::ScrollView && parentIndex < scrollViewStatesByNodeIndex.size() &&
            parentIndex < scrollViewLayoutScratchByNodeIndex.size())
        {
            ScrollViewState& state = scrollViewStatesByNodeIndex[parentIndex];
            const bool horizontalEnabled = hasScrollAxis(state.style.axes, UIScrollAxes::Horizontal);
            const bool verticalEnabled = hasScrollAxis(state.style.axes, UIScrollAxes::Vertical);
            const float rawContentWidth = row ? totalMain : totalCross;
            const float rawContentHeight = row ? totalCross : totalMain;
            const bool barsHidden = state.style.scrollBarVisibility == UIScrollBarVisibility::Hidden;
            bool horizontalBar = !barsHidden && horizontalEnabled &&
                                 state.style.scrollBarVisibility == UIScrollBarVisibility::Always;
            bool verticalBar = !barsHidden && verticalEnabled &&
                               state.style.scrollBarVisibility == UIScrollBarVisibility::Always;

            // Auto bars can cause one another by reducing the opposite axis.
            // Two updates reach the fixed point for a rectangular viewport.
            for (usize passIndex = 0; passIndex < 2; ++passIndex)
            {
                const float viewportWidth =
                    (std::max)(0.0F, unscrolledContentRect.width - (verticalBar ? state.paint.thickness : 0.0F));
                const float viewportHeight =
                    (std::max)(0.0F, unscrolledContentRect.height - (horizontalBar ? state.paint.thickness : 0.0F));
                if (!barsHidden && state.style.scrollBarVisibility == UIScrollBarVisibility::Auto)
                {
                    horizontalBar = horizontalEnabled && rawContentWidth > viewportWidth;
                    verticalBar = verticalEnabled && rawContentHeight > viewportHeight;
                }
            }

            const UILogicalRect scrollViewportRect{
                .x = unscrolledContentRect.x,
                .y = unscrolledContentRect.y,
                .width = normalizeFloat((std::max)(
                    0.0F, unscrolledContentRect.width - (verticalBar ? state.paint.thickness : 0.0F))),
                .height = normalizeFloat((std::max)(
                    0.0F, unscrolledContentRect.height - (horizontalBar ? state.paint.thickness : 0.0F))),
            };
            const UILogicalSize contentSize{
                .width = normalizeFloat(horizontalEnabled
                                            ? (std::max)(rawContentWidth, scrollViewportRect.width)
                                            : scrollViewportRect.width),
                .height = normalizeFloat(verticalEnabled
                                             ? (std::max)(rawContentHeight, scrollViewportRect.height)
                                             : scrollViewportRect.height),
            };
            const UIScrollOffset clampedOffset{
                .x = horizontalEnabled
                         ? normalizeFloat((std::clamp)(state.requestedOffset.x, 0.0F,
                                                       (std::max)(0.0F, contentSize.width - scrollViewportRect.width)))
                         : 0.0F,
                .y = verticalEnabled
                         ? normalizeFloat((std::clamp)(state.requestedOffset.y, 0.0F,
                                                       (std::max)(0.0F, contentSize.height - scrollViewportRect.height)))
                         : 0.0F,
            };
            scrollViewLayoutScratchByNodeIndex[parentIndex] = ScrollViewLayoutScratch{
                .metrics =
                    UIScrollViewMetrics{
                        .offset = clampedOffset,
                        .viewportSize = scrollViewportRect.size(),
                        .contentSize = contentSize,
                        .horizontalScrollBarVisible = horizontalBar,
                        .verticalScrollBarVisible = verticalBar,
                    },
                .viewportRect = scrollViewportRect,
            };
            layoutContentRect = UILogicalRect{
                .x = normalizeFloat(scrollViewportRect.x - clampedOffset.x),
                .y = normalizeFloat(scrollViewportRect.y - clampedOffset.y),
                .width = contentSize.width,
                .height = contentSize.height,
            };
            descendantClip = intersectRects(parentScratch.descendantClip, scrollViewportRect);
        }

        const float contentMain = row ? layoutContentRect.width : layoutContentRect.height;
        const float contentCross = row ? layoutContentRect.height : layoutContentRect.width;

        const float freeSpace = contentMain - totalMain;
        const float growSpace = totalGrow > 0.0 ? (std::max)(0.0F, freeSpace) : 0.0F;
        float mainOffset = 0.0F;
        float gap = configuredGap;
        if (totalGrow == 0.0 && freeSpace > 0.0F)
        {
            switch (parentStyle.flex.justify)
            {
            case UIJustifyContent::Center:
                mainOffset = freeSpace * 0.5F;
                break;
            case UIJustifyContent::End:
                mainOffset = freeSpace;
                break;
            case UIJustifyContent::SpaceBetween:
                if (flowChildCount > 1)
                {
                    gap += freeSpace / static_cast<float>(flowChildCount - 1);
                }
                break;
            case UIJustifyContent::Start:
                break;
            }
        }

        childIndex = parentRecord->firstChildIndex;
        while (childIndex != InvalidNodeIndex)
        {
            const NodeRecord* childRecord = recordByIndex(childIndex);
            if (childRecord == nullptr)
            {
                break;
            }
            const u32 currentChild = childIndex;
            childIndex = childRecord->nextSiblingIndex;
            const UILayoutStyle& childStyle = layoutStylesByIndex[currentChild];
            LayoutScratchState& childScratch = layoutScratchByIndex[currentChild];

            if (childScratch.effectiveVisibility == UIVisibility::Collapsed)
            {
                assignLayoutRect(currentChild, layoutContentRect, parentWorldRect, descendantClip);
                continue;
            }
            if (childStyle.position == UILayoutPositionMode::AbsoluteOverlay)
            {
                if (childRecord->kind == UIWidgetKind::Popup && currentChild < popupStatesByNodeIndex.size() &&
                    currentChild < popupLayoutScratchByNodeIndex.size())
                {
                    arrangePopupChild(currentChild, parentWorldRect, viewportRect, statistics);
                } else
                {
                    arrangeAbsoluteChild(currentChild, layoutContentRect, parentWorldRect, descendantClip, statistics);
                }
                continue;
            }

            float width = childScratch.measuredSize.width;
            float height = childScratch.measuredSize.height;
            if (totalGrow > 0.0 && childStyle.flex.grow > 0.0F)
            {
                const double growRatio = static_cast<double>(childStyle.flex.grow) / totalGrow;
                const float share = growSpace * static_cast<float>(growRatio);
                if (row)
                {
                    width += share;
                } else
                {
                    height += share;
                }
            }

            const float crossAvailable = (std::max)(0.0F, contentCross - (row ? verticalMargin(childStyle.margin)
                                                                              : horizontalMargin(childStyle.margin)));
            if (parentStyle.flex.alignItems == UIAlignItems::Stretch &&
                isCrossAxisAuto(childStyle, parentStyle.flex.direction))
            {
                if (row)
                {
                    height = crossAvailable;
                } else
                {
                    width = crossAvailable;
                }
            }
            width = clampWidth(width, childStyle, childScratch, statistics);
            height = clampHeight(height, childStyle, childScratch, statistics);

            const float childMainSize = row ? width : height;
            const float childCrossSize = row ? height : width;
            const float mainBefore = row ? childStyle.margin.left : childStyle.margin.top;
            const float mainAfter = row ? childStyle.margin.right : childStyle.margin.bottom;
            const float crossBefore = row ? childStyle.margin.top : childStyle.margin.left;
            const float crossAfter = row ? childStyle.margin.bottom : childStyle.margin.right;

            float crossOffset = crossBefore;
            const float crossFree = contentCross - childCrossSize - crossBefore - crossAfter;
            if (crossFree > 0.0F)
            {
                switch (parentStyle.flex.alignItems)
                {
                case UIAlignItems::Center:
                    crossOffset = crossBefore + crossFree * 0.5F;
                    break;
                case UIAlignItems::End:
                    crossOffset = crossBefore + crossFree;
                    break;
                case UIAlignItems::Stretch:
                case UIAlignItems::Start:
                    break;
                }
            }

            const float x =
                row ? layoutContentRect.x + mainOffset + mainBefore : layoutContentRect.x + crossOffset;
            const float y =
                row ? layoutContentRect.y + crossOffset : layoutContentRect.y + mainOffset + mainBefore;
            assignLayoutRect(currentChild,
                             UILogicalRect{
                                 .x = normalizeFloat(x),
                                 .y = normalizeFloat(y),
                                 .width = normalizeFloat((std::max)(0.0F, width)),
                                 .height = normalizeFloat((std::max)(0.0F, height)),
                             },
                             parentWorldRect, descendantClip);

            mainOffset += childMainSize + mainBefore + mainAfter + gap;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status arrangeLayout(UILogicalSize viewportSize, const std::pmr::vector<u32>& order,
                                             LayoutPassStatistics& statistics)
    {
        const UILogicalRect viewportRect{
            .x = 0.0F,
            .y = 0.0F,
            .width = viewportSize.width,
            .height = viewportSize.height,
        };

        u32 ordinal = 0;
        for (const u32 index : order)
        {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr)
            {
                continue;
            }
            const u32 currentOrdinal = ordinal++;
            if (!hasLayoutWork(layoutWorkByIndex[index], LayoutWorkArrange))
            {
                continue;
            }
            ++statistics.arrangedNodeCount;
            LayoutScratchState& scratch = layoutScratchByIndex[index];
            if (record->parentIndex == InvalidNodeIndex)
            {
                assignLayoutRect(index,
                                 UILogicalRect{
                                     .x = 0.0F,
                                     .y = 0.0F,
                                     .width = scratch.measuredSize.width,
                                     .height = scratch.measuredSize.height,
                                 },
                                 viewportRect, viewportRect);
            }
            scratch.layoutOrdinal = currentOrdinal;
            scratch.paintOrdinal = currentOrdinal;
            if (Core::Status arranged = arrangeChildren(index, viewportRect, statistics); !arranged)
            {
                return arranged;
            }
        }
        return Core::success();
    }

    void buildCommittedLayout(std::pmr::vector<UICommittedLayoutEntry>& output,
                              const std::pmr::vector<u32>& order) const noexcept
    {
        output.clear();
        u32 paintOrdinal = 0;
        const auto appendPass = [&](bool popupPass) noexcept {
            for (const u32 index : order)
            {
                if (layoutScratchByIndex[index].inPopupSubtree != popupPass)
                {
                    continue;
                }
                const LayoutScratchState& scratch = layoutScratchByIndex[index];
                output.push_back(UICommittedLayoutEntry{
                    .node = idForIndex(index),
                    .localRect = scratch.localRect,
                    .worldRect = scratch.worldRect,
                    .effectiveClip = scratch.effectiveClip,
                    .effectiveVisibility = scratch.effectiveVisibility,
                    .layoutOrdinal = scratch.layoutOrdinal,
                    .paintOrdinal = paintOrdinal,
                });
                ++paintOrdinal;
            }
        };
        appendPass(false);
        appendPass(true);
    }

    [[nodiscard]] Core::Result<CommittedHitBuildResult>
    buildCommittedHit(std::pmr::vector<UICommittedHitEntry>& output,
                      std::span<const UICommittedLayoutEntry> layoutEntries)
    {
        usize visibleEntryCount = 0;
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            if (layoutEntry.effectiveVisibility == UIVisibility::Visible)
            {
                ++visibleEntryCount;
            }
        }
        if (visibleEntryCount > capacityConfig.hitSnapshotCapacity)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI committed hit snapshot capacity has been exhausted");
        }

        output.clear();
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            hitEntryIndexByNodeIndex[layoutEntry.node.index()] = InvalidUIHitEntryIndex;
        }
        usize targetCount = 0;
        u32 activeModalEntryIndex = InvalidUIHitEntryIndex;

        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            if (layoutEntry.effectiveVisibility != UIVisibility::Visible)
            {
                continue;
            }
            if (!contains(layoutEntry.node))
            {
                return fail(UIErrorCode::InvalidNode, "UI hit snapshot layout references a stale node");
            }

            const u32 nodeIndex = layoutEntry.node.index();
            const NodeRecord* record = recordByIndex(nodeIndex);
            if (record == nullptr)
            {
                return fail(UIErrorCode::InvalidNode, "UI hit snapshot node record is unavailable");
            }

            const UIPointerHitPolicy policy = pointerHitPoliciesByIndex[nodeIndex];
            if (!isValidPointerHitPolicy(policy))
            {
                return fail(UIErrorCode::InvalidPointerPolicy, "UI hit snapshot contains an invalid pointer policy");
            }
            const UIFocusScopeMode focusScopeMode = focusScopeModesByNodeIndex[nodeIndex];
            if (!isValidFocusScopeMode(focusScopeMode))
            {
                return fail(UIErrorCode::InvalidFocusScope, "UI hit snapshot contains an invalid focus-scope mode");
            }

            const u32 entryIndex = static_cast<u32>(output.size());
            u32 parentEntryIndex = InvalidUIHitEntryIndex;
            u32 rootEntryIndex = entryIndex;
            u32 focusScopeEntryIndex = InvalidUIHitEntryIndex;
            u32 modalScopeEntryIndex = InvalidUIHitEntryIndex;
            if (record->parentIndex != InvalidNodeIndex)
            {
                parentEntryIndex = hitEntryIndexByNodeIndex[record->parentIndex];
                rootEntryIndex = hitEntryIndexByNodeIndex[record->rootIndex];
                if (parentEntryIndex == InvalidUIHitEntryIndex || rootEntryIndex == InvalidUIHitEntryIndex)
                {
                    return fail(UIErrorCode::InvalidNode, "UI hit snapshot visible ancestry is incomplete");
                }
                focusScopeEntryIndex = output[parentEntryIndex].focusScopeEntryIndex;
                modalScopeEntryIndex = output[parentEntryIndex].modalScopeEntryIndex;
            }
            if (focusScopeMode == UIFocusScopeMode::Contain || record->kind == UIWidgetKind::Modal)
            {
                focusScopeEntryIndex = entryIndex;
            }
            if (record->kind == UIWidgetKind::Modal)
            {
                modalScopeEntryIndex = entryIndex;
                activeModalEntryIndex = entryIndex;
            }

            output.push_back(UICommittedHitEntry{
                .node = layoutEntry.node,
                .parentEntryIndex = parentEntryIndex,
                .rootEntryIndex = rootEntryIndex,
                .focusScopeEntryIndex = focusScopeEntryIndex,
                .modalScopeEntryIndex = modalScopeEntryIndex,
                .worldRect = layoutEntry.worldRect,
                .effectiveClip = layoutEntry.effectiveClip,
                .paintOrdinal = layoutEntry.paintOrdinal,
                .policy = policy,
                .kind = record->kind,
            });
            hitEntryIndexByNodeIndex[nodeIndex] = entryIndex;
            if (policy == UIPointerHitPolicy::Targetable)
            {
                ++targetCount;
            }
        }
        return CommittedHitBuildResult{
            .targetCount = targetCount,
            .activeModalEntryIndex = activeModalEntryIndex,
        };
    }

    [[nodiscard]] static usize countDrawableTextCodepoints(std::string_view utf8) noexcept
    {
        usize count = 0;
        usize index = 0;
        while (index < utf8.size())
        {
            const auto first = static_cast<unsigned char>(utf8[index]);
            usize unitLength = 1;
            if (first <= 0x7FU)
            {
                unitLength = 1;
            } else if ((first & 0xE0U) == 0xC0U)
            {
                unitLength = 2;
            } else if ((first & 0xF0U) == 0xE0U)
            {
                unitLength = 3;
            } else
            {
                unitLength = 4;
            }
            if (unitLength > utf8.size() - index)
            {
                break;
            }
            if (!(unitLength == 1 && first == '\n'))
            {
                ++count;
            }
            index += unitLength;
        }
        return count;
    }

    [[nodiscard]] static float normalizedRangeFraction(float value, float minValue, float maxValue) noexcept
    {
        if (!(std::isfinite(value) && std::isfinite(minValue) && std::isfinite(maxValue) && maxValue > minValue))
        {
            return 0.0F;
        }
        const double numerator = static_cast<double>(value) - static_cast<double>(minValue);
        const double denominator = static_cast<double>(maxValue) - static_cast<double>(minValue);
        return static_cast<float>(std::clamp(numerator / denominator, 0.0, 1.0));
    }

    struct SliderTrackGeometry final {
        float verticalInset = 0.0F;
        float thumbWidth = 0.0F;
        float startCenterX = 0.0F;
        float endCenterX = 0.0F;
    };

    [[nodiscard]] static SliderTrackGeometry sliderTrackGeometry(UILogicalRect worldRect,
                                                                 const SliderState& slider) noexcept
    {
        const float width = (std::max)(0.0F, worldRect.width);
        const float height = (std::max)(0.0F, worldRect.height);
        const float horizontalInset = (std::min)(slider.paint.contentInset, width * 0.5F);
        const float verticalInset = (std::min)(slider.paint.contentInset, height * 0.5F);
        const float thumbWidth = (std::min)(slider.paint.thumbWidth, width);
        const float minimumCenterX = worldRect.x + thumbWidth * 0.5F;
        const float maximumCenterX = worldRect.x + width - thumbWidth * 0.5F;
        const float rawStartCenterX = worldRect.x + horizontalInset;
        const float rawEndCenterX = worldRect.x + width - horizontalInset;
        return SliderTrackGeometry{
            .verticalInset = verticalInset,
            .thumbWidth = thumbWidth,
            .startCenterX = std::clamp(rawStartCenterX, minimumCenterX, maximumCenterX),
            .endCenterX = std::clamp(rawEndCenterX, minimumCenterX, maximumCenterX),
        };
    }

    struct SliderPaintGeometry final {
        UILogicalRect filledTrack{};
        UILogicalRect thumb{};
        float fraction = 0.0F;
    };

    [[nodiscard]] static SliderPaintGeometry sliderPaintGeometry(UILogicalRect worldRect,
                                                                 const SliderState& slider) noexcept
    {
        const float fraction = normalizedRangeFraction(slider.value, slider.minValue, slider.maxValue);
        const SliderTrackGeometry track = sliderTrackGeometry(worldRect, slider);
        const float centerSpan = (std::max)(0.0F, track.endCenterX - track.startCenterX);
        const float thumbCenterX = track.startCenterX + centerSpan * fraction;
        const float thumbX = thumbCenterX - track.thumbWidth * 0.5F;
        const float trackHeight = (std::max)(0.0F, worldRect.height - track.verticalInset * 2.0F);
        return SliderPaintGeometry{
            .filledTrack =
                UILogicalRect{
                    .x = normalizeFloat(track.startCenterX),
                    .y = normalizeFloat(worldRect.y + track.verticalInset),
                    .width = normalizeFloat(centerSpan * fraction),
                    .height = normalizeFloat(trackHeight),
                },
            .thumb =
                UILogicalRect{
                    .x = normalizeFloat(thumbX),
                    .y = worldRect.y,
                    .width = normalizeFloat(track.thumbWidth),
                    .height = worldRect.height,
                },
            .fraction = fraction,
        };
    }

    [[nodiscard]] static constexpr UIPremultipliedRgba8Color applyOpacity(UIPremultipliedRgba8Color color,
                                                                          u8 opacity) noexcept
    {
        const auto scale = [opacity](u8 channel) constexpr noexcept -> u8 {
            return static_cast<u8>((static_cast<u16>(channel) * static_cast<u16>(opacity) + u16{127}) / u16{255});
        };
        return UIPremultipliedRgba8Color{
            .red = scale(color.red),
            .green = scale(color.green),
            .blue = scale(color.blue),
            .alpha = scale(color.alpha),
        };
    }

    [[nodiscard]] UIPremultipliedRgba8Color widgetPaintColor(UINodeId node,
                                                             UIPremultipliedRgba8Color color) const noexcept
    {
        constexpr u8 DisabledOpacity = 140;
        return isCandidateNodeEnabled(node) ? color : applyOpacity(color, DisabledOpacity);
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedBoxFillColor(UINodeId node, u32 nodeIndex,
                                                                 UIPremultipliedRgba8Color normalColor) const noexcept
    {
        UIPremultipliedRgba8Color color = normalColor;
        const NodeRecord* record = recordByIndex(nodeIndex);
        if (record == nullptr || !isButtonChromeKind(record->kind) || nodeIndex >= buttonPaintsByNodeIndex.size())
        {
            return widgetPaintColor(node, color);
        }

        const UIButtonPaint& paint = buttonPaintsByNodeIndex[nodeIndex];
        const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
            if (overrideColor.alpha != 0)
            {
                color = premultiply(overrideColor);
            }
        };
        if (defaultActionFocusButton == node)
        {
            applyOverride(paint.focusedBackgroundColor);
        }
        if (hoveredPrimaryButton == node)
        {
            applyOverride(paint.hoveredBackgroundColor);
        }
        if (isButtonPressed(node))
        {
            applyOverride(paint.pressedBackgroundColor);
        }
        if (!isNodeEnabled(node))
        {
            applyOverride(paint.disabledBackgroundColor);
        }
        return widgetPaintColor(node, color);
    }

    [[nodiscard]] UIBoxPaint resolvedBoxChrome(UINodeId node, u32 nodeIndex) const noexcept
    {
        UIBoxPaint chrome = boxPaintsByIndex[nodeIndex];
        const NodeRecord* record = recordByIndex(nodeIndex);
        if (record == nullptr || !isButtonChromeKind(record->kind) || nodeIndex >= buttonPaintsByNodeIndex.size() ||
            !isCandidateNodeEnabled(node))
        {
            return chrome;
        }

        if (isButtonPressed(node))
        {
            std::swap(chrome.borderLight, chrome.borderDark);
            chrome.shadowOffsetX = 0.0F;
            chrome.shadowOffsetY = 0.0F;
            return chrome;
        }

        const UIButtonPaint& states = buttonPaintsByNodeIndex[nodeIndex];
        if (defaultActionFocusButton == node && states.focusedBorderColor.alpha != 0)
        {
            chrome.borderLight = states.focusedBorderColor;
            chrome.borderDark = states.focusedBorderColor;
            if (!(chrome.borderWidth > 0.0F))
            {
                chrome.borderWidth = 1.0F;
            }
        }
        return chrome;
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedRadioIndicatorColor(UINodeId node, u32 nodeIndex) const noexcept
    {
        if (nodeIndex >= radioButtonStatesByNodeIndex.size())
        {
            return {};
        }

        const UIRadioButtonPaint& paint = radioButtonStatesByNodeIndex[nodeIndex].paint;
        UIPremultipliedRgba8Color color = premultiply(paint.indicatorColor);
        const auto applyOverride = [&color](UIStraightSrgba8Color overrideColor) noexcept {
            if (overrideColor.alpha != 0)
            {
                color = premultiply(overrideColor);
            }
        };
        if (defaultActionFocusButton == node)
        {
            applyOverride(paint.focusedIndicatorColor);
        }
        if (isButtonPressed(node))
        {
            applyOverride(paint.pressedIndicatorColor);
        }
        return widgetPaintColor(node, color);
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedDropdownSelectionColor(UINodeId item) const noexcept
    {
        const UINodeId dropdown = dropdownForItem(item);
        if (!dropdown.hasValue() || dropdown.index() >= dropdownStatesByNodeIndex.size() ||
            dropdownStatesByNodeIndex[dropdown.index()].selectedItem != item)
        {
            return {};
        }
        return widgetPaintColor(item,
                                premultiply(dropdownStatesByNodeIndex[dropdown.index()].paint
                                                .selectedItemBackgroundColor));
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedListViewSelectionColor(UINodeId item) const noexcept
    {
        const UINodeId listView = listViewForItem(item);
        if (!listView.hasValue() || listView.index() >= listViewStatesByNodeIndex.size() ||
            !isSelectedListViewItem(item))
        {
            return {};
        }
        return widgetPaintColor(
            item, premultiply(listViewStatesByNodeIndex[listView.index()].paint.selectedItemBackgroundColor));
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedTreeViewSelectionColor(UINodeId item) const noexcept
    {
        const UINodeId treeView = treeViewForItem(item);
        if (!treeView.hasValue() || treeView.index() >= treeViewStatesByNodeIndex.size() ||
            !isSelectedTreeViewItem(item))
        {
            return {};
        }
        return widgetPaintColor(
            item, premultiply(treeViewStatesByNodeIndex[treeView.index()].paint.selectedItemBackgroundColor));
    }

    [[nodiscard]] UIPremultipliedRgba8Color resolvedTreeViewDisclosureColor(UINodeId item) const noexcept
    {
        const UINodeId treeView = treeViewForItem(item);
        if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size())
        {
            return {};
        }
        const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
        if (!itemState.bound || !itemState.expandable)
        {
            return {};
        }
        return widgetPaintColor(item, premultiply(treeViewStatesByNodeIndex[treeView.index()].paint.disclosureColor));
    }

    [[nodiscard]] Core::Result<usize>
    validatePaintCandidateCapacity(std::span<const UICommittedLayoutEntry> layoutEntries) const
    {
        usize paintEntryCount = 0;
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            if (layoutEntry.effectiveVisibility != UIVisibility::Visible)
            {
                continue;
            }
            if (!contains(layoutEntry.node))
            {
                return fail(UIErrorCode::InvalidNode, "UI paint snapshot layout references a stale node");
            }
            const u32 nodeIndex = layoutEntry.node.index();
            const UIBoxPaint boxPaint = resolvedBoxChrome(layoutEntry.node, nodeIndex);
            const UIPremultipliedRgba8Color normalColor =
                boxPaint.solidFill.has_value() ? premultiply(boxPaint.solidFill->color) : UIPremultipliedRgba8Color{};
            const UIPremultipliedRgba8Color resolvedFill =
                resolvedBoxFillColor(layoutEntry.node, nodeIndex, normalColor);
            paintEntryCount +=
                countBoxChromePaintEntries(boxPaint, layoutEntry.worldRect, !resolvedFill.isTransparent());
            const NodeRecord* record = recordByIndex(nodeIndex);
            if (record != nullptr && record->kind == UIWidgetKind::Checkbox &&
                nodeIndex < checkboxCheckedByNodeIndex.size() && nodeIndex < checkboxPaintsByNodeIndex.size() &&
                checkboxCheckedByNodeIndex[nodeIndex] != 0)
            {
                const UICheckboxPaint& checkboxPaint = checkboxPaintsByNodeIndex[nodeIndex];
                const float indicatorExtent = (std::min)(layoutEntry.worldRect.width, layoutEntry.worldRect.height);
                if (checkboxPaint.checkedIndicatorColor.alpha != 0 &&
                    indicatorExtent > checkboxPaint.checkedIndicatorInset * 2.0F)
                {
                    ++paintEntryCount;
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::Slider && nodeIndex < sliderStatesByNodeIndex.size())
            {
                const SliderState& slider = sliderStatesByNodeIndex[nodeIndex];
                const SliderPaintGeometry geometry = sliderPaintGeometry(layoutEntry.worldRect, slider);
                if (geometry.filledTrack.width > 0.0F && geometry.filledTrack.height > 0.0F &&
                    geometry.fraction > 0.0F && slider.paint.filledTrackColor.alpha != 0)
                {
                    ++paintEntryCount;
                }
                const UIStraightSrgba8Color thumbColor =
                    armedSlider == layoutEntry.node && slider.paint.draggingThumbColor.alpha != 0
                        ? slider.paint.draggingThumbColor
                        : slider.paint.thumbColor;
                if (geometry.thumb.width > 0.0F && geometry.thumb.height > 0.0F && thumbColor.alpha != 0)
                {
                    ++paintEntryCount;
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::ScrollView &&
                nodeIndex < scrollViewStatesByNodeIndex.size() &&
                nodeIndex < scrollViewLayoutScratchByNodeIndex.size())
            {
                const ScrollViewState& scroll = scrollViewStatesByNodeIndex[nodeIndex];
                const ScrollViewLayoutScratch& scrollLayout = scrollViewLayoutScratchByNodeIndex[nodeIndex];
                const auto countBar = [&](UIScrollAxes axis) noexcept {
                    const ScrollBarGeometry geometry =
                        makeScrollBarGeometry(scrollLayout.metrics, scrollLayout.viewportRect, scroll.paint, axis);
                    if (!geometry.visible)
                    {
                        return;
                    }
                    if (geometry.track.width > 0.0F && geometry.track.height > 0.0F && scroll.paint.trackColor.alpha != 0)
                    {
                        ++paintEntryCount;
                    }
                    const UIStraightSrgba8Color thumbColor =
                        scrollThumbDragActive && armedScrollView == layoutEntry.node && armedScrollAxis == axis &&
                                scroll.paint.draggingThumbColor.alpha != 0
                            ? scroll.paint.draggingThumbColor
                            : scroll.paint.thumbColor;
                    if (geometry.thumb.width > 0.0F && geometry.thumb.height > 0.0F && thumbColor.alpha != 0)
                    {
                        ++paintEntryCount;
                    }
                };
                countBar(UIScrollAxes::Horizontal);
                countBar(UIScrollAxes::Vertical);
            }
            if (record != nullptr && record->kind == UIWidgetKind::ListView &&
                nodeIndex < listViewStatesByNodeIndex.size() && nodeIndex < listViewLayoutScratchByNodeIndex.size())
            {
                const ListViewState& list = listViewStatesByNodeIndex[nodeIndex];
                const ListViewLayoutScratch& listLayout = listViewLayoutScratchByNodeIndex[nodeIndex];
                const ScrollBarGeometry geometry =
                    makeListViewScrollBarGeometry(listLayout.metrics, listLayout.viewportRect, list.paint.scrollBar);
                if (geometry.visible)
                {
                    if (geometry.track.width > 0.0F && geometry.track.height > 0.0F &&
                        list.paint.scrollBar.trackColor.alpha != 0)
                    {
                        ++paintEntryCount;
                    }
                    const UIStraightSrgba8Color thumbColor =
                        scrollThumbDragActive && armedScrollView == layoutEntry.node &&
                                list.paint.scrollBar.draggingThumbColor.alpha != 0
                            ? list.paint.scrollBar.draggingThumbColor
                            : list.paint.scrollBar.thumbColor;
                    if (geometry.thumb.width > 0.0F && geometry.thumb.height > 0.0F && thumbColor.alpha != 0)
                    {
                        ++paintEntryCount;
                    }
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::TreeView &&
                nodeIndex < treeViewStatesByNodeIndex.size() && nodeIndex < treeViewLayoutScratchByNodeIndex.size())
            {
                const TreeViewState& tree = treeViewStatesByNodeIndex[nodeIndex];
                const TreeViewLayoutScratch& treeLayout = treeViewLayoutScratchByNodeIndex[nodeIndex];
                const ScrollBarGeometry geometry =
                    makeTreeViewScrollBarGeometry(treeLayout.metrics, treeLayout.viewportRect, tree.paint.scrollBar);
                if (geometry.visible)
                {
                    if (geometry.track.width > 0.0F && geometry.track.height > 0.0F &&
                        tree.paint.scrollBar.trackColor.alpha != 0)
                    {
                        ++paintEntryCount;
                    }
                    const UIStraightSrgba8Color thumbColor = scrollThumbDragActive &&
                                                                     armedScrollView == layoutEntry.node &&
                                                                     tree.paint.scrollBar.draggingThumbColor.alpha != 0
                                                                 ? tree.paint.scrollBar.draggingThumbColor
                                                                 : tree.paint.scrollBar.thumbColor;
                    if (geometry.thumb.width > 0.0F && geometry.thumb.height > 0.0F && thumbColor.alpha != 0)
                    {
                        ++paintEntryCount;
                    }
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::Dropdown &&
                nodeIndex < dropdownStatesByNodeIndex.size())
            {
                const UIDropdownPaint& dropdown = dropdownStatesByNodeIndex[nodeIndex].paint;
                if (dropdown.indicatorColor.alpha != 0 && dropdown.indicatorWidth > 0.0F &&
                    dropdown.indicatorHeight > 0.0F)
                {
                    paintEntryCount += 3;
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::DropdownItem &&
                !resolvedDropdownSelectionColor(layoutEntry.node).isTransparent())
            {
                ++paintEntryCount;
            }
            if (record != nullptr && record->kind == UIWidgetKind::ListViewItem &&
                !resolvedListViewSelectionColor(layoutEntry.node).isTransparent())
            {
                ++paintEntryCount;
            }
            if (record != nullptr && record->kind == UIWidgetKind::TreeViewItem)
            {
                if (!resolvedTreeViewSelectionColor(layoutEntry.node).isTransparent())
                {
                    ++paintEntryCount;
                }
                const UIPremultipliedRgba8Color disclosure = resolvedTreeViewDisclosureColor(layoutEntry.node);
                if (!disclosure.isTransparent())
                {
                    const TreeViewItemState& item = treeViewItemStatesByNodeIndex[nodeIndex];
                    paintEntryCount += item.expanded ? 1U : 2U;
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::ProgressBar &&
                nodeIndex < progressBarStatesByNodeIndex.size())
            {
                const ProgressBarState& progress = progressBarStatesByNodeIndex[nodeIndex];
                const float fraction = normalizedRangeFraction(progress.value, progress.minValue, progress.maxValue);
                if (fraction > 0.0F && progress.paint.fillColor.alpha != 0 && layoutEntry.worldRect.width > 0.0F &&
                    layoutEntry.worldRect.height > 0.0F)
                {
                    ++paintEntryCount;
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::RadioButton &&
                nodeIndex < radioButtonStatesByNodeIndex.size())
            {
                const RadioButtonState& radio = radioButtonStatesByNodeIndex[nodeIndex];
                const float indicatorExtent = (std::min)(layoutEntry.worldRect.width, layoutEntry.worldRect.height);
                const float inset = radio.paint.selectedIndicatorInset;
                if (!resolvedRadioIndicatorColor(layoutEntry.node, nodeIndex).isTransparent() && indicatorExtent > 0.0F)
                {
                    ++paintEntryCount;
                }
                if (radio.selected && radio.paint.selectedIndicatorColor.alpha != 0 && indicatorExtent > inset * 2.0F)
                {
                    ++paintEntryCount;
                }
            }
            const bool focusedTextEdit = record != nullptr && record->kind == UIWidgetKind::TextEdit &&
                                         isNodeEnabled(layoutEntry.node) && textInputFocus == layoutEntry.node &&
                                         isLiveTextEdit(textInputFocus);
            const bool activeIme = focusedTextEdit && imeComposition.active();
            if (nodeIndex < textStatesByIndex.size() && !presentationTextViewFor(nodeIndex).empty() &&
                textStatesByIndex[nodeIndex].style.color.alpha != 0)
            {
                const usize committedCodepoints = countDrawableTextCodepoints(presentationTextViewFor(nodeIndex));
                if (activeIme && nodeIndex < textEditStatesByNodeIndex.size())
                {
                    const UITextSelection selection = textEditStatesByNodeIndex[nodeIndex].selection;
                    const usize selectedCodepoints =
                        static_cast<usize>((std::max)(selection.anchorCodepoint, selection.caretCodepoint) -
                                           (std::min)(selection.anchorCodepoint, selection.caretCodepoint));
                    paintEntryCount += committedCodepoints - (std::min)(committedCodepoints, selectedCodepoints);
                } else
                {
                    paintEntryCount += committedCodepoints;
                }
            }
            if (focusedTextEdit)
            {
                if (activeIme)
                {
                    // Active IME preedit replaces both the selected text and its
                    // highlight, so count only the replacement glyphs.
                    paintEntryCount += countDrawableTextCodepoints(imeComposition.preeditUtf8());
                } else if (nodeIndex < textEditStatesByNodeIndex.size() &&
                           !textEditStatesByNodeIndex[nodeIndex].selection.isCollapsed())
                {
                    ++paintEntryCount;
                }
                ++paintEntryCount;
            }
        }
        if (paintEntryCount > capacityConfig.paintSnapshotCapacity)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI committed paint snapshot capacity has been exhausted");
        }
        return paintEntryCount;
    }

    [[nodiscard]] usize rebuildDirtyPaintCaches(std::span<const UICommittedLayoutEntry> layoutEntries) noexcept
    {
        usize rebuildCount = 0;
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            const u32 nodeIndex = layoutEntry.node.index();
            if (nodeIndex >= dirtyByIndex.size() || !hasDirty(dirtyByIndex[nodeIndex], UIDirty::Paint))
            {
                continue;
            }

            const UIBoxPaint& paint = boxPaintsByIndex[nodeIndex];
            localSolidFillCacheByIndex[nodeIndex] =
                paint.solidFill.has_value() ? premultiply(paint.solidFill->color) : UIPremultipliedRgba8Color{};
            localTextColorCacheByIndex[nodeIndex] =
                nodeIndex < textStatesByIndex.size() && textStatesByIndex[nodeIndex].hasContent
                    ? premultiply(textStatesByIndex[nodeIndex].style.color)
                    : UIPremultipliedRgba8Color{};
            ++rebuildCount;
        }
        return rebuildCount;
    }

    // Shared UTF-8 → glyph/solid paint emitter. Returns final cursor after the
    // last drawable codepoint (for chaining IME preedit after committed text).
    struct TextPaintCursor final {
        float x = 0.0F;
        float y = 0.0F;
        float lineHeight = 0.0F;
        float baseX = 0.0F;
    };

    void appendUtf8TextPaints(std::pmr::vector<UICommittedPaintEntry>& output,
                              const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal, std::string_view utf8,
                              const UITextStyle& style, UIPremultipliedRgba8Color color, float startX, float startY,
                              TextPaintCursor* outCursor) noexcept
    {
        const float lineStartX = outCursor != nullptr ? outCursor->baseX : startX;
        if (outCursor != nullptr)
        {
            *outCursor = TextPaintCursor{
                .x = startX,
                .y = startY,
                .lineHeight = style.logicalSize * style.lineHeightScale,
                .baseX = lineStartX,
            };
        }
        if (utf8.empty() || color.isTransparent())
        {
            return;
        }
        const float fallbackAdvance = style.logicalSize * style.advanceScale;
        const float lineHeight = style.logicalSize * style.lineHeightScale;
        if (!(std::isfinite(fallbackAdvance) && fallbackAdvance > 0.0F && std::isfinite(lineHeight) &&
              lineHeight > 0.0F))
        {
            return;
        }
        const u32 pixelSize = static_cast<u32>((std::max)(1.0F, std::floor(style.logicalSize)));

        if (textRasterizer && textFace.hasValue() && glyphAtlas)
        {
            auto batch = textRasterizer->raster(textFace, utf8, style);
            if (batch)
            {
                const usize outputBase = output.size();
                const u32 ordinalBase = nextPaintOrdinal;
                float cursorX = startX;
                float cursorY = startY;
                usize glyphIndex = 0;
                usize index = 0;
                bool usedAtlasPath = true;
                while (index < utf8.size())
                {
                    const auto first = static_cast<unsigned char>(utf8[index]);
                    usize unitLength = 1;
                    if (first <= 0x7FU)
                    {
                        unitLength = 1;
                    } else if ((first & 0xE0U) == 0xC0U)
                    {
                        unitLength = 2;
                    } else if ((first & 0xF0U) == 0xE0U)
                    {
                        unitLength = 3;
                    } else
                    {
                        unitLength = 4;
                    }
                    if (unitLength > utf8.size() - index)
                    {
                        usedAtlasPath = false;
                        break;
                    }
                    if (unitLength == 1 && first == '\n')
                    {
                        cursorX = lineStartX;
                        cursorY = normalizeFloat(cursorY + lineHeight);
                        index += unitLength;
                        continue;
                    }
                    if (glyphIndex >= batch->glyphs.size())
                    {
                        usedAtlasPath = false;
                        break;
                    }
                    const UITextGlyphRaster& glyph = batch->glyphs[glyphIndex];
                    ++glyphIndex;
                    float advance = glyph.advance;
                    if (!(std::isfinite(advance) && advance > 0.0F))
                    {
                        advance = fallbackAdvance;
                    }

                    if (glyph.width == 0 || glyph.height == 0)
                    {
                        cursorX = normalizeFloat(cursorX + advance);
                        index += unitLength;
                        continue;
                    }

                    std::span<const u8> coverage{};
                    const usize coverageBytes = static_cast<usize>(glyph.width) * glyph.height;
                    if (glyph.coverageOffset + coverageBytes > batch->coverage.size())
                    {
                        usedAtlasPath = false;
                        break;
                    }
                    coverage = std::span<const u8>(batch->coverage.data() + glyph.coverageOffset, coverageBytes);
                    auto placed = glyphAtlas->insert(
                        UIGlyphKey{
                            .face = textFace,
                            .codepoint = glyph.codepoint,
                            .pixelSize = pixelSize,
                        },
                        glyph, coverage);
                    if (!placed)
                    {
                        usedAtlasPath = false;
                        break;
                    }

                    const float drawX = normalizeFloat(cursorX + glyph.bearingX);
                    const float drawY = normalizeFloat(cursorY + (lineHeight - glyph.bearingY));
                    const float drawW = static_cast<float>(placed->width);
                    const float drawH = static_cast<float>(placed->height);

                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect =
                            UILogicalRect{
                                .x = drawX,
                                .y = drawY,
                                .width = normalizeFloat((std::max)(0.0F, drawW)),
                                .height = normalizeFloat((std::max)(0.0F, drawH)),
                            },
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal,
                        .solidFill = color,
                        .isGlyph = true,
                        .atlasX = placed->atlasX,
                        .atlasY = placed->atlasY,
                        .atlasWidth = placed->width,
                        .atlasHeight = placed->height,
                        .atlasPage = 0,
                    });
                    ++nextPaintOrdinal;
                    cursorX = normalizeFloat(cursorX + advance);
                    index += unitLength;
                }
                if (usedAtlasPath)
                {
                    if (outCursor != nullptr)
                    {
                        outCursor->x = cursorX;
                        outCursor->y = cursorY;
                        outCursor->lineHeight = lineHeight;
                        outCursor->baseX = lineStartX;
                    }
                    return;
                }
                while (output.size() > outputBase)
                {
                    output.pop_back();
                }
                nextPaintOrdinal = ordinalBase;
            }
        }

        float cursorX = startX;
        float cursorY = startY;
        usize index = 0;
        while (index < utf8.size())
        {
            const auto first = static_cast<unsigned char>(utf8[index]);
            usize unitLength = 1;
            if (first <= 0x7FU)
            {
                unitLength = 1;
            } else if ((first & 0xE0U) == 0xC0U)
            {
                unitLength = 2;
            } else if ((first & 0xF0U) == 0xE0U)
            {
                unitLength = 3;
            } else
            {
                unitLength = 4;
            }
            if (unitLength > utf8.size() - index)
            {
                break;
            }
            if (unitLength == 1 && first == '\n')
            {
                cursorX = lineStartX;
                cursorY = normalizeFloat(cursorY + lineHeight);
                index += unitLength;
                continue;
            }
            output.push_back(UICommittedPaintEntry{
                .node = layoutEntry.node,
                .worldRect =
                    UILogicalRect{
                        .x = normalizeFloat(cursorX),
                        .y = normalizeFloat(cursorY),
                        .width = normalizeFloat(fallbackAdvance),
                        .height = normalizeFloat(lineHeight),
                    },
                .effectiveClip = layoutEntry.effectiveClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = color,
                .isGlyph = false,
            });
            ++nextPaintOrdinal;
            cursorX = normalizeFloat(cursorX + fallbackAdvance);
            index += unitLength;
        }
        if (outCursor != nullptr)
        {
            outCursor->x = cursorX;
            outCursor->y = cursorY;
            outCursor->lineHeight = lineHeight;
            outCursor->baseX = lineStartX;
        }
    }

    void appendTextGlyphPaints(std::pmr::vector<UICommittedPaintEntry>& output,
                               const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal) noexcept
    {
        const u32 nodeIndex = layoutEntry.node.index();
        const NodeRecord* record = recordByIndex(nodeIndex);
        const bool focusedTextEdit = record != nullptr && record->kind == UIWidgetKind::TextEdit &&
                                     isNodeEnabled(layoutEntry.node) && textInputFocus == layoutEntry.node &&
                                     isLiveTextEdit(textInputFocus);
        const WidgetTextState* textState =
            nodeIndex < textStatesByIndex.size() ? &textStatesByIndex[nodeIndex] : nullptr;
        const UITextStyle style = textState != nullptr ? textState->style : UITextStyle{};
        const UIPremultipliedRgba8Color textColor =
            nodeIndex < localTextColorCacheByIndex.size()
                ? widgetPaintColor(layoutEntry.node, localTextColorCacheByIndex[nodeIndex])
                : UIPremultipliedRgba8Color{};
        const std::string_view committedText = presentationTextViewFor(nodeIndex);
        const UIEdgeSpacing padding =
            nodeIndex < layoutStylesByIndex.size() ? layoutStylesByIndex[nodeIndex].padding : UIEdgeSpacing{};
        UICommittedLayoutEntry textLayoutEntry = layoutEntry;
        if (record != nullptr && record->kind == UIWidgetKind::Dropdown &&
            nodeIndex < dropdownStatesByNodeIndex.size())
        {
            const UIDropdownPaint& dropdown = dropdownStatesByNodeIndex[nodeIndex].paint;
            const float reservedWidth = dropdown.indicatorWidth + dropdown.indicatorInset * 2.0F;
            textLayoutEntry.effectiveClip = intersectRects(
                textLayoutEntry.effectiveClip,
                UILogicalRect{
                    .x = layoutEntry.worldRect.x,
                    .y = layoutEntry.worldRect.y,
                    .width = normalizeFloat((std::max)(0.0F, layoutEntry.worldRect.width - reservedWidth)),
                    .height = layoutEntry.worldRect.height,
                });
        }
        float textStartX = normalizeFloat(layoutEntry.worldRect.x + padding.left);
        const float textStartY = normalizeFloat(layoutEntry.worldRect.y + padding.top);
        if (record != nullptr && record->kind == UIWidgetKind::RadioButton &&
            nodeIndex < radioButtonStatesByNodeIndex.size())
        {
            const float indicatorExtent = (std::min)(layoutEntry.worldRect.width, layoutEntry.worldRect.height);
            textStartX = normalizeFloat(layoutEntry.worldRect.x + indicatorExtent +
                                        radioButtonStatesByNodeIndex[nodeIndex].paint.labelGap + padding.left);
        }
        TextPaintCursor cursor{
            .x = textStartX,
            .y = textStartY,
            .lineHeight = style.logicalSize * style.lineHeightScale,
            .baseX = textStartX,
        };
        TextPaintCursor caretCursor = cursor;
        const auto appendText = [&](std::string_view text, UIPremultipliedRgba8Color color) noexcept {
            appendUtf8TextPaints(output, textLayoutEntry, nextPaintOrdinal, text, style, color, cursor.x, cursor.y,
                                 &cursor);
        };

        if (!focusedTextEdit)
        {
            appendText(committedText, textColor);
            return;
        }

        const UITextSelection selection = textEditStatesByNodeIndex[nodeIndex].selection;
        const u32 selectionBegin = (std::min)(selection.anchorCodepoint, selection.caretCodepoint);
        const u32 selectionEnd = (std::max)(selection.anchorCodepoint, selection.caretCodepoint);
        const usize selectionBeginByte = utf8ByteOffsetForCodepoint(committedText, selectionBegin);
        const usize selectionEndByte = utf8ByteOffsetForCodepoint(committedText, selectionEnd);

        appendText(committedText.substr(0, selectionBeginByte), textColor);
        const TextPaintCursor selectionStartCursor = cursor;

        if (imeComposition.active())
        {
            const std::string_view preedit = imeComposition.preeditUtf8();
            const usize preeditCursorByte = utf8ByteOffsetForCodepoint(preedit, imeComposition.cursorCodepoint());
            const UIPremultipliedRgba8Color preeditColor = premultiply(UIStraightSrgba8Color{
                .red = 0,
                .green = 180,
                .blue = 255,
                .alpha = 255,
            });
            appendText(preedit.substr(0, preeditCursorByte), preeditColor);
            caretCursor = cursor;
            appendText(preedit.substr(preeditCursorByte), preeditColor);
            appendText(committedText.substr(selectionEndByte), textColor);
        } else
        {
            usize selectionPaintIndex = output.size();
            if (selectionBegin != selectionEnd)
            {
                output.push_back(UICommittedPaintEntry{
                    .node = layoutEntry.node,
                    .worldRect = {},
                    .effectiveClip = textLayoutEntry.effectiveClip,
                    .paintOrdinal = nextPaintOrdinal,
                    .solidFill = premultiply(UIStraightSrgba8Color{
                        .red = 42,
                        .green = 112,
                        .blue = 190,
                        .alpha = 190,
                    }),
                    .isGlyph = false,
                });
                ++nextPaintOrdinal;
            }
            appendText(committedText.substr(selectionBeginByte, selectionEndByte - selectionBeginByte), textColor);
            const TextPaintCursor selectionEndCursor = cursor;
            if (selectionBegin != selectionEnd)
            {
                output[selectionPaintIndex].worldRect = UILogicalRect{
                    .x = normalizeFloat(selectionStartCursor.x),
                    .y = normalizeFloat(selectionStartCursor.y),
                    .width = normalizeFloat((std::max)(0.0F, selectionEndCursor.x - selectionStartCursor.x)),
                    .height = normalizeFloat((std::max)(1.0F, selectionEndCursor.lineHeight)),
                };
            }
            caretCursor = selection.caretCodepoint == selectionBegin ? selectionStartCursor : selectionEndCursor;
            appendText(committedText.substr(selectionEndByte), textColor);
        }

        {
            float lineHeight = caretCursor.lineHeight;
            if (!(std::isfinite(lineHeight) && lineHeight > 0.0F))
            {
                if (nodeIndex < textStatesByIndex.size())
                {
                    const UITextStyle& style = textStatesByIndex[nodeIndex].style;
                    lineHeight = style.logicalSize * style.lineHeightScale;
                } else
                {
                    lineHeight = 16.0F * 1.2F;
                }
            }
            if (!(std::isfinite(lineHeight) && lineHeight > 0.0F))
            {
                lineHeight = 19.2F;
            }
            constexpr float CaretWidth = 2.0F;
            const UIPremultipliedRgba8Color caretColor = premultiply(UIStraightSrgba8Color{
                .red = 255,
                .green = 255,
                .blue = 255,
                .alpha = 255,
            });
            output.push_back(UICommittedPaintEntry{
                .node = layoutEntry.node,
                .worldRect =
                    UILogicalRect{
                        .x = normalizeFloat(caretCursor.x),
                        .y = normalizeFloat(caretCursor.y),
                        .width = CaretWidth,
                        .height = normalizeFloat(lineHeight),
                    },
                .effectiveClip = textLayoutEntry.effectiveClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = caretColor,
                .isGlyph = false,
            });
            ++nextPaintOrdinal;
        }
    }

    void buildCommittedPaint(std::pmr::vector<UICommittedPaintEntry>& output,
                             std::span<const UICommittedLayoutEntry> layoutEntries) noexcept
    {
        output.clear();
        u32 nextPaintOrdinal = 1;
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            if (layoutEntry.effectiveVisibility != UIVisibility::Visible)
            {
                continue;
            }
            const u32 nodeIndex = layoutEntry.node.index();
            const UIBoxPaint boxPaint = resolvedBoxChrome(layoutEntry.node, nodeIndex);
            const UIPremultipliedRgba8Color fill =
                resolvedBoxFillColor(layoutEntry.node, nodeIndex, localSolidFillCacheByIndex[nodeIndex]);
            appendBoxChromePaints(output, layoutEntry.node, layoutEntry.worldRect, layoutEntry.effectiveClip,
                                  nextPaintOrdinal, boxPaint, fill);
            const NodeRecord* record = recordByIndex(nodeIndex);
            if (record != nullptr && record->kind == UIWidgetKind::Checkbox &&
                nodeIndex < checkboxCheckedByNodeIndex.size() && nodeIndex < checkboxPaintsByNodeIndex.size() &&
                checkboxCheckedByNodeIndex[nodeIndex] != 0)
            {
                const UICheckboxPaint& checkboxPaint = checkboxPaintsByNodeIndex[nodeIndex];
                const float indicatorExtent = (std::min)(layoutEntry.worldRect.width, layoutEntry.worldRect.height);
                const float inset = checkboxPaint.checkedIndicatorInset;
                const UIPremultipliedRgba8Color indicator =
                    widgetPaintColor(layoutEntry.node, premultiply(checkboxPaint.checkedIndicatorColor));
                if (!indicator.isTransparent() && indicatorExtent > inset * 2.0F)
                {
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect =
                            UILogicalRect{
                                .x = normalizeFloat(layoutEntry.worldRect.x + inset),
                                .y = normalizeFloat(layoutEntry.worldRect.y + inset),
                                .width = normalizeFloat(indicatorExtent - inset * 2.0F),
                                .height = normalizeFloat(indicatorExtent - inset * 2.0F),
                            },
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal,
                        .solidFill = indicator,
                    });
                    ++nextPaintOrdinal;
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::Slider && nodeIndex < sliderStatesByNodeIndex.size())
            {
                const SliderState& slider = sliderStatesByNodeIndex[nodeIndex];
                const SliderPaintGeometry geometry = sliderPaintGeometry(layoutEntry.worldRect, slider);
                const UIPremultipliedRgba8Color filledTrack =
                    widgetPaintColor(layoutEntry.node, premultiply(slider.paint.filledTrackColor));
                if (geometry.filledTrack.width > 0.0F && geometry.filledTrack.height > 0.0F &&
                    geometry.fraction > 0.0F && !filledTrack.isTransparent())
                {
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect = geometry.filledTrack,
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal,
                        .solidFill = filledTrack,
                    });
                    ++nextPaintOrdinal;
                }
                const UIStraightSrgba8Color thumbSource =
                    armedSlider == layoutEntry.node && slider.paint.draggingThumbColor.alpha != 0
                        ? slider.paint.draggingThumbColor
                        : slider.paint.thumbColor;
                const UIPremultipliedRgba8Color thumb = widgetPaintColor(layoutEntry.node, premultiply(thumbSource));
                if (geometry.thumb.width > 0.0F && geometry.thumb.height > 0.0F && !thumb.isTransparent())
                {
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect = geometry.thumb,
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal,
                        .solidFill = thumb,
                    });
                    ++nextPaintOrdinal;
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::ScrollView &&
                nodeIndex < scrollViewStatesByNodeIndex.size() &&
                nodeIndex < scrollViewLayoutScratchByNodeIndex.size())
            {
                const ScrollViewState& scroll = scrollViewStatesByNodeIndex[nodeIndex];
                const ScrollViewLayoutScratch& scrollLayout = scrollViewLayoutScratchByNodeIndex[nodeIndex];
                const auto appendBar = [&](UIScrollAxes axis) noexcept {
                    const ScrollBarGeometry geometry =
                        makeScrollBarGeometry(scrollLayout.metrics, scrollLayout.viewportRect, scroll.paint, axis);
                    if (!geometry.visible)
                    {
                        return;
                    }
                    const UIPremultipliedRgba8Color track =
                        widgetPaintColor(layoutEntry.node, premultiply(scroll.paint.trackColor));
                    if (geometry.track.width > 0.0F && geometry.track.height > 0.0F && !track.isTransparent())
                    {
                        output.push_back(UICommittedPaintEntry{
                            .node = layoutEntry.node,
                            .worldRect = geometry.track,
                            .effectiveClip = layoutEntry.effectiveClip,
                            .paintOrdinal = nextPaintOrdinal++,
                            .solidFill = track,
                        });
                    }
                    const UIStraightSrgba8Color thumbSource =
                        scrollThumbDragActive && armedScrollView == layoutEntry.node && armedScrollAxis == axis &&
                                scroll.paint.draggingThumbColor.alpha != 0
                            ? scroll.paint.draggingThumbColor
                            : scroll.paint.thumbColor;
                    const UIPremultipliedRgba8Color thumbColor =
                        widgetPaintColor(layoutEntry.node, premultiply(thumbSource));
                    if (geometry.thumb.width > 0.0F && geometry.thumb.height > 0.0F && !thumbColor.isTransparent())
                    {
                        output.push_back(UICommittedPaintEntry{
                            .node = layoutEntry.node,
                            .worldRect = geometry.thumb,
                            .effectiveClip = layoutEntry.effectiveClip,
                            .paintOrdinal = nextPaintOrdinal++,
                            .solidFill = thumbColor,
                        });
                    }
                };
                appendBar(UIScrollAxes::Horizontal);
                appendBar(UIScrollAxes::Vertical);
            }
            if (record != nullptr && record->kind == UIWidgetKind::ListView &&
                nodeIndex < listViewStatesByNodeIndex.size() && nodeIndex < listViewLayoutScratchByNodeIndex.size())
            {
                const ListViewState& list = listViewStatesByNodeIndex[nodeIndex];
                const ListViewLayoutScratch& listLayout = listViewLayoutScratchByNodeIndex[nodeIndex];
                const ScrollBarGeometry geometry =
                    makeListViewScrollBarGeometry(listLayout.metrics, listLayout.viewportRect, list.paint.scrollBar);
                if (geometry.visible)
                {
                    const UIPremultipliedRgba8Color track =
                        widgetPaintColor(layoutEntry.node, premultiply(list.paint.scrollBar.trackColor));
                    if (geometry.track.width > 0.0F && geometry.track.height > 0.0F && !track.isTransparent())
                    {
                        output.push_back(UICommittedPaintEntry{
                            .node = layoutEntry.node,
                            .worldRect = geometry.track,
                            .effectiveClip = layoutEntry.effectiveClip,
                            .paintOrdinal = nextPaintOrdinal++,
                            .solidFill = track,
                        });
                    }
                    const UIStraightSrgba8Color thumbSource =
                        scrollThumbDragActive && armedScrollView == layoutEntry.node &&
                                list.paint.scrollBar.draggingThumbColor.alpha != 0
                            ? list.paint.scrollBar.draggingThumbColor
                            : list.paint.scrollBar.thumbColor;
                    const UIPremultipliedRgba8Color thumb =
                        widgetPaintColor(layoutEntry.node, premultiply(thumbSource));
                    if (geometry.thumb.width > 0.0F && geometry.thumb.height > 0.0F && !thumb.isTransparent())
                    {
                        output.push_back(UICommittedPaintEntry{
                            .node = layoutEntry.node,
                            .worldRect = geometry.thumb,
                            .effectiveClip = layoutEntry.effectiveClip,
                            .paintOrdinal = nextPaintOrdinal++,
                            .solidFill = thumb,
                        });
                    }
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::TreeView &&
                nodeIndex < treeViewStatesByNodeIndex.size() && nodeIndex < treeViewLayoutScratchByNodeIndex.size())
            {
                const TreeViewState& tree = treeViewStatesByNodeIndex[nodeIndex];
                const TreeViewLayoutScratch& treeLayout = treeViewLayoutScratchByNodeIndex[nodeIndex];
                const ScrollBarGeometry geometry =
                    makeTreeViewScrollBarGeometry(treeLayout.metrics, treeLayout.viewportRect, tree.paint.scrollBar);
                if (geometry.visible)
                {
                    const UIPremultipliedRgba8Color track =
                        widgetPaintColor(layoutEntry.node, premultiply(tree.paint.scrollBar.trackColor));
                    if (geometry.track.width > 0.0F && geometry.track.height > 0.0F && !track.isTransparent())
                    {
                        output.push_back(UICommittedPaintEntry{
                            .node = layoutEntry.node,
                            .worldRect = geometry.track,
                            .effectiveClip = layoutEntry.effectiveClip,
                            .paintOrdinal = nextPaintOrdinal++,
                            .solidFill = track,
                        });
                    }
                    const UIStraightSrgba8Color thumbSource = scrollThumbDragActive &&
                                                                      armedScrollView == layoutEntry.node &&
                                                                      tree.paint.scrollBar.draggingThumbColor.alpha != 0
                                                                  ? tree.paint.scrollBar.draggingThumbColor
                                                                  : tree.paint.scrollBar.thumbColor;
                    const UIPremultipliedRgba8Color thumb =
                        widgetPaintColor(layoutEntry.node, premultiply(thumbSource));
                    if (geometry.thumb.width > 0.0F && geometry.thumb.height > 0.0F && !thumb.isTransparent())
                    {
                        output.push_back(UICommittedPaintEntry{
                            .node = layoutEntry.node,
                            .worldRect = geometry.thumb,
                            .effectiveClip = layoutEntry.effectiveClip,
                            .paintOrdinal = nextPaintOrdinal++,
                            .solidFill = thumb,
                        });
                    }
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::Dropdown &&
                nodeIndex < dropdownStatesByNodeIndex.size())
            {
                const UIDropdownPaint& dropdown = dropdownStatesByNodeIndex[nodeIndex].paint;
                const UIPremultipliedRgba8Color indicator =
                    widgetPaintColor(layoutEntry.node, premultiply(dropdown.indicatorColor));
                if (!indicator.isTransparent() && dropdown.indicatorWidth > 0.0F &&
                    dropdown.indicatorHeight > 0.0F)
                {
                    constexpr usize StripeCount = 3;
                    const float stripeHeight = dropdown.indicatorHeight / static_cast<float>(StripeCount);
                    const float centerX = layoutEntry.worldRect.right() - dropdown.indicatorInset -
                                          dropdown.indicatorWidth * 0.5F;
                    const float top = layoutEntry.worldRect.y +
                                      (layoutEntry.worldRect.height - dropdown.indicatorHeight) * 0.5F;
                    for (usize stripe = 0; stripe < StripeCount; ++stripe)
                    {
                        const float width = dropdown.indicatorWidth *
                                            static_cast<float>(StripeCount - stripe) /
                                            static_cast<float>(StripeCount);
                        output.push_back(UICommittedPaintEntry{
                            .node = layoutEntry.node,
                            .worldRect =
                                UILogicalRect{
                                    .x = normalizeFloat(centerX - width * 0.5F),
                                    .y = normalizeFloat(top + stripeHeight * static_cast<float>(stripe)),
                                    .width = normalizeFloat(width),
                                    .height = normalizeFloat(stripeHeight),
                                },
                            .effectiveClip = layoutEntry.effectiveClip,
                            .paintOrdinal = nextPaintOrdinal++,
                            .solidFill = indicator,
                        });
                    }
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::DropdownItem)
            {
                const UIPremultipliedRgba8Color selection = resolvedDropdownSelectionColor(layoutEntry.node);
                if (!selection.isTransparent())
                {
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect = layoutEntry.worldRect,
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal++,
                        .solidFill = selection,
                    });
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::ListViewItem)
            {
                const UIPremultipliedRgba8Color selection = resolvedListViewSelectionColor(layoutEntry.node);
                if (!selection.isTransparent())
                {
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect = layoutEntry.worldRect,
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal++,
                        .solidFill = selection,
                    });
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::TreeViewItem &&
                nodeIndex < treeViewItemStatesByNodeIndex.size())
            {
                const UIPremultipliedRgba8Color selection = resolvedTreeViewSelectionColor(layoutEntry.node);
                if (!selection.isTransparent())
                {
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect = layoutEntry.worldRect,
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal++,
                        .solidFill = selection,
                    });
                }
                const TreeViewItemState& item = treeViewItemStatesByNodeIndex[nodeIndex];
                const UINodeId treeView = treeViewForItem(layoutEntry.node);
                const UIPremultipliedRgba8Color disclosure = resolvedTreeViewDisclosureColor(layoutEntry.node);
                if (treeView.hasValue() && !disclosure.isTransparent())
                {
                    const UITreeViewStyle& style = treeViewStatesByNodeIndex[treeView.index()].style;
                    const UILogicalRect disclosureRect =
                        makeTreeViewDisclosureRect(layoutEntry.worldRect, style, item.level);
                    const float stroke = normalizeFloat((std::max)(1.0F, disclosureRect.height / 6.0F));
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect =
                            UILogicalRect{
                                .x = disclosureRect.x,
                                .y = normalizeFloat(disclosureRect.y + (disclosureRect.height - stroke) * 0.5F),
                                .width = disclosureRect.width,
                                .height = stroke,
                            },
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal++,
                        .solidFill = disclosure,
                    });
                    if (!item.expanded)
                    {
                        output.push_back(UICommittedPaintEntry{
                            .node = layoutEntry.node,
                            .worldRect =
                                UILogicalRect{
                                    .x = normalizeFloat(disclosureRect.x + (disclosureRect.width - stroke) * 0.5F),
                                    .y = disclosureRect.y,
                                    .width = stroke,
                                    .height = disclosureRect.height,
                                },
                            .effectiveClip = layoutEntry.effectiveClip,
                            .paintOrdinal = nextPaintOrdinal++,
                            .solidFill = disclosure,
                        });
                    }
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::ProgressBar &&
                nodeIndex < progressBarStatesByNodeIndex.size())
            {
                const ProgressBarState& progress = progressBarStatesByNodeIndex[nodeIndex];
                const float fraction = normalizedRangeFraction(progress.value, progress.minValue, progress.maxValue);
                const UIPremultipliedRgba8Color progressFill =
                    widgetPaintColor(layoutEntry.node, premultiply(progress.paint.fillColor));
                if (fraction > 0.0F && !progressFill.isTransparent() && layoutEntry.worldRect.width > 0.0F &&
                    layoutEntry.worldRect.height > 0.0F)
                {
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect =
                            UILogicalRect{
                                .x = layoutEntry.worldRect.x,
                                .y = layoutEntry.worldRect.y,
                                .width = normalizeFloat(layoutEntry.worldRect.width * fraction),
                                .height = layoutEntry.worldRect.height,
                            },
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal,
                        .solidFill = progressFill,
                    });
                    ++nextPaintOrdinal;
                }
            }
            if (record != nullptr && record->kind == UIWidgetKind::RadioButton &&
                nodeIndex < radioButtonStatesByNodeIndex.size())
            {
                const RadioButtonState& radio = radioButtonStatesByNodeIndex[nodeIndex];
                const float indicatorExtent = (std::min)(layoutEntry.worldRect.width, layoutEntry.worldRect.height);
                const float inset = radio.paint.selectedIndicatorInset;
                const UIPremultipliedRgba8Color indicatorTrack =
                    resolvedRadioIndicatorColor(layoutEntry.node, nodeIndex);
                const UIPremultipliedRgba8Color indicator =
                    widgetPaintColor(layoutEntry.node, premultiply(radio.paint.selectedIndicatorColor));
                if (!indicatorTrack.isTransparent() && indicatorExtent > 0.0F)
                {
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect =
                            UILogicalRect{
                                .x = layoutEntry.worldRect.x,
                                .y = layoutEntry.worldRect.y,
                                .width = normalizeFloat(indicatorExtent),
                                .height = normalizeFloat(indicatorExtent),
                            },
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal,
                        .solidFill = indicatorTrack,
                    });
                    ++nextPaintOrdinal;
                }
                if (radio.selected && !indicator.isTransparent() && indicatorExtent > inset * 2.0F)
                {
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect =
                            UILogicalRect{
                                .x = normalizeFloat(layoutEntry.worldRect.x + inset),
                                .y = normalizeFloat(layoutEntry.worldRect.y + inset),
                                .width = normalizeFloat(indicatorExtent - inset * 2.0F),
                                .height = normalizeFloat(indicatorExtent - inset * 2.0F),
                            },
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal,
                        .solidFill = indicator,
                    });
                    ++nextPaintOrdinal;
                }
            }
            appendTextGlyphPaints(output, layoutEntry, nextPaintOrdinal);
        }
    }

    // Publish semantic controls into a stable owner-thread snapshot.
    // Decorative Root/Panel are omitted.
    // Capacity reuses paintSnapshotCapacity as a fixed entry budget for emitted
    // semantics rows (not layout node count — Root/Panel do not publish).
    [[nodiscard]] Core::Status buildCommittedSemantics(std::pmr::vector<UISemanticsEntry>& output,
                                                       std::pmr::vector<char>& textOutput,
                                                       std::span<const UICommittedLayoutEntry> layoutEntries) const
    {
        output.clear();
        usize textOutputSize = 0;
        const auto copyText = [&](std::string_view source, std::string_view& destination) -> Core::Status {
            destination = {};
            if (source.empty())
            {
                return Core::success();
            }
            if (textOutputSize > textOutput.size() || source.size() > textOutput.size() - textOutputSize)
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI committed semantics text snapshot capacity has been exhausted");
            }
            char* const destinationBytes = textOutput.data() + textOutputSize;
            std::memcpy(destinationBytes, source.data(), source.size());
            destination = std::string_view(destinationBytes, source.size());
            textOutputSize += source.size();
            return Core::success();
        };
        for (const UICommittedLayoutEntry& layoutEntry : layoutEntries)
        {
            if (layoutEntry.effectiveVisibility != UIVisibility::Visible)
            {
                continue;
            }
            const u32 nodeIndex = layoutEntry.node.index();
            const NodeRecord* record = recordByIndex(nodeIndex);
            if (record == nullptr || !isSemanticsPublishedKind(record->kind))
            {
                continue;
            }
            if (output.size() >= capacityConfig.paintSnapshotCapacity)
            {
                return fail(UIErrorCode::CapacityExceeded,
                            "UI committed semantics snapshot capacity has been exhausted");
            }
            const bool enabled = isCandidateNodeEnabled(layoutEntry.node);
            UISemanticsEntry entry{
                .node = layoutEntry.node,
                .parent = idForIndex(record->parentIndex),
                .role = semanticsRoleForWidgetKind(record->kind),
                .kind = record->kind,
                .worldRect = layoutEntry.worldRect,
                .enabled = enabled,
                .focused = false,
            };
            if (record->kind == UIWidgetKind::Label || record->kind == UIWidgetKind::Button ||
                record->kind == UIWidgetKind::RadioButton || record->kind == UIWidgetKind::Dropdown ||
                record->kind == UIWidgetKind::DropdownItem || record->kind == UIWidgetKind::ListViewItem ||
                record->kind == UIWidgetKind::TreeViewItem)
            {
                if (Core::Status status = copyText(textViewFor(nodeIndex), entry.name); !status)
                {
                    return status;
                }
            }
            if (record->kind == UIWidgetKind::Checkbox && nodeIndex < checkboxCheckedByNodeIndex.size())
            {
                entry.checked = checkboxCheckedByNodeIndex[nodeIndex] != 0;
                entry.focused = enabled && defaultActionFocusButton == layoutEntry.node;
            }
            if (record->kind == UIWidgetKind::Button)
            {
                entry.focused = enabled && defaultActionFocusButton == layoutEntry.node;
            }
            if (record->kind == UIWidgetKind::Dropdown && nodeIndex < dropdownStatesByNodeIndex.size())
            {
                const UINodeId selected = dropdownStatesByNodeIndex[nodeIndex].selectedItem;
                if (selected.hasValue() && contains(selected))
                {
                    if (Core::Status status = copyText(textViewFor(selected.index()), entry.valueText); !status)
                    {
                        return status;
                    }
                }
                entry.focused = enabled && defaultActionFocusButton == layoutEntry.node;
            }
            if (record->kind == UIWidgetKind::DropdownItem)
            {
                entry.selected = isSelectedDropdownItem(layoutEntry.node);
                entry.focused = enabled && defaultActionFocusButton == layoutEntry.node;
            }
            if (record->kind == UIWidgetKind::TextEdit)
            {
                if (Core::Status status = copyText(textViewFor(nodeIndex), entry.valueText); !status)
                {
                    return status;
                }
                entry.focused = enabled && textInputFocus == layoutEntry.node;
            }
            if (record->kind == UIWidgetKind::Slider && nodeIndex < sliderStatesByNodeIndex.size())
            {
                const SliderState& slider = sliderStatesByNodeIndex[nodeIndex];
                entry.hasRange = true;
                entry.minValue = slider.minValue;
                entry.maxValue = slider.maxValue;
                entry.value = slider.value;
                entry.focused = enabled && armedSlider == layoutEntry.node;
            }
            if (record->kind == UIWidgetKind::ProgressBar && nodeIndex < progressBarStatesByNodeIndex.size())
            {
                const ProgressBarState& progress = progressBarStatesByNodeIndex[nodeIndex];
                entry.hasRange = true;
                entry.minValue = progress.minValue;
                entry.maxValue = progress.maxValue;
                entry.value = progress.value;
            }
            if (record->kind == UIWidgetKind::RadioButton && nodeIndex < radioButtonStatesByNodeIndex.size())
            {
                entry.checked = radioButtonStatesByNodeIndex[nodeIndex].selected;
                entry.focused = enabled && defaultActionFocusButton == layoutEntry.node;
            }
            if (record->kind == UIWidgetKind::ScrollView &&
                nodeIndex < scrollViewLayoutScratchByNodeIndex.size())
            {
                const UIScrollViewMetrics& metrics = scrollViewLayoutScratchByNodeIndex[nodeIndex].metrics;
                entry.hasRange = true;
                entry.minValue = 0.0F;
                entry.maxValue = hasScrollAxis(scrollViewStatesByNodeIndex[nodeIndex].style.axes,
                                               UIScrollAxes::Vertical)
                                     ? metrics.maxOffsetY()
                                     : metrics.maxOffsetX();
                entry.value = hasScrollAxis(scrollViewStatesByNodeIndex[nodeIndex].style.axes,
                                            UIScrollAxes::Vertical)
                                  ? metrics.offset.y
                                  : metrics.offset.x;
                entry.focused = enabled && scrollThumbDragActive && armedScrollView == layoutEntry.node;
            }
            if (record->kind == UIWidgetKind::ListView && nodeIndex < listViewStatesByNodeIndex.size() &&
                nodeIndex < listViewLayoutScratchByNodeIndex.size())
            {
                const UIListViewMetrics& metrics = listViewLayoutScratchByNodeIndex[nodeIndex].metrics;
                entry.hasRange = true;
                entry.minValue = 0.0F;
                entry.maxValue = metrics.maxScrollOffset;
                entry.value = metrics.scrollOffset;
                entry.focused = enabled && defaultActionFocusButton == layoutEntry.node;
            }
            if (record->kind == UIWidgetKind::ListViewItem && nodeIndex < listViewItemStatesByNodeIndex.size())
            {
                const ListViewItemState& item = listViewItemStatesByNodeIndex[nodeIndex];
                entry.virtualItemKey = item.key;
                entry.virtualItemIndex = item.logicalIndex;
                entry.selected = item.bound && isSelectedListViewItem(layoutEntry.node);
                const UINodeId listView = listViewForItem(layoutEntry.node);
                entry.focused = enabled && entry.selected && defaultActionFocusButton == listView;
            }
            if (record->kind == UIWidgetKind::TreeView && nodeIndex < treeViewStatesByNodeIndex.size() &&
                nodeIndex < treeViewLayoutScratchByNodeIndex.size())
            {
                const UITreeViewMetrics& metrics = treeViewLayoutScratchByNodeIndex[nodeIndex].metrics;
                entry.hasRange = true;
                entry.minValue = 0.0F;
                entry.maxValue = metrics.maxScrollOffset;
                entry.value = metrics.scrollOffset;
                entry.focused = enabled && defaultActionFocusButton == layoutEntry.node;
            }
            if (record->kind == UIWidgetKind::TreeViewItem && nodeIndex < treeViewItemStatesByNodeIndex.size())
            {
                const TreeViewItemState& item = treeViewItemStatesByNodeIndex[nodeIndex];
                entry.virtualItemKey = item.key;
                entry.virtualItemIndex = item.logicalIndex;
                entry.level = item.level;
                entry.expandable = item.bound && item.expandable;
                entry.expanded = item.bound && item.expanded;
                entry.selected = item.bound && isSelectedTreeViewItem(layoutEntry.node);
                const UINodeId treeView = treeViewForItem(layoutEntry.node);
                entry.focused = enabled && entry.selected && defaultActionFocusButton == treeView;
            }
            output.push_back(entry);
        }
        return Core::success();
    }

    [[nodiscard]] static bool isFiniteLayoutRect(UILogicalRect rect) noexcept
    {
        return std::isfinite(rect.x) && std::isfinite(rect.y) && isFiniteNonNegative(rect.width) &&
               isFiniteNonNegative(rect.height) && std::isfinite(rect.right()) && std::isfinite(rect.bottom());
    }

    [[nodiscard]] Core::Status validateLayoutCandidate(const std::pmr::vector<u32>& order) const
    {
        for (const u32 index : order)
        {
            const LayoutScratchState& scratch = layoutScratchByIndex[index];
            if (!isFiniteNonNegative(scratch.measuredSize.width) || !isFiniteNonNegative(scratch.measuredSize.height) ||
                !isFiniteLayoutRect(scratch.localRect) || !isFiniteLayoutRect(scratch.worldRect) ||
                !isFiniteLayoutRect(scratch.effectiveClip) || !isFiniteLayoutRect(scratch.descendantClip))
            {
                return fail(UIErrorCode::InvalidLayout, "UI layout arithmetic produced non-finite geometry");
            }
        }
        return Core::success();
    }

    [[nodiscard]] bool isPhaseDirty(UIDirty flags) const noexcept
    {
        return hasDirty(phaseDirty, flags);
    }

    [[nodiscard]] Core::Status publishStructureIfDirty()
    {
        if (!isPhaseDirty(PhaseStructure))
        {
            return Core::success();
        }

        const usize writeBufferIndex = 1 - publishedBufferIndex;
        buildCommittedStructure(committedBuffers[writeBufferIndex]);
        publishedBufferIndex = writeBufferIndex;
        ++committedRevision;
        phaseDirty = clearDirty(phaseDirty, PhaseStructure);
        return Core::success();
    }

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == ownerThreadId;
    }

    [[nodiscard]] Core::Status ensureOwnerThread() const
    {
        if (!isOwnerThread())
        {
            return fail(UIErrorCode::WrongOwnerThread, "UI context was accessed from a non-owner thread");
        }
        return Core::success();
    }

    void publishRoutedPointerListenerTokenState(u32 slot, u32 generation, bool active) noexcept
    {
        if (lifetime)
        {
            lifetime->publishRoutedPointerListenerState(slot, generation, active);
        }
    }

    static void publishRoutedPointerListenerTokenStateFromRegistry(void* context, u32 slot,
                                                                   u32 generation, bool active) noexcept
    {
        if (context != nullptr)
        {
            static_cast<Impl*>(context)->publishRoutedPointerListenerTokenState(slot, generation, active);
        }
    }

    [[nodiscard]] Detail::UIRoutedPointerListenerStatePublisher
    routedPointerListenerStatePublisher() noexcept
    {
        return Detail::UIRoutedPointerListenerStatePublisher{
            .context = this,
            .publish = &Impl::publishRoutedPointerListenerTokenStateFromRegistry,
        };
    }

    void reclaimInactiveRoutedPointerListeners() noexcept
    {
        routedPointerListenerRegistry.reclaim(routeDispatchDepth != 0);
    }

    void deactivateRoutedPointerListener(u32 listenerIndex, u32 generation,
                                         bool publishTokenState) noexcept
    {
        const auto statePublisher = publishTokenState ? routedPointerListenerStatePublisher()
                                                      : Detail::UIRoutedPointerListenerStatePublisher{};
        static_cast<void>(routedPointerListenerRegistry.deactivate(
            listenerIndex, generation, statePublisher, routeDispatchDepth != 0));
    }

    void deactivateAllRoutedPointerListenersForNode(u32 nodeIndex) noexcept
    {
        routedPointerListenerRegistry.clearNode(nodeIndex, routedPointerListenerStatePublisher());
    }

    void drainDeferredRoutedPointerListenerReleases() noexcept
    {
        if (!isOwnerThread() || !lifetime)
        {
            return;
        }
        lifetime->takeDeferredRoutedPointerListenerReleases(
            deferredRoutedPointerListenerReleaseBuffer);
        for (const Detail::DeferredRoutedPointerListenerRelease release : deferredRoutedPointerListenerReleaseBuffer)
        {
            deactivateRoutedPointerListener(release.slot, release.generation, false);
        }
        deferredRoutedPointerListenerReleaseBuffer.clear();
        reclaimInactiveRoutedPointerListeners();
    }

    void drainDeferredRootDestroys() noexcept
    {
        if (!isOwnerThread() || !lifetime)
        {
            return;
        }

        lifetime->takeDeferredRootDestroys(deferredRootDestroyBuffer);
        for (const UINodeId root : deferredRootDestroyBuffer)
        {
            destroyRootImmediately(root);
        }
        deferredRootDestroyBuffer.clear();
        drainDeferredRoutedPointerListenerReleases();
    }

    [[nodiscard]] UINodeId idForIndex(u32 index) const noexcept
    {
        if (index == InvalidNodeIndex || index >= idsByIndex.size())
        {
            return {};
        }
        return idsByIndex[index];
    }

    [[nodiscard]] NodeRecord* recordByIndex(u32 index) noexcept
    {
        return nodes.tryGet(idForIndex(index).storageId());
    }

    [[nodiscard]] const NodeRecord* recordByIndex(u32 index) const noexcept
    {
        return nodes.tryGet(idForIndex(index).storageId());
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveNode(UINodeId node)
    {
        if (!node.hasValue())
        {
            return fail(UIErrorCode::InvalidNode, "UI node id is empty");
        }
        if (node.ownerWindow() != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI node belongs to another owner window");
        }
        if (node.storageId().owner() != nodes.owner())
        {
            return fail(UIErrorCode::WrongContext, "UI node belongs to another context");
        }
        NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr)
        {
            return fail(UIErrorCode::InvalidNode, "UI node is stale or out of range");
        }
        return record;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveParent(UINodeId parent)
    {
        if (!parent.hasValue())
        {
            return fail(UIErrorCode::InvalidParent, "UI parent id is empty");
        }
        if (parent.ownerWindow() != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI parent belongs to another owner window");
        }
        if (parent.storageId().owner() != nodes.owner())
        {
            return fail(UIErrorCode::WrongContext, "UI parent belongs to another context");
        }
        NodeRecord* record = nodes.tryGet(parent.storageId());
        if (record == nullptr)
        {
            return fail(UIErrorCode::InvalidParent, "UI parent is stale or out of range");
        }
        return record;
    }

    [[nodiscard]] bool contains(UINodeId node) const noexcept
    {
        return node.hasValue() && node.ownerWindow() == ownerWindow && node.storageId().owner() == nodes.owner() &&
               nodes.contains(node.storageId());
    }

    [[nodiscard]] bool isNodeEnabled(UINodeId node) const noexcept
    {
        if (!contains(node) || node.index() >= enabledByNodeIndex.size() || enabledByNodeIndex[node.index()] == 0)
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record != nullptr && record->kind == UIWidgetKind::ListViewItem &&
            node.index() < listViewItemStatesByNodeIndex.size())
        {
            const ListViewItemState& item = listViewItemStatesByNodeIndex[node.index()];
            return item.committedBound && item.committedEnabled;
        }
        if (record != nullptr && record->kind == UIWidgetKind::TreeViewItem &&
            node.index() < treeViewItemStatesByNodeIndex.size())
        {
            const TreeViewItemState& item = treeViewItemStatesByNodeIndex[node.index()];
            return item.committedBound && item.committedEnabled;
        }
        return true;
    }

    [[nodiscard]] bool isCandidateNodeEnabled(UINodeId node) const noexcept
    {
        if (!contains(node) || node.index() >= enabledByNodeIndex.size() || enabledByNodeIndex[node.index()] == 0)
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record != nullptr && record->kind == UIWidgetKind::ListViewItem &&
            node.index() < listViewItemStatesByNodeIndex.size())
        {
            const ListViewItemState& item = listViewItemStatesByNodeIndex[node.index()];
            return item.bound && item.enabled;
        }
        if (record != nullptr && record->kind == UIWidgetKind::TreeViewItem &&
            node.index() < treeViewItemStatesByNodeIndex.size())
        {
            const TreeViewItemState& item = treeViewItemStatesByNodeIndex[node.index()];
            return item.bound && item.enabled;
        }
        return true;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveButton(UINodeId button)
    {
        auto nodeResult = resolveNode(button);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isDefaultActivatableKind((*nodeResult)->kind) || (*nodeResult)->kind == UIWidgetKind::ListViewItem ||
            (*nodeResult)->kind == UIWidgetKind::TreeViewItem)
        {
            return fail(UIErrorCode::InvalidButtonAction,
                        "UI Button action requires a Button, Checkbox, RadioButton, Dropdown, or DropdownItem node");
        }
        return *nodeResult;
    }

    [[nodiscard]] static bool isButtonChromeKind(UIWidgetKind kind) noexcept
    {
        return kind == UIWidgetKind::Button || kind == UIWidgetKind::Dropdown || kind == UIWidgetKind::DropdownItem ||
               kind == UIWidgetKind::ListViewItem || kind == UIWidgetKind::TreeViewItem;
    }

    [[nodiscard]] static bool isDefaultActivatableKind(UIWidgetKind kind) noexcept
    {
        return kind == UIWidgetKind::Button || kind == UIWidgetKind::Dropdown || kind == UIWidgetKind::DropdownItem ||
               kind == UIWidgetKind::ListViewItem || kind == UIWidgetKind::Checkbox ||
               kind == UIWidgetKind::TreeViewItem || kind == UIWidgetKind::RadioButton;
    }

    [[nodiscard]] static bool isKeyboardFocusableKind(UIWidgetKind kind) noexcept
    {
        return isDefaultActivatableKind(kind) || kind == UIWidgetKind::TextEdit || kind == UIWidgetKind::ListView ||
               kind == UIWidgetKind::TreeView;
    }

    [[nodiscard]] bool isButtonPressed(UINodeId node) const noexcept
    {
        return (armedPrimaryButton == node && armedPrimaryButtonPressed) ||
               defaultActionPressState.isPressed(node);
    }

    void clearArmedPrimaryButton() noexcept
    {
        armedPrimaryButton = {};
        armedPrimaryButtonPressed = false;
        armedTreeDisclosure = false;
    }

    void clearHoveredPrimaryButton() noexcept
    {
        hoveredPrimaryButton = {};
    }

    [[nodiscard]] UINodeId resolvedHoveredPrimaryButton(UINodeId candidate) const noexcept
    {
        if (candidate.hasValue() && isNodeEnabled(candidate))
        {
            const NodeRecord* record = nodes.tryGet(candidate.storageId());
            if (record != nullptr && isButtonChromeKind(record->kind))
            {
                return candidate;
            }
        }
        return {};
    }

    [[nodiscard]] Core::Status preflightHoveredPrimaryButton(UINodeId candidate) const
    {
        const UINodeId nextHover = resolvedHoveredPrimaryButton(candidate);
        if (nextHover == hoveredPrimaryButton)
        {
            return Core::success();
        }
        const UINodeId previousHover =
            hoveredPrimaryButton.hasValue() && contains(hoveredPrimaryButton) ? hoveredPrimaryButton : UINodeId{};
        return preflightPaintDirtyBatch({previousHover, nextHover});
    }

    [[nodiscard]] Core::Status updateHoveredPrimaryButton(UINodeId candidate)
    {
        const UINodeId nextHover = resolvedHoveredPrimaryButton(candidate);
        if (nextHover == hoveredPrimaryButton)
        {
            return Core::success();
        }
        const UINodeId previousHover =
            hoveredPrimaryButton.hasValue() && contains(hoveredPrimaryButton) ? hoveredPrimaryButton : UINodeId{};
        if (Core::Status dirty = markPaintDirtyBatch({previousHover, nextHover}); !dirty)
        {
            return dirty;
        }
        hoveredPrimaryButton = nextHover;
        return Core::success();
    }

    void clearArmedSlider() noexcept
    {
        armedSlider = {};
    }

    void clearArmedScrollView() noexcept
    {
        armedScrollView = {};
        armedScrollAxis = UIScrollAxes::None;
        scrollDragGrabOffset = 0.0F;
        scrollThumbDragActive = false;
    }

    void clearArmedTextEdit() noexcept
    {
        armedTextEdit = {};
    }

    void clearDefaultActionFocus() noexcept
    {
        defaultActionFocusButton = {};
        defaultActionPressState.clearAll();
    }

    void resetImeCompositionState() noexcept
    {
        imeComposition.reset();
    }

    [[nodiscard]] Core::Status clearImeComposition()
    {
        const bool wasActive = imeComposition.active();
        const UINodeId focus = textInputFocus;
        if (wasActive && focus.hasValue() && contains(focus))
        {
            if (Core::Status paintStatus = markPaintDirty(focus); !paintStatus)
            {
                return paintStatus;
            }
        }
        resetImeCompositionState();
        return Core::success();
    }

    void clearImeFocus() noexcept
    {
        const UINodeId previousFocus = textInputFocus;
        if (Core::Status status = clearImeComposition(); !status)
        {
            // Focus cancellation must win even when another dirty node has
            // exhausted the queue. A pending paint commit will observe this
            // reset state.
            resetImeCompositionState();
        }
        textInputFocus = {};
        if (defaultActionFocusButton == previousFocus)
        {
            clearDefaultActionFocus();
        }
        if (previousFocus.hasValue() && contains(previousFocus))
        {
            static_cast<void>(markPaintDirty(previousFocus));
        }
    }

    [[nodiscard]] bool isLiveTextEdit(UINodeId node) const noexcept
    {
        if (!node.hasValue() || !contains(node))
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        return record != nullptr && record->kind == UIWidgetKind::TextEdit;
    }

    [[nodiscard]] static u32 findHitEntryIndex(UINodeId node, std::span<const UICommittedHitEntry> entries) noexcept
    {
        for (u32 index = 0; index < entries.size(); ++index)
        {
            if (entries[index].node == node)
            {
                return index;
            }
        }
        return InvalidUIHitEntryIndex;
    }

    [[nodiscard]] static bool hitEntryIsWithinScope(u32 entryIndex, u32 scopeEntryIndex,
                                                    std::span<const UICommittedHitEntry> entries) noexcept
    {
        if (scopeEntryIndex == InvalidUIHitEntryIndex)
        {
            return true;
        }
        usize visited = 0;
        while (entryIndex != InvalidUIHitEntryIndex && entryIndex < entries.size())
        {
            if (entryIndex == scopeEntryIndex)
            {
                return true;
            }
            entryIndex = entries[entryIndex].parentEntryIndex;
            if (++visited > entries.size())
            {
                return false;
            }
        }
        return false;
    }

    [[nodiscard]] static bool hitEntryAllowedByModal(const UICommittedHitEntry& entry,
                                                     u32 activeModalEntryIndex) noexcept
    {
        return activeModalEntryIndex == InvalidUIHitEntryIndex || entry.modalScopeEntryIndex == activeModalEntryIndex;
    }

    [[nodiscard]] bool isPointerInteractionCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
                                                     u32 activeModalEntryIndex) const noexcept
    {
        if (!node.hasValue() || !isNodeEnabled(node))
        {
            return false;
        }
        const u32 entryIndex = findHitEntryIndex(node, entries);
        return entryIndex < entries.size() && entries[entryIndex].policy == UIPointerHitPolicy::Targetable &&
               hitEntryAllowedByModal(entries[entryIndex], activeModalEntryIndex);
    }

    [[nodiscard]] bool isPointerCaptureCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
                                                 u32 activeModalEntryIndex) const noexcept
    {
        if (!node.hasValue() || !contains(node) || !isNodeEnabled(node))
        {
            return false;
        }
        const u32 entryIndex = findHitEntryIndex(node, entries);
        return entryIndex < entries.size() && hitEntryAllowedByModal(entries[entryIndex], activeModalEntryIndex);
    }

    [[nodiscard]] bool isKeyboardFocusCandidate(UINodeId node, std::span<const UICommittedHitEntry> entries,
                                                u32 activeModalEntryIndex) const noexcept
    {
        if (!isPointerInteractionCandidate(node, entries, activeModalEntryIndex))
        {
            return false;
        }
        const u32 entryIndex = findHitEntryIndex(node, entries);
        return entryIndex < entries.size() && isKeyboardFocusableKind(entries[entryIndex].kind);
    }

    [[nodiscard]] bool isCommittedKeyboardFocusCandidate(UINodeId node) const noexcept
    {
        if (!node.hasValue() || !isNodeEnabled(node))
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !isKeyboardFocusableKind(record->kind))
        {
            return false;
        }
        const auto& hitEntries = committedHitBuffers[publishedHitBufferIndex];
        return isKeyboardFocusCandidate(node, hitEntries, committedActiveModalEntryIndex);
    }

    [[nodiscard]] bool isCommittedTextEditFocusCandidate(UINodeId node) const noexcept
    {
        return isLiveTextEdit(node) && isCommittedKeyboardFocusCandidate(node) && defaultActionFocusButton == node;
    }

    void deactivateButtonActionForNode(u32 nodeIndex) noexcept
    {
        if (nodeIndex >= idsByIndex.size())
        {
            return;
        }
        const UINodeId node = idForIndex(nodeIndex);
        // Node destruction makes every matching control identity stale; no
        // synthetic Up is emitted for the destroyed target.
        defaultActionPressState.clearNode(node);
        if (hoveredPrimaryButton == node)
        {
            hoveredPrimaryButton = {};
        }
        if (armedPrimaryButton == node)
        {
            clearArmedPrimaryButton();
        }
        if (armedSlider == node)
        {
            clearArmedSlider();
        }
        if (armedScrollView == node)
        {
            clearArmedScrollView();
        }
        if (armedTextEdit == node)
        {
            clearArmedTextEdit();
        }
        if (capturedPointerNode == node)
        {
            capturedPointerNode = {};
        }
        if (defaultActionFocusButton == node)
        {
            clearDefaultActionFocus();
        }
        if (textInputFocus == node)
        {
            clearImeFocus();
        }
        // Callback destruction is delayed until the node generation has been
        // erased, so a callable destructor cannot register against a dying node.
        buttonActionRegistry.clearNode(nodeIndex, true);
    }

    [[nodiscard]] Detail::UIButtonActionInvocation
    captureButtonAction(UINodeId button, u64 registrationSerialBoundary) const noexcept
    {
        if (!contains(button))
        {
            return {};
        }
        return buttonActionRegistry.capture(button, registrationSerialBoundary);
    }

    void invokeButtonAction(Detail::UIButtonActionInvocation candidate, const UIButtonActionEvent& event,
                            u64 routeSerial) noexcept
    {
        if (!candidate.hasValue() || !contains(candidate.button))
        {
            return;
        }
        const NodeRecord* buttonRecord = nodes.tryGet(candidate.button.storageId());
        if (buttonRecord == nullptr || !isDefaultActivatableKind(buttonRecord->kind))
        {
            return;
        }
        buttonActionRegistry.invoke(candidate, event, routeSerial, routeDispatchDepth != 0);
    }

    [[nodiscard]] Detail::UISliderChangeCallbackInvocation
    captureSliderChangeCallback(UINodeId slider) const noexcept
    {
        if (!contains(slider))
        {
            return {};
        }
        return sliderChangeCallbackRegistry.capture(slider);
    }

    void invokeSliderChangeCallback(Detail::UISliderChangeCallbackInvocation candidate,
                                    const UISliderChangeEvent& event) noexcept
    {
        if (!candidate.hasValue() || !contains(candidate.slider))
        {
            return;
        }
        const NodeRecord* sliderRecord = nodes.tryGet(candidate.slider.storageId());
        if (sliderRecord == nullptr || sliderRecord->kind != UIWidgetKind::Slider)
        {
            return;
        }
        sliderChangeCallbackRegistry.invoke(candidate, event, routeDispatchDepth != 0);
    }

    void clearTextState(u32 index) noexcept
    {
        if (index >= textStatesByIndex.size())
        {
            return;
        }
        WidgetTextState& state = textStatesByIndex[index];
        textStorage.release(state.allocation);
        state = {};
    }

    [[nodiscard]] std::string_view textViewFor(u32 index) const noexcept
    {
        if (index >= textStatesByIndex.size())
        {
            return {};
        }
        const WidgetTextState& state = textStatesByIndex[index];
        if (!state.hasContent || state.length == 0 || state.allocation.capacity == 0)
        {
            return {};
        }
        return textStorage.view(state.allocation, state.length);
    }

    [[nodiscard]] UINodeId dropdownForPopup(UINodeId popup) const noexcept
    {
        if (!contains(popup))
        {
            return {};
        }
        const NodeRecord* popupRecord = nodes.tryGet(popup.storageId());
        if (popupRecord == nullptr || popupRecord->kind != UIWidgetKind::Popup ||
            popupRecord->parentIndex == InvalidNodeIndex)
        {
            return {};
        }
        const NodeRecord* dropdownRecord = recordByIndex(popupRecord->parentIndex);
        return dropdownRecord != nullptr && dropdownRecord->kind == UIWidgetKind::Dropdown
                   ? idForIndex(popupRecord->parentIndex)
                   : UINodeId{};
    }

    [[nodiscard]] UINodeId popupForDropdown(UINodeId dropdown) const noexcept
    {
        if (!contains(dropdown) || dropdown.index() >= dropdownStatesByNodeIndex.size())
        {
            return {};
        }
        const NodeRecord* record = nodes.tryGet(dropdown.storageId());
        const UINodeId popup = dropdownStatesByNodeIndex[dropdown.index()].popup;
        return record != nullptr && record->kind == UIWidgetKind::Dropdown && contains(popup) ? popup : UINodeId{};
    }

    [[nodiscard]] UINodeId dropdownForItem(UINodeId item) const noexcept
    {
        if (!contains(item))
        {
            return {};
        }
        const NodeRecord* itemRecord = nodes.tryGet(item.storageId());
        if (itemRecord == nullptr || itemRecord->kind != UIWidgetKind::DropdownItem ||
            itemRecord->parentIndex == InvalidNodeIndex)
        {
            return {};
        }
        return dropdownForPopup(idForIndex(itemRecord->parentIndex));
    }

    [[nodiscard]] bool isSelectedDropdownItem(UINodeId item) const noexcept
    {
        const UINodeId dropdown = dropdownForItem(item);
        return dropdown.hasValue() && dropdown.index() < dropdownStatesByNodeIndex.size() &&
               dropdownStatesByNodeIndex[dropdown.index()].selectedItem == item;
    }

    [[nodiscard]] std::string_view presentationTextViewFor(u32 index) const noexcept
    {
        const NodeRecord* record = recordByIndex(index);
        if (record != nullptr && record->kind == UIWidgetKind::Dropdown && index < dropdownStatesByNodeIndex.size())
        {
            const UINodeId selected = dropdownStatesByNodeIndex[index].selectedItem;
            if (contains(selected) && selected.index() < textStatesByIndex.size() &&
                textStatesByIndex[selected.index()].hasContent)
            {
                return textViewFor(selected.index());
            }
        }
        return textViewFor(index);
    }

    [[nodiscard]] const UITextMetrics* presentationTextMetricsFor(u32 index) const noexcept
    {
        const NodeRecord* record = recordByIndex(index);
        if (record != nullptr && record->kind == UIWidgetKind::Dropdown && index < dropdownStatesByNodeIndex.size())
        {
            const UINodeId selected = dropdownStatesByNodeIndex[index].selectedItem;
            if (contains(selected) && selected.index() < textStatesByIndex.size() &&
                textStatesByIndex[selected.index()].hasContent)
            {
                return &textStatesByIndex[selected.index()].metrics;
            }
        }
        return index < textStatesByIndex.size() && textStatesByIndex[index].hasContent
                   ? &textStatesByIndex[index].metrics
                   : nullptr;
    }

    [[nodiscard]] Core::Result<UITextMetrics> measureWidgetText(std::string_view utf8, const UITextStyle& style)
    {
        if (textRasterizer && textFace.hasValue())
        {
            return textRasterizer->measure(textFace, utf8, style);
        }
        // Fallback keeps measure available when a custom FreeType rasterizer is
        // injected before any face is opened.
        return measurePlaceholderText(utf8, style);
    }

    [[nodiscard]] static bool supportsWidgetText(UIWidgetKind kind) noexcept
    {
        return kind == UIWidgetKind::Label || kind == UIWidgetKind::Button || kind == UIWidgetKind::TextEdit ||
               kind == UIWidgetKind::RadioButton || kind == UIWidgetKind::Dropdown ||
               kind == UIWidgetKind::DropdownItem || kind == UIWidgetKind::ListViewItem ||
               kind == UIWidgetKind::TreeViewItem;
    }

    void resetNodeSideData(u32 index) noexcept
    {
        if (index >= layoutStylesByIndex.size())
        {
            return;
        }
        layoutStylesByIndex[index] = {};
        pointerHitPoliciesByIndex[index] = UIPointerHitPolicy::Ignore;
        if (index < enabledByNodeIndex.size())
        {
            enabledByNodeIndex[index] = 1;
        }
        if (index < focusScopeModesByNodeIndex.size())
        {
            focusScopeModesByNodeIndex[index] = UIFocusScopeMode::None;
        }
        if (index < focusRestoreByNodeIndex.size())
        {
            focusRestoreByNodeIndex[index] = {};
        }
        boxPaintsByIndex[index] = {};
        buttonPaintsByNodeIndex[index] = {};
        if (index < themeBindingsByNodeIndex.size())
        {
            themeBindingsByNodeIndex[index] = 0;
            themeDirtyScratchByNodeIndex[index] = 0;
            themeTextMetricsScratchByNodeIndex[index] = {};
        }
        localSolidFillCacheByIndex[index] = {};
        localTextColorCacheByIndex[index] = {};
        clearTextState(index);
        dirtyByIndex[index] = UIDirty::None;
        dirtyQueuedByIndex[index] = 0;
        if (index < dirtyReservedByIndex.size() && dirtyReservedByIndex[index] != 0)
        {
            dirtyReservedByIndex[index] = 0;
            if (dirtyQueueReservationCount != 0)
            {
                --dirtyQueueReservationCount;
            }
        }
        if (index < routeDirtyReservationCandidateByIndex.size())
        {
            routeDirtyReservationCandidateByIndex[index] = 0;
        }
        layoutScratchByIndex[index] = {};
        layoutWorkByIndex[index] = 0;
        routedPointerListenerRegistry.resetNodeSlot(index);
        if (index < checkboxCheckedByNodeIndex.size())
        {
            checkboxCheckedByNodeIndex[index] = 0;
        }
        if (index < checkboxPaintsByNodeIndex.size())
        {
            checkboxPaintsByNodeIndex[index] = {};
        }
        if (index < sliderStatesByNodeIndex.size())
        {
            sliderStatesByNodeIndex[index] = {};
        }
        if (index < textEditStatesByNodeIndex.size())
        {
            textEditStatesByNodeIndex[index] = {};
        }
        if (index < progressBarStatesByNodeIndex.size())
        {
            progressBarStatesByNodeIndex[index] = {};
        }
        if (index < radioButtonStatesByNodeIndex.size())
        {
            radioButtonStatesByNodeIndex[index] = {};
        }
        if (index < scrollViewStatesByNodeIndex.size())
        {
            scrollViewStatesByNodeIndex[index] = {};
        }
        if (index < scrollViewLayoutScratchByNodeIndex.size())
        {
            scrollViewLayoutScratchByNodeIndex[index] = {};
        }
        if (index < dropdownStatesByNodeIndex.size())
        {
            dropdownStatesByNodeIndex[index] = {};
        }
        if (index < popupStatesByNodeIndex.size())
        {
            popupStatesByNodeIndex[index] = {};
        }
        if (index < popupLayoutScratchByNodeIndex.size())
        {
            popupLayoutScratchByNodeIndex[index] = {};
        }
        if (index < listViewStatesByNodeIndex.size())
        {
            listViewStatesByNodeIndex[index] = {};
        }
        if (index < listViewLayoutScratchByNodeIndex.size())
        {
            listViewLayoutScratchByNodeIndex[index] = {};
        }
        if (index < listViewItemStatesByNodeIndex.size())
        {
            listViewItemStatesByNodeIndex[index] = {};
        }
        if (index < treeViewStatesByNodeIndex.size())
        {
            treeViewStatesByNodeIndex[index] = {};
        }
        if (index < treeViewLayoutScratchByNodeIndex.size())
        {
            treeViewLayoutScratchByNodeIndex[index] = {};
        }
        if (index < treeViewItemStatesByNodeIndex.size())
        {
            treeViewItemStatesByNodeIndex[index] = {};
        }
    }

    void markStructureChanged() noexcept
    {
        phaseDirty |= PhaseStructure | PhaseLayout | PhaseHit;
        layoutReuseCacheValid = false;
    }

    void compactDirtyQueue() noexcept
    {
        usize writeIndex = 0;
        for (const UINodeId queued : dirtyQueue)
        {
            if (!queued.hasValue() || queued.index() >= dirtyQueuedByIndex.size())
            {
                continue;
            }
            const u32 index = queued.index();
            if (idForIndex(index) != queued)
            {
                // A stale generation may share its slot with a newly queued node.
                // Never clear the current generation's side-state from the stale entry.
                continue;
            }
            if (!contains(queued) || dirtyQueuedByIndex[index] == 0 || !anyDirty(dirtyByIndex[index]))
            {
                dirtyQueuedByIndex[index] = 0;
                continue;
            }
            dirtyQueue[writeIndex++] = queued;
        }
        dirtyQueue.resize(writeIndex);
    }

    [[nodiscard]] usize validDirtyQueueCount() const noexcept
    {
        usize count = 0;
        for (const UINodeId queued : dirtyQueue)
        {
            if (!queued.hasValue() || queued.index() >= dirtyQueuedByIndex.size())
            {
                continue;
            }
            const u32 index = queued.index();
            if (idForIndex(index) == queued && contains(queued) && dirtyQueuedByIndex[index] != 0 &&
                anyDirty(dirtyByIndex[index]))
            {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] usize occupiedDirtyQueueSlotCount() const noexcept
    {
        return dirtyQueue.size() + dirtyQueueReservationCount;
    }

    void consumeDirtyQueueReservation(u32 index) noexcept
    {
        if (index >= dirtyReservedByIndex.size() || dirtyReservedByIndex[index] == 0)
        {
            return;
        }
        dirtyReservedByIndex[index] = 0;
        if (dirtyQueueReservationCount != 0)
        {
            --dirtyQueueReservationCount;
        }
    }

    void addRouteDirtyReservationCandidate(UINodeId node)
    {
        if (!node.hasValue() || !contains(node))
        {
            return;
        }
        const u32 index = node.index();
        if (index >= routeDirtyReservationCandidateByIndex.size() || routeDirtyReservationCandidateByIndex[index] != 0)
        {
            return;
        }
        routeDirtyReservationCandidateByIndex[index] = 1;
        routeDirtyReservationScratch.push_back(node);
    }

    void addRouteLayoutDirtyReservationCandidates(UINodeId node)
    {
        if (!node.hasValue() || !contains(node))
        {
            return;
        }
        u32 index = node.index();
        usize visited = 0;
        while (index != InvalidNodeIndex && visited++ < nodes.capacity())
        {
            addRouteDirtyReservationCandidate(idForIndex(index));
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr)
            {
                return;
            }
            index = record->parentIndex;
        }
    }

    [[nodiscard]] Core::Status reserveRouteDirtyQueueSlots()
    {
        compactDirtyQueue();
        usize requiredQueueEntries = 0;
        for (const UINodeId node : routeDirtyReservationScratch)
        {
            if (!contains(node) || node.index() >= dirtyByIndex.size())
            {
                return fail(UIErrorCode::InvalidNode, "UI pointer route dirty reservation node is invalid");
            }
            const u32 index = node.index();
            if (dirtyQueuedByIndex[index] == 0 && dirtyReservedByIndex[index] == 0)
            {
                ++requiredQueueEntries;
            }
        }

        const usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        if (occupiedQueueEntries > capacityConfig.dirtyQueueCapacity ||
            requiredQueueEntries > capacityConfig.dirtyQueueCapacity - occupiedQueueEntries)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
        }

        for (const UINodeId node : routeDirtyReservationScratch)
        {
            const u32 index = node.index();
            if (dirtyQueuedByIndex[index] == 0 && dirtyReservedByIndex[index] == 0)
            {
                dirtyReservedByIndex[index] = 1;
                ++dirtyQueueReservationCount;
            }
        }
        return Core::success();
    }

    void releaseRouteDirtyQueueReservations() noexcept
    {
        for (const UINodeId node : routeDirtyReservationScratch)
        {
            if (node.hasValue() && node.index() < dirtyReservedByIndex.size())
            {
                consumeDirtyQueueReservation(node.index());
            }
            if (node.hasValue() && node.index() < routeDirtyReservationCandidateByIndex.size())
            {
                routeDirtyReservationCandidateByIndex[node.index()] = 0;
            }
        }
        routeDirtyReservationScratch.clear();
    }

    [[nodiscard]] Core::Status markLayoutDirtyBatch(std::initializer_list<UINodeId> requestedNodes)
    {
        layoutOrderScratch.clear();
        for (const UINodeId requested : requestedNodes)
        {
            if (!requested.hasValue())
            {
                continue;
            }
            if (!contains(requested) || requested.index() >= dirtyByIndex.size())
            {
                return fail(UIErrorCode::InvalidNode, "UI dirty node is invalid");
            }

            u32 index = requested.index();
            usize visited = 0;
            while (index != InvalidNodeIndex && visited++ < nodes.capacity())
            {
                if (std::find(layoutOrderScratch.begin(), layoutOrderScratch.end(), index) == layoutOrderScratch.end())
                {
                    layoutOrderScratch.push_back(index);
                }
                const NodeRecord* record = recordByIndex(index);
                if (record == nullptr)
                {
                    return fail(UIErrorCode::InvalidNode, "UI dirty ancestry is invalid");
                }
                index = record->parentIndex;
            }
        }
        if (layoutOrderScratch.empty())
        {
            return Core::success();
        }

        usize requiredQueueEntries = 0;
        for (const u32 dirtyIndex : layoutOrderScratch)
        {
            if (dirtyQueuedByIndex[dirtyIndex] == 0 && dirtyReservedByIndex[dirtyIndex] == 0)
            {
                ++requiredQueueEntries;
            }
        }
        usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        if (occupiedQueueEntries > capacityConfig.dirtyQueueCapacity ||
            requiredQueueEntries > capacityConfig.dirtyQueueCapacity - occupiedQueueEntries)
        {
            compactDirtyQueue();
            requiredQueueEntries = 0;
            for (const u32 dirtyIndex : layoutOrderScratch)
            {
                if (dirtyQueuedByIndex[dirtyIndex] == 0 && dirtyReservedByIndex[dirtyIndex] == 0)
                {
                    ++requiredQueueEntries;
                }
            }
            occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        }
        if (occupiedQueueEntries > capacityConfig.dirtyQueueCapacity ||
            requiredQueueEntries > capacityConfig.dirtyQueueCapacity - occupiedQueueEntries)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
        }
        constexpr UIDirty ChangedNodeDirty = UIDirty::Style | UIDirty::Measure | UIDirty::Arrange | UIDirty::Composite |
                                             UIDirty::HitTest | UIDirty::Semantics;
        constexpr UIDirty AncestorDirty = UIDirty::Measure | UIDirty::Arrange | UIDirty::Composite | UIDirty::HitTest;
        for (const u32 dirtyIndex : layoutOrderScratch)
        {
            if (dirtyQueuedByIndex[dirtyIndex] == 0)
            {
                consumeDirtyQueueReservation(dirtyIndex);
                dirtyQueue.push_back(idForIndex(dirtyIndex));
                dirtyQueuedByIndex[dirtyIndex] = 1;
            }
            const bool directlyRequested = std::any_of(
                requestedNodes.begin(), requestedNodes.end(),
                [dirtyIndex](UINodeId requested) noexcept {
                    return requested.hasValue() && requested.index() == dirtyIndex;
                });
            dirtyByIndex[dirtyIndex] |= directlyRequested ? ChangedNodeDirty : AncestorDirty;
        }
        dirtyQueueHighWater = (std::max)(dirtyQueueHighWater, dirtyQueue.size());
        phaseDirty |= PhaseLayout | PhaseHit;
        return Core::success();
    }

    [[nodiscard]] Core::Status markHitTestDirty(UINodeId node)
    {
        if (!contains(node) || node.index() >= dirtyByIndex.size())
        {
            return fail(UIErrorCode::InvalidNode, "UI hit-test dirty node is invalid");
        }

        const u32 index = node.index();
        if (dirtyQueuedByIndex[index] == 0)
        {
            if (occupiedDirtyQueueSlotCount() >= capacityConfig.dirtyQueueCapacity)
            {
                compactDirtyQueue();
            }
            if (occupiedDirtyQueueSlotCount() >= capacityConfig.dirtyQueueCapacity && dirtyReservedByIndex[index] == 0)
            {
                return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
            }
            consumeDirtyQueueReservation(index);
            dirtyQueue.push_back(node);
            dirtyQueuedByIndex[index] = 1;
        }
        dirtyByIndex[index] |= UIDirty::HitTest;
        dirtyQueueHighWater = (std::max)(dirtyQueueHighWater, dirtyQueue.size());
        phaseDirty |= PhaseHit;
        return Core::success();
    }

    [[nodiscard]] Core::Status preflightPaintDirtyBatch(std::initializer_list<UINodeId> requestedNodes) const
    {
        usize requiredQueueEntries = 0;
        for (auto current = requestedNodes.begin(); current != requestedNodes.end(); ++current)
        {
            if (!current->hasValue())
            {
                continue;
            }
            if (!contains(*current) || current->index() >= dirtyByIndex.size())
            {
                return fail(UIErrorCode::InvalidNode, "UI paint dirty node is invalid");
            }
            bool duplicate = false;
            for (auto prior = requestedNodes.begin(); prior != current; ++prior)
            {
                if (prior->hasValue() && *prior == *current)
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && dirtyQueuedByIndex[current->index()] == 0 && dirtyReservedByIndex[current->index()] == 0)
            {
                ++requiredQueueEntries;
            }
        }

        const usize occupiedQueueEntries = validDirtyQueueCount() + dirtyQueueReservationCount;
        if (occupiedQueueEntries > capacityConfig.dirtyQueueCapacity ||
            requiredQueueEntries > capacityConfig.dirtyQueueCapacity - occupiedQueueEntries)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status markPaintDirtyBatch(std::initializer_list<UINodeId> requestedNodes)
    {
        usize uniqueNodeCount = 0;
        usize requiredQueueEntries = 0;
        for (auto current = requestedNodes.begin(); current != requestedNodes.end(); ++current)
        {
            if (!current->hasValue())
            {
                continue;
            }
            if (!contains(*current) || current->index() >= dirtyByIndex.size())
            {
                return fail(UIErrorCode::InvalidNode, "UI paint dirty node is invalid");
            }
            bool duplicate = false;
            for (auto prior = requestedNodes.begin(); prior != current; ++prior)
            {
                if (prior->hasValue() && *prior == *current)
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && dirtyQueuedByIndex[current->index()] == 0 && dirtyReservedByIndex[current->index()] == 0)
            {
                ++requiredQueueEntries;
            }
            if (!duplicate)
            {
                ++uniqueNodeCount;
            }
        }
        if (uniqueNodeCount == 0)
        {
            return Core::success();
        }

        usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        if (occupiedQueueEntries > capacityConfig.dirtyQueueCapacity ||
            requiredQueueEntries > capacityConfig.dirtyQueueCapacity - occupiedQueueEntries)
        {
            compactDirtyQueue();
            requiredQueueEntries = 0;
            for (auto current = requestedNodes.begin(); current != requestedNodes.end(); ++current)
            {
                if (!current->hasValue())
                {
                    continue;
                }
                bool duplicate = false;
                for (auto prior = requestedNodes.begin(); prior != current; ++prior)
                {
                    if (prior->hasValue() && *prior == *current)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate && dirtyQueuedByIndex[current->index()] == 0 &&
                    dirtyReservedByIndex[current->index()] == 0)
                {
                    ++requiredQueueEntries;
                }
            }
            occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        }
        if (occupiedQueueEntries > capacityConfig.dirtyQueueCapacity ||
            requiredQueueEntries > capacityConfig.dirtyQueueCapacity - occupiedQueueEntries)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
        }

        for (auto current = requestedNodes.begin(); current != requestedNodes.end(); ++current)
        {
            if (!current->hasValue())
            {
                continue;
            }
            bool duplicate = false;
            for (auto prior = requestedNodes.begin(); prior != current; ++prior)
            {
                if (prior->hasValue() && *prior == *current)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
            {
                continue;
            }
            const u32 index = current->index();
            if (dirtyQueuedByIndex[index] == 0)
            {
                consumeDirtyQueueReservation(index);
                dirtyQueue.push_back(*current);
                dirtyQueuedByIndex[index] = 1;
            }
            dirtyByIndex[index] |= UIDirty::Paint | UIDirty::Semantics;
        }
        dirtyQueueHighWater = (std::max)(dirtyQueueHighWater, dirtyQueue.size());
        phaseDirty |= PhasePaint | PhaseSemantics;
        return Core::success();
    }

    [[nodiscard]] Core::Status markPaintDirty(UINodeId node)
    {
        return markPaintDirtyBatch({node});
    }

    void clearDirtyState() noexcept
    {
        std::fill(dirtyByIndex.begin(), dirtyByIndex.end(), UIDirty::None);
        std::fill(dirtyQueuedByIndex.begin(), dirtyQueuedByIndex.end(), 0);
        dirtyQueue.clear();
        phaseDirty = UIDirty::None;
    }

    [[nodiscard]] bool isNodeWithinRoot(UINodeId root, UINodeId node) const noexcept
    {
        if (!contains(root) || !contains(node))
        {
            return false;
        }

        const NodeRecord* nodeRecord = nodes.tryGet(node.storageId());
        if (nodeRecord == nullptr)
        {
            return false;
        }
        return nodeRecord->rootIndex == root.index();
    }

    [[nodiscard]] bool isNodeWithinSubtree(UINodeId subtreeRoot, UINodeId node) const noexcept
    {
        if (!contains(subtreeRoot) || !contains(node))
        {
            return false;
        }

        u32 currentIndex = node.index();
        usize visitedCount = 0;
        while (currentIndex != InvalidNodeIndex && visitedCount++ < nodes.capacity())
        {
            if (currentIndex == subtreeRoot.index())
            {
                return true;
            }
            const NodeRecord* current = recordByIndex(currentIndex);
            if (current == nullptr)
            {
                return false;
            }
            currentIndex = current->parentIndex;
        }
        return false;
    }

    [[nodiscard]] static constexpr u16 defaultThemeBindingsFor(UIWidgetKind kind) noexcept
    {
        switch (kind)
        {
        case UIWidgetKind::Root:
        case UIWidgetKind::Panel:
            return 0;
        case UIWidgetKind::Modal:
            return ThemeBindingBoxPaint;
        case UIWidgetKind::ScrollView:
            return ThemeBindingScrollViewPaint;
        case UIWidgetKind::ListView:
            return ThemeBindingBoxPaint | ThemeBindingListViewPaint;
        case UIWidgetKind::TreeView:
            return ThemeBindingBoxPaint | ThemeBindingTreeViewPaint;
        case UIWidgetKind::Popup:
            return ThemeBindingBoxPaint;
        case UIWidgetKind::Label:
            return ThemeBindingTextStyle;
        case UIWidgetKind::Button:
            return ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle;
        case UIWidgetKind::Dropdown:
            return ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle |
                   ThemeBindingDropdownPaint;
        case UIWidgetKind::DropdownItem:
        case UIWidgetKind::ListViewItem:
        case UIWidgetKind::TreeViewItem:
            return ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle;
        case UIWidgetKind::Checkbox:
            return ThemeBindingBoxPaint | ThemeBindingCheckboxPaint;
        case UIWidgetKind::Slider:
            return ThemeBindingBoxPaint | ThemeBindingSliderPaint;
        case UIWidgetKind::TextEdit:
            return ThemeBindingBoxPaint | ThemeBindingTextStyle;
        case UIWidgetKind::ProgressBar:
            return ThemeBindingBoxPaint | ThemeBindingProgressBarPaint;
        case UIWidgetKind::RadioButton:
            return ThemeBindingRadioButtonPaint | ThemeBindingTextStyle;
        }
        return 0;
    }

    void applyBoundProductChrome(u32 index, UIWidgetKind kind, const UITheme& theme) noexcept
    {
        if (index >= themeBindingsByNodeIndex.size())
        {
            return;
        }
        const u16 bindings = themeBindingsByNodeIndex[index];
        switch (kind)
        {
        case UIWidgetKind::Root:
        case UIWidgetKind::Panel:
            break;
        case UIWidgetKind::Modal:
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] =
                    makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 248), UIElevation::Low);
            }
            break;
        case UIWidgetKind::ScrollView:
            if ((bindings & ThemeBindingScrollViewPaint) != 0)
            {
                scrollViewStatesByNodeIndex[index].paint = makeScrollViewPaint(theme);
            }
            break;
        case UIWidgetKind::ListView:
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] = makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 245));
            }
            if ((bindings & ThemeBindingListViewPaint) != 0)
            {
                listViewStatesByNodeIndex[index].paint = makeListViewPaint(theme);
            }
            break;
        case UIWidgetKind::TreeView:
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] = makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 245));
            }
            if ((bindings & ThemeBindingTreeViewPaint) != 0)
            {
                treeViewStatesByNodeIndex[index].paint = makeTreeViewPaint(theme);
            }
            break;
        case UIWidgetKind::Popup:
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] = makePopupBoxPaint(theme);
            }
            break;
        case UIWidgetKind::Label:
            if ((bindings & ThemeBindingTextStyle) != 0)
            {
                textStatesByIndex[index].style = makeBodyTextStyle(theme);
            }
            break;
        case UIWidgetKind::Button: {
            const UIButtonChrome chrome = makeButtonChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] = chrome.box;
            }
            if ((bindings & ThemeBindingButtonPaint) != 0)
            {
                buttonPaintsByNodeIndex[index] = chrome.states;
            }
            if ((bindings & ThemeBindingTextStyle) != 0)
            {
                textStatesByIndex[index].style = chrome.label;
            }
            break;
        }
        case UIWidgetKind::Dropdown: {
            const UIDropdownChrome chrome = makeDropdownChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] = chrome.box;
            }
            if ((bindings & ThemeBindingButtonPaint) != 0)
            {
                buttonPaintsByNodeIndex[index] = chrome.states;
            }
            if ((bindings & ThemeBindingTextStyle) != 0)
            {
                textStatesByIndex[index].style = chrome.label;
            }
            if ((bindings & ThemeBindingDropdownPaint) != 0)
            {
                dropdownStatesByNodeIndex[index].paint = chrome.dropdown;
            }
            break;
        }
        case UIWidgetKind::DropdownItem:
        case UIWidgetKind::ListViewItem:
        case UIWidgetKind::TreeViewItem: {
            const UIButtonChrome chrome = makeDropdownItemChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] = chrome.box;
            }
            if ((bindings & ThemeBindingButtonPaint) != 0)
            {
                buttonPaintsByNodeIndex[index] = chrome.states;
            }
            if ((bindings & ThemeBindingTextStyle) != 0)
            {
                textStatesByIndex[index].style = chrome.label;
            }
            break;
        }
        case UIWidgetKind::Checkbox: {
            const UICheckboxChrome chrome = makeCheckboxChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] = chrome.box;
            }
            if ((bindings & ThemeBindingCheckboxPaint) != 0)
            {
                checkboxPaintsByNodeIndex[index] = chrome.indicator;
            }
            break;
        }
        case UIWidgetKind::Slider: {
            const UISliderChrome chrome = makeSliderChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] = chrome.track;
            }
            if ((bindings & ThemeBindingSliderPaint) != 0)
            {
                sliderStatesByNodeIndex[index].paint = chrome.slider;
            }
            break;
        }
        case UIWidgetKind::TextEdit: {
            const UITextEditChrome chrome = makeTextEditChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] = chrome.box;
            }
            if ((bindings & ThemeBindingTextStyle) != 0)
            {
                textStatesByIndex[index].style = chrome.text;
            }
            break;
        }
        case UIWidgetKind::ProgressBar: {
            const UIProgressBarChrome chrome = makeProgressBarChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0)
            {
                boxPaintsByIndex[index] = chrome.track;
            }
            if ((bindings & ThemeBindingProgressBarPaint) != 0)
            {
                progressBarStatesByNodeIndex[index].paint = chrome.bar;
            }
            break;
        }
        case UIWidgetKind::RadioButton: {
            const UIRadioButtonChrome chrome = makeRadioButtonChrome(theme);
            if ((bindings & ThemeBindingRadioButtonPaint) != 0)
            {
                radioButtonStatesByNodeIndex[index].paint = chrome.radio;
            }
            if ((bindings & ThemeBindingTextStyle) != 0)
            {
                textStatesByIndex[index].style = chrome.label;
            }
            break;
        }
        }
    }

    void applyDefaultProductChrome(u32 index, UIWidgetKind kind) noexcept
    {
        if (index >= themeBindingsByNodeIndex.size())
        {
            return;
        }
        themeBindingsByNodeIndex[index] = defaultThemeBindingsFor(kind);
        applyBoundProductChrome(index, kind, productTheme);
    }

    [[nodiscard]] static std::optional<UITextStyle> productTextStyleFor(UIWidgetKind kind,
                                                                        const UITheme& theme) noexcept
    {
        switch (kind)
        {
        case UIWidgetKind::Label:
            return makeBodyTextStyle(theme);
        case UIWidgetKind::Button:
            return makeButtonChrome(theme).label;
        case UIWidgetKind::Dropdown:
            return makeDropdownChrome(theme).label;
        case UIWidgetKind::DropdownItem:
        case UIWidgetKind::ListViewItem:
        case UIWidgetKind::TreeViewItem:
            return makeDropdownItemChrome(theme).label;
        case UIWidgetKind::TextEdit:
            return makeTextEditChrome(theme).text;
        case UIWidgetKind::RadioButton:
            return makeRadioButtonChrome(theme).label;
        case UIWidgetKind::Root:
        case UIWidgetKind::Panel:
        case UIWidgetKind::Checkbox:
        case UIWidgetKind::Slider:
        case UIWidgetKind::ProgressBar:
        case UIWidgetKind::Modal:
        case UIWidgetKind::ScrollView:
        case UIWidgetKind::ListView:
        case UIWidgetKind::TreeView:
        case UIWidgetKind::Popup:
            return std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] static bool textMeasureInputsDiffer(const UITextStyle& left, const UITextStyle& right) noexcept
    {
        return left.logicalSize != right.logicalSize || left.advanceScale != right.advanceScale ||
               left.lineHeightScale != right.lineHeightScale;
    }

    void stageThemePaintChange(u32 index) noexcept
    {
        themeDirtyScratchByNodeIndex[index] |= ThemeDirtyPaint;
    }

    void detachThemeBinding(u32 index, u16 binding) noexcept
    {
        if (index < themeBindingsByNodeIndex.size())
        {
            themeBindingsByNodeIndex[index] &= static_cast<u16>(~binding);
        }
    }

    [[nodiscard]] Core::Status stageThemeTextStyle(u32 index, const UITextStyle& nextStyle)
    {
        WidgetTextState& state = textStatesByIndex[index];
        if (state.style == nextStyle)
        {
            return Core::success();
        }
        stageThemePaintChange(index);
        if (!state.hasContent || !textMeasureInputsDiffer(state.style, nextStyle))
        {
            themeTextMetricsScratchByNodeIndex[index] = state.metrics;
            return Core::success();
        }
        auto measured = measureWidgetText(textViewFor(index), nextStyle);
        if (!measured)
        {
            return Core::failure(measured.error());
        }
        themeTextMetricsScratchByNodeIndex[index] = *measured;
        if (*measured != state.metrics)
        {
            themeDirtyScratchByNodeIndex[index] |= ThemeDirtyLayoutSelf;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status stageBoundProductChrome(u32 index, UIWidgetKind kind, const UITheme& theme)
    {
        const u16 bindings = themeBindingsByNodeIndex[index];
        if ((bindings & ThemeBindingTextStyle) != 0)
        {
            const auto nextStyle = productTextStyleFor(kind, theme);
            if (!nextStyle.has_value())
            {
                return fail(Core::CoreErrorCode::Internal, "UI Theme text binding does not match the node kind");
            }
            if (Core::Status status = stageThemeTextStyle(index, *nextStyle); !status)
            {
                return status;
            }
        }

        switch (kind)
        {
        case UIWidgetKind::Root:
        case UIWidgetKind::Panel:
        case UIWidgetKind::Label:
            break;
        case UIWidgetKind::Modal: {
            const UIBoxPaint chrome = makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 248), UIElevation::Low);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != chrome)
            {
                stageThemePaintChange(index);
            }
            break;
        }
        case UIWidgetKind::ScrollView: {
            const UIScrollViewPaint chrome = makeScrollViewPaint(theme);
            if ((bindings & ThemeBindingScrollViewPaint) != 0 &&
                scrollViewStatesByNodeIndex[index].paint != chrome)
            {
                stageThemePaintChange(index);
                if (scrollViewStatesByNodeIndex[index].paint.thickness != chrome.thickness ||
                    scrollViewStatesByNodeIndex[index].paint.minThumbExtent != chrome.minThumbExtent)
                {
                    themeDirtyScratchByNodeIndex[index] |= ThemeDirtyLayoutSelf;
                }
            }
            break;
        }
        case UIWidgetKind::ListView: {
            const UIBoxPaint box = makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 245));
            const UIListViewPaint chrome = makeListViewPaint(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != box)
            {
                stageThemePaintChange(index);
            }
            if ((bindings & ThemeBindingListViewPaint) != 0 && listViewStatesByNodeIndex[index].paint != chrome)
            {
                stageThemePaintChange(index);
                if (listViewStatesByNodeIndex[index].paint.scrollBar.thickness != chrome.scrollBar.thickness ||
                    listViewStatesByNodeIndex[index].paint.scrollBar.minThumbExtent != chrome.scrollBar.minThumbExtent)
                {
                    themeDirtyScratchByNodeIndex[index] |= ThemeDirtyLayoutSelf;
                }
            }
            break;
        }
        case UIWidgetKind::TreeView: {
            const UIBoxPaint box = makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 245));
            const UITreeViewPaint chrome = makeTreeViewPaint(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != box)
            {
                stageThemePaintChange(index);
            }
            if ((bindings & ThemeBindingTreeViewPaint) != 0 && treeViewStatesByNodeIndex[index].paint != chrome)
            {
                stageThemePaintChange(index);
                if (treeViewStatesByNodeIndex[index].paint.scrollBar.thickness != chrome.scrollBar.thickness ||
                    treeViewStatesByNodeIndex[index].paint.scrollBar.minThumbExtent != chrome.scrollBar.minThumbExtent)
                {
                    themeDirtyScratchByNodeIndex[index] |= ThemeDirtyLayoutSelf;
                }
            }
            break;
        }
        case UIWidgetKind::Popup: {
            const UIBoxPaint chrome = makePopupBoxPaint(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != chrome)
            {
                stageThemePaintChange(index);
            }
            break;
        }
        case UIWidgetKind::Button: {
            const UIButtonChrome chrome = makeButtonChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != chrome.box)
            {
                stageThemePaintChange(index);
            }
            if ((bindings & ThemeBindingButtonPaint) != 0 && buttonPaintsByNodeIndex[index] != chrome.states)
            {
                stageThemePaintChange(index);
            }
            break;
        }
        case UIWidgetKind::Dropdown: {
            const UIDropdownChrome chrome = makeDropdownChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != chrome.box)
            {
                stageThemePaintChange(index);
            }
            if ((bindings & ThemeBindingButtonPaint) != 0 && buttonPaintsByNodeIndex[index] != chrome.states)
            {
                stageThemePaintChange(index);
            }
            if ((bindings & ThemeBindingDropdownPaint) != 0 && dropdownStatesByNodeIndex[index].paint != chrome.dropdown)
            {
                stageThemePaintChange(index);
                if (dropdownStatesByNodeIndex[index].paint.indicatorWidth != chrome.dropdown.indicatorWidth ||
                    dropdownStatesByNodeIndex[index].paint.indicatorHeight != chrome.dropdown.indicatorHeight ||
                    dropdownStatesByNodeIndex[index].paint.indicatorInset != chrome.dropdown.indicatorInset)
                {
                    themeDirtyScratchByNodeIndex[index] |= ThemeDirtyLayoutSelf;
                }
            }
            break;
        }
        case UIWidgetKind::DropdownItem:
        case UIWidgetKind::ListViewItem:
        case UIWidgetKind::TreeViewItem: {
            const UIButtonChrome chrome = makeDropdownItemChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != chrome.box)
            {
                stageThemePaintChange(index);
            }
            if ((bindings & ThemeBindingButtonPaint) != 0 && buttonPaintsByNodeIndex[index] != chrome.states)
            {
                stageThemePaintChange(index);
            }
            break;
        }
        case UIWidgetKind::Checkbox: {
            const UICheckboxChrome chrome = makeCheckboxChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != chrome.box)
            {
                stageThemePaintChange(index);
            }
            if ((bindings & ThemeBindingCheckboxPaint) != 0 && checkboxPaintsByNodeIndex[index] != chrome.indicator)
            {
                stageThemePaintChange(index);
            }
            break;
        }
        case UIWidgetKind::Slider: {
            const UISliderChrome chrome = makeSliderChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != chrome.track)
            {
                stageThemePaintChange(index);
            }
            if ((bindings & ThemeBindingSliderPaint) != 0 && sliderStatesByNodeIndex[index].paint != chrome.slider)
            {
                stageThemePaintChange(index);
            }
            break;
        }
        case UIWidgetKind::TextEdit: {
            const UITextEditChrome chrome = makeTextEditChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != chrome.box)
            {
                stageThemePaintChange(index);
            }
            break;
        }
        case UIWidgetKind::ProgressBar: {
            const UIProgressBarChrome chrome = makeProgressBarChrome(theme);
            if ((bindings & ThemeBindingBoxPaint) != 0 && boxPaintsByIndex[index] != chrome.track)
            {
                stageThemePaintChange(index);
            }
            if ((bindings & ThemeBindingProgressBarPaint) != 0 &&
                progressBarStatesByNodeIndex[index].paint != chrome.bar)
            {
                stageThemePaintChange(index);
            }
            break;
        }
        case UIWidgetKind::RadioButton: {
            const UIRadioButtonChrome chrome = makeRadioButtonChrome(theme);
            if ((bindings & ThemeBindingRadioButtonPaint) != 0 &&
                radioButtonStatesByNodeIndex[index].paint != chrome.radio)
            {
                stageThemePaintChange(index);
                if (radioButtonStatesByNodeIndex[index].paint.labelGap != chrome.radio.labelGap)
                {
                    themeDirtyScratchByNodeIndex[index] |= ThemeDirtyLayoutSelf;
                }
            }
            break;
        }
        }
        return Core::success();
    }

    void propagateThemeLayoutDirtyToAncestors() noexcept
    {
        for (u32 index = 0; index < themeDirtyScratchByNodeIndex.size(); ++index)
        {
            if ((themeDirtyScratchByNodeIndex[index] & ThemeDirtyLayoutSelf) == 0)
            {
                continue;
            }
            const NodeRecord* record = recordByIndex(index);
            u32 parentIndex = record == nullptr ? InvalidNodeIndex : record->parentIndex;
            while (parentIndex != InvalidNodeIndex)
            {
                themeDirtyScratchByNodeIndex[parentIndex] |= ThemeDirtyLayoutAncestor;
                record = recordByIndex(parentIndex);
                parentIndex = record == nullptr ? InvalidNodeIndex : record->parentIndex;
            }
        }
    }

    [[nodiscard]] Core::Status preflightThemeDirtyQueue()
    {
        compactDirtyQueue();
        usize requiredQueueEntries = 0;
        for (u32 index = 0; index < themeDirtyScratchByNodeIndex.size(); ++index)
        {
            if (themeDirtyScratchByNodeIndex[index] != 0 && dirtyQueuedByIndex[index] == 0 &&
                dirtyReservedByIndex[index] == 0)
            {
                ++requiredQueueEntries;
            }
        }
        const usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        if (occupiedQueueEntries > capacityConfig.dirtyQueueCapacity ||
            requiredQueueEntries > capacityConfig.dirtyQueueCapacity - occupiedQueueEntries)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI Theme update exceeds dirty queue capacity");
        }
        return Core::success();
    }

    void publishThemeDirtyState() noexcept
    {
        constexpr UIDirty ChangedNodeLayoutDirty = UIDirty::Style | UIDirty::Measure | UIDirty::Arrange |
                                                   UIDirty::Composite | UIDirty::HitTest | UIDirty::Semantics;
        constexpr UIDirty AncestorLayoutDirty =
            UIDirty::Measure | UIDirty::Arrange | UIDirty::Composite | UIDirty::HitTest;
        bool hasLayoutChange = false;
        bool hasPaintChange = false;
        for (u32 index = 0; index < themeDirtyScratchByNodeIndex.size(); ++index)
        {
            const u8 staged = themeDirtyScratchByNodeIndex[index];
            if (staged == 0)
            {
                continue;
            }
            if (dirtyQueuedByIndex[index] == 0)
            {
                consumeDirtyQueueReservation(index);
                dirtyQueue.push_back(idForIndex(index));
                dirtyQueuedByIndex[index] = 1;
            }
            if ((staged & ThemeDirtyLayoutSelf) != 0)
            {
                dirtyByIndex[index] |= ChangedNodeLayoutDirty;
                hasLayoutChange = true;
            } else if ((staged & ThemeDirtyLayoutAncestor) != 0)
            {
                dirtyByIndex[index] |= AncestorLayoutDirty;
                hasLayoutChange = true;
            }
            if ((staged & ThemeDirtyPaint) != 0)
            {
                dirtyByIndex[index] |= UIDirty::Paint | UIDirty::Semantics;
                hasPaintChange = true;
            }
        }
        dirtyQueueHighWater = (std::max)(dirtyQueueHighWater, dirtyQueue.size());
        if (hasLayoutChange)
        {
            phaseDirty |= PhaseLayout | PhaseHit;
        }
        if (hasPaintChange)
        {
            phaseDirty |= PhasePaint | PhaseSemantics;
        }
    }

    [[nodiscard]] Core::Status setProductTheme(const UITheme& theme)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status validation = validateProductTheme(theme); !validation)
        {
            return validation;
        }
        if (productTheme == theme)
        {
            return Core::success();
        }

        std::fill(themeDirtyScratchByNodeIndex.begin(), themeDirtyScratchByNodeIndex.end(), u8{0});
        for (u32 index = 0; index < themeBindingsByNodeIndex.size(); ++index)
        {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr || themeBindingsByNodeIndex[index] == 0)
            {
                continue;
            }
            if (Core::Status staged = stageBoundProductChrome(index, record->kind, theme); !staged)
            {
                return staged;
            }
        }
        propagateThemeLayoutDirtyToAncestors();
        if (Core::Status capacity = preflightThemeDirtyQueue(); !capacity)
        {
            return capacity;
        }

        for (u32 index = 0; index < themeBindingsByNodeIndex.size(); ++index)
        {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr || themeBindingsByNodeIndex[index] == 0)
            {
                continue;
            }
            const auto nextTextStyle = productTextStyleFor(record->kind, theme);
            WidgetTextState& textState = textStatesByIndex[index];
            if ((themeBindingsByNodeIndex[index] & ThemeBindingTextStyle) != 0 && nextTextStyle.has_value() &&
                textState.hasContent && textMeasureInputsDiffer(textState.style, *nextTextStyle))
            {
                textState.metrics = themeTextMetricsScratchByNodeIndex[index];
            }
            applyBoundProductChrome(index, record->kind, theme);
        }
        productTheme = theme;
        publishThemeDirtyState();
        return Core::success();
    }

    [[nodiscard]] Core::Result<UINodeId> createNode(UIWidgetKind kind)
    {
        auto idResult = nodes.tryEmplace();
        if (!idResult)
        {
            const Core::Error& error = idResult.error();
            if (error.code == Core::CoreErrorCode::CapacityExceeded)
            {
                return fail(UIErrorCode::CapacityExceeded, "UI node capacity has been exhausted");
            }
            return Core::failure(error);
        }

        const UINodeId node = UINodeId::create(ownerWindow, *idResult);
        idsByIndex[node.index()] = node;
        resetNodeSideData(node.index());
        NodeRecord* record = nodes.tryGet(node.storageId());
        record->kind = kind;
        record->rootIndex = node.index();
        if (kind == UIWidgetKind::Modal || kind == UIWidgetKind::Popup)
        {
            focusScopeModesByNodeIndex[node.index()] = UIFocusScopeMode::Contain;
        }
        if (kind == UIWidgetKind::Popup)
        {
            layoutStylesByIndex[node.index()].position = UILayoutPositionMode::AbsoluteOverlay;
        }
        // Interactive controls are targetable. Label remains read-only and
        // decorative unless the caller explicitly changes its hit policy.
        pointerHitPoliciesByIndex[node.index()] =
            (kind == UIWidgetKind::Button || kind == UIWidgetKind::Checkbox || kind == UIWidgetKind::Slider ||
             kind == UIWidgetKind::TextEdit || kind == UIWidgetKind::RadioButton || kind == UIWidgetKind::ScrollView ||
             kind == UIWidgetKind::Dropdown || kind == UIWidgetKind::DropdownItem || kind == UIWidgetKind::ListView ||
             kind == UIWidgetKind::ListViewItem || kind == UIWidgetKind::TreeView || kind == UIWidgetKind::TreeViewItem)
                ? UIPointerHitPolicy::Targetable
                : UIPointerHitPolicy::Ignore;
        if (capacityConfig.applyDefaultProductChrome)
        {
            applyDefaultProductChrome(node.index(), kind);
        }
        return node;
    }

    [[nodiscard]] Core::Result<UIRootOwner> createRoot(UIContext& context)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (liveRootCount >= capacityConfig.rootCapacity)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI root capacity has been exhausted");
        }

        auto nodeResult = createNode(UIWidgetKind::Root);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }

        const UINodeId root = *nodeResult;
        NodeRecord* rootRecord = nodes.tryGet(root.storageId());
        rootRecord->parentIndex = InvalidNodeIndex;
        rootRecord->previousSiblingIndex = lastRootIndex;
        rootRecord->nextSiblingIndex = InvalidNodeIndex;
        rootRecord->depth = 0;

        if (lastRootIndex != InvalidNodeIndex)
        {
            recordByIndex(lastRootIndex)->nextSiblingIndex = root.index();
        } else
        {
            firstRootIndex = root.index();
        }
        lastRootIndex = root.index();
        ++liveRootCount;
        markStructureChanged();
        return UIRootOwner(context.m_impl->lifetime, root);
    }

    [[nodiscard]] Core::Result<UINodeId> createChild(UINodeId parent, UIWidgetKind kind)
    {
        if (kind == UIWidgetKind::Root)
        {
            return fail(UIErrorCode::InvalidParent, "Root nodes cannot be created as children");
        }
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();

        auto parentResult = resolveParent(parent);
        if (!parentResult)
        {
            return Core::failure(parentResult.error());
        }
        NodeRecord& parentRecord = **parentResult;
        if (kind == UIWidgetKind::Popup)
        {
            if (parentRecord.kind != UIWidgetKind::Dropdown)
            {
                return fail(UIErrorCode::InvalidParent, "UI Popup requires a Dropdown parent");
            }
            if (parent.index() >= dropdownStatesByNodeIndex.size())
            {
                return fail(Core::CoreErrorCode::Internal, "UI Dropdown state index is out of range");
            }
            if (contains(dropdownStatesByNodeIndex[parent.index()].popup))
            {
                return fail(UIErrorCode::InvalidParent, "UI Dropdown already owns a Popup");
            }
        } else if (kind == UIWidgetKind::DropdownItem)
        {
            if (parentRecord.kind != UIWidgetKind::Popup)
            {
                return fail(UIErrorCode::InvalidParent, "UI DropdownItem requires a Popup parent");
            }
        } else if (kind == UIWidgetKind::ListViewItem)
        {
            if (parentRecord.kind != UIWidgetKind::ListView)
            {
                return fail(UIErrorCode::InvalidParent, "UI ListViewItem requires a ListView parent");
            }
        } else if (kind == UIWidgetKind::TreeViewItem)
        {
            if (parentRecord.kind != UIWidgetKind::TreeView)
            {
                return fail(UIErrorCode::InvalidParent, "UI TreeViewItem requires a TreeView parent");
            }
        } else if (parentRecord.kind == UIWidgetKind::Dropdown || parentRecord.kind == UIWidgetKind::Popup)
        {
            return fail(UIErrorCode::InvalidParent,
                        "UI Dropdown composites only accept Popup and DropdownItem children");
        } else if (parentRecord.kind == UIWidgetKind::ListView)
        {
            return fail(UIErrorCode::InvalidParent, "UI ListView only accepts its internal item rows");
        } else if (parentRecord.kind == UIWidgetKind::TreeView)
        {
            return fail(UIErrorCode::InvalidParent, "UI TreeView only accepts its internal item rows");
        }
        if (parentRecord.depth == (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::InvalidParent, "UI parent depth cannot be represented");
        }

        auto nodeResult = createNode(kind);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }

        const UINodeId node = *nodeResult;
        NodeRecord* childRecord = nodes.tryGet(node.storageId());
        childRecord->parentIndex = parent.index();
        childRecord->previousSiblingIndex = parentRecord.lastChildIndex;
        childRecord->nextSiblingIndex = InvalidNodeIndex;
        childRecord->rootIndex = parentRecord.rootIndex;
        childRecord->depth = parentRecord.depth + 1;

        if (parentRecord.lastChildIndex != InvalidNodeIndex)
        {
            recordByIndex(parentRecord.lastChildIndex)->nextSiblingIndex = node.index();
        } else
        {
            parentRecord.firstChildIndex = node.index();
        }
        parentRecord.lastChildIndex = node.index();
        if (kind == UIWidgetKind::Popup)
        {
            dropdownStatesByNodeIndex[parent.index()].popup = node;
        }
        markStructureChanged();
        return node;
    }

    [[nodiscard]] Core::Result<UINodeId> createListViewComposite(UINodeId parent, UIListViewCreateConfig config)
    {
        auto normalized = normalizeListViewCreateConfig(config);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        const usize requiredNodes = static_cast<usize>(normalized->materializedItemCapacity) + 1U;
        if (nodes.availableCount() < requiredNodes)
        {
            return fail(UIErrorCode::CapacityExceeded,
                        "UI ListView row pool exceeds the remaining node capacity");
        }

        auto listResult = createChild(parent, UIWidgetKind::ListView);
        if (!listResult)
        {
            return Core::failure(listResult.error());
        }
        const UINodeId listView = *listResult;
        auto rollback = Core::makeScopeExit([this, listView]() noexcept {
            if (contains(listView))
            {
                static_cast<void>(destroySubtree(listView));
            }
        });
        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        state.materializedItemCapacity = normalized->materializedItemCapacity;

        for (u32 row = 0; row < normalized->materializedItemCapacity; ++row)
        {
            auto itemResult = createChild(listView, UIWidgetKind::ListViewItem);
            if (!itemResult)
            {
                return Core::failure(itemResult.error());
            }
            const UINodeId item = *itemResult;
            UILayoutStyle& itemLayout = layoutStylesByIndex[item.index()];
            itemLayout.size.height = UILayoutLength::Px(state.style.rowHeight);
            itemLayout.padding = UIEdgeSpacing::HorizontalVertical(8.0F, 4.0F);
        }
        rollback.release();
        return listView;
    }

    [[nodiscard]] Core::Result<UINodeId> createTreeViewComposite(UINodeId parent, UITreeViewCreateConfig config)
    {
        auto normalized = normalizeTreeViewCreateConfig(config);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        const usize requiredNodes = static_cast<usize>(normalized->materializedItemCapacity) + 1U;
        if (nodes.availableCount() < requiredNodes)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI TreeView row pool exceeds the remaining node capacity");
        }

        auto treeResult = createChild(parent, UIWidgetKind::TreeView);
        if (!treeResult)
        {
            return Core::failure(treeResult.error());
        }
        const UINodeId treeView = *treeResult;
        auto rollback = Core::makeScopeExit([this, treeView]() noexcept {
            if (contains(treeView))
            {
                static_cast<void>(destroySubtree(treeView));
            }
        });
        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        state.materializedItemCapacity = normalized->materializedItemCapacity;

        for (u32 row = 0; row < normalized->materializedItemCapacity; ++row)
        {
            auto itemResult = createChild(treeView, UIWidgetKind::TreeViewItem);
            if (!itemResult)
            {
                return Core::failure(itemResult.error());
            }
            const UINodeId item = *itemResult;
            UILayoutStyle& itemLayout = layoutStylesByIndex[item.index()];
            itemLayout.size.height = UILayoutLength::Px(state.style.rowHeight);
            itemLayout.padding = UIEdgeSpacing::HorizontalVertical(8.0F, 4.0F);
        }
        rollback.release();
        return treeView;
    }

    [[nodiscard]] Core::Result<UINodeId> createChildFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                                                UIWidgetKind kind)
    {
        if (kind == UIWidgetKind::Root)
        {
            return fail(UIErrorCode::InvalidParent, "Root nodes cannot be created as children");
        }
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto parentResult = resolveParent(parent);
        if (!parentResult)
        {
            return Core::failure(parentResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, parent))
        {
            return fail(UIErrorCode::InvalidNode, "UI parent is not owned by the updater root");
        }

        return createChild(parent, kind);
    }

    [[nodiscard]] Core::Result<UINodeId> createListViewFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                                                  UIListViewCreateConfig config)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto parentResult = resolveParent(parent);
        if (!parentResult)
        {
            return Core::failure(parentResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, parent))
        {
            return fail(UIErrorCode::InvalidNode, "UI ListView parent is not owned by the updater root");
        }
        return createListViewComposite(parent, config);
    }

    [[nodiscard]] Core::Result<UINodeId> createTreeViewFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                                                   UITreeViewCreateConfig config)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto parentResult = resolveParent(parent);
        if (!parentResult)
        {
            return Core::failure(parentResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, parent))
        {
            return fail(UIErrorCode::InvalidNode, "UI TreeView parent is not owned by the updater root");
        }
        return createTreeViewComposite(parent, config);
    }

    void unlinkFromTree(u32 index, NodeRecord& record) noexcept
    {
        if (record.parentIndex != InvalidNodeIndex)
        {
            NodeRecord* parent = recordByIndex(record.parentIndex);
            if (parent != nullptr)
            {
                if (parent->firstChildIndex == index)
                {
                    parent->firstChildIndex = record.nextSiblingIndex;
                }
                if (parent->lastChildIndex == index)
                {
                    parent->lastChildIndex = record.previousSiblingIndex;
                }
            }
        } else
        {
            if (firstRootIndex == index)
            {
                firstRootIndex = record.nextSiblingIndex;
            }
            if (lastRootIndex == index)
            {
                lastRootIndex = record.previousSiblingIndex;
            }
        }

        if (record.previousSiblingIndex != InvalidNodeIndex)
        {
            if (NodeRecord* previous = recordByIndex(record.previousSiblingIndex); previous != nullptr)
            {
                previous->nextSiblingIndex = record.nextSiblingIndex;
            }
        }
        if (record.nextSiblingIndex != InvalidNodeIndex)
        {
            if (NodeRecord* next = recordByIndex(record.nextSiblingIndex); next != nullptr)
            {
                next->previousSiblingIndex = record.previousSiblingIndex;
            }
        }
    }

    void eraseDetachedSubtree(u32 index) noexcept
    {
        u32 currentIndex = index;
        while (currentIndex != InvalidNodeIndex)
        {
            NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }

            if (record->firstChildIndex != InvalidNodeIndex)
            {
                currentIndex = record->firstChildIndex;
                continue;
            }

            const u32 parentIndex = record->parentIndex;
            const u32 nextSiblingIndex = record->nextSiblingIndex;
            if (currentIndex != index)
            {
                unlinkFromTree(currentIndex, *record);
            }

            const UINodeId node = idForIndex(currentIndex);
            if (record->kind == UIWidgetKind::Modal && currentIndex < focusRestoreByNodeIndex.size())
            {
                const auto& committedEntries = committedHitBuffers[publishedHitBufferIndex];
                const u32 destroyedModalEntryIndex = findHitEntryIndex(node, committedEntries);
                if (committedActiveModalEntryIndex < committedEntries.size() &&
                    destroyedModalEntryIndex < committedEntries.size() &&
                    hitEntryIsWithinScope(committedActiveModalEntryIndex, destroyedModalEntryIndex, committedEntries))
                {
                    // Post-order destruction visits the active Modal before its
                    // committed Modal ancestors. The last stored value therefore
                    // restores through every removed layer to the surviving scope.
                    pendingDestroyedModalRestoreFocus = focusRestoreByNodeIndex[currentIndex];
                    hasPendingDestroyedModalRestoreFocus = true;
                }
            }
            if (record->kind == UIWidgetKind::DropdownItem)
            {
                const UINodeId dropdown = dropdownForItem(node);
                if (dropdown.hasValue() && dropdown.index() < dropdownStatesByNodeIndex.size() &&
                    dropdownStatesByNodeIndex[dropdown.index()].selectedItem == node)
                {
                    dropdownStatesByNodeIndex[dropdown.index()].selectedItem = {};
                }
            }
            if (record->kind == UIWidgetKind::Popup)
            {
                const UINodeId dropdown = dropdownForPopup(node);
                if (dropdown.hasValue() && dropdown.index() < dropdownStatesByNodeIndex.size())
                {
                    DropdownState& dropdownState = dropdownStatesByNodeIndex[dropdown.index()];
                    if (dropdownState.popup == node)
                    {
                        dropdownState.popup = {};
                        dropdownState.selectedItem = {};
                    }
                }
                if (activePopupNode == node)
                {
                    activePopupNode = {};
                    popupDismissPointerBarrierActive = false;
                    dropdownCommandPressLatch.clear();
                }
            }
            deactivateAllRoutedPointerListenersForNode(currentIndex);
            deactivateButtonActionForNode(currentIndex);
            sliderChangeCallbackRegistry.clearNode(currentIndex, true);
            idsByIndex[currentIndex] = {};
            static_cast<void>(nodes.erase(node.storageId()));
            resetNodeSideData(currentIndex);
            routedPointerListenerRegistry.reclaim(routeDispatchDepth != 0);
            buttonActionRegistry.reclaim(routeDispatchDepth != 0);
            sliderChangeCallbackRegistry.reclaim(routeDispatchDepth != 0);

            if (currentIndex == index)
            {
                return;
            }
            currentIndex = nextSiblingIndex != InvalidNodeIndex ? nextSiblingIndex : parentIndex;
        }
    }

    [[nodiscard]] Core::Status destroySubtree(UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }

        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }

        dispatchPointerCancelForSubtree(node);
        NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr)
        {
            // A PointerCancel listener may synchronously complete the requested
            // destruction. Generation validation makes that outcome idempotent.
            return Core::success();
        }
        const bool wasRoot = record->kind == UIWidgetKind::Root;
        unlinkFromTree(node.index(), *record);
        eraseDetachedSubtree(node.index());
        if (wasRoot && liveRootCount > 0)
        {
            --liveRootCount;
        }
        markStructureChanged();
        return Core::success();
    }

    void destroyRootImmediately(UINodeId root) noexcept
    {
        if (!isOwnerThread() || !contains(root))
        {
            return;
        }

        dispatchPointerCancelForSubtree(root);
        NodeRecord* rootRecord = nodes.tryGet(root.storageId());
        if (rootRecord == nullptr || rootRecord->kind != UIWidgetKind::Root)
        {
            return;
        }

        unlinkFromTree(root.index(), *rootRecord);
        eraseDetachedSubtree(root.index());
        if (liveRootCount > 0)
        {
            --liveRootCount;
        }
        markStructureChanged();
    }

    [[nodiscard]] Core::Status destroyFromUpdater(UINodeId updaterRoot, UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        if (!contains(node))
        {
            auto nodeResult = resolveNode(node);
            return nodeResult ? Core::success() : Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }

        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record != nullptr && record->kind == UIWidgetKind::ListViewItem)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI ListView item rows are internal and cannot be destroyed independently");
        }
        if (record != nullptr && record->kind == UIWidgetKind::TreeViewItem)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI TreeView item rows are internal and cannot be destroyed independently");
        }

        if (sameNode(updaterRoot, node))
        {
            return fail(UIErrorCode::RootRequired, "Destroying a root node requires UIRootOwner::reset");
        }
        return destroySubtree(node);
    }

    [[nodiscard]] Core::Status setLayoutStyleFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                         const UILayoutStyle& style)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto normalizedStyle = normalizeLayoutStyle(style);
        if (!normalizedStyle)
        {
            return Core::failure(normalizedStyle.error());
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }

        UILayoutStyle& currentStyle = layoutStylesByIndex[node.index()];
        if (currentStyle == *normalizedStyle)
        {
            return Core::success();
        }

        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
        {
            return dirtyStatus;
        }
        currentStyle = *normalizedStyle;
        return Core::success();
    }

    [[nodiscard]] Core::Status setPointerHitPolicyFromUpdater(UINodeId updaterRoot, UINodeId node,
                                                              UIPointerHitPolicy policy)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        if (!isValidPointerHitPolicy(policy))
        {
            return fail(UIErrorCode::InvalidPointerPolicy, "UI pointer hit policy is not recognized");
        }

        UIPointerHitPolicy& currentPolicy = pointerHitPoliciesByIndex[node.index()];
        if (currentPolicy == policy)
        {
            return Core::success();
        }
        const bool clearHover = hoveredPrimaryButton == node && policy != UIPointerHitPolicy::Targetable;
        if (clearHover)
        {
            if (Core::Status dirtyStatus = markPaintDirty(node); !dirtyStatus)
            {
                return dirtyStatus;
            }
        }
        if (Core::Status dirtyStatus = markHitTestDirty(node); !dirtyStatus)
        {
            return dirtyStatus;
        }
        currentPolicy = policy;
        if (clearHover)
        {
            clearHoveredPrimaryButton();
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setEnabledFromUpdater(UINodeId updaterRoot, UINodeId node, bool enabled)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        if (!isSemanticsPublishedKind((*nodeResult)->kind))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI enabled state requires a published widget node");
        }
        if (node.index() >= enabledByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI enabled state index is out of range");
        }

        const u8 next = enabled ? 1 : 0;
        if (enabledByNodeIndex[node.index()] == next)
        {
            return Core::success();
        }

        UINodeId popupToClose{};
        if (!enabled)
        {
            if ((*nodeResult)->kind == UIWidgetKind::Dropdown)
            {
                const UINodeId popup = popupForDropdown(node);
                if (popup.hasValue() && popupStatesByNodeIndex[popup.index()].open)
                {
                    popupToClose = popup;
                }
            } else if ((*nodeResult)->kind == UIWidgetKind::Popup &&
                       popupStatesByNodeIndex[node.index()].open)
            {
                popupToClose = node;
            }
        }

        // Dirty capacity is reserved before interaction state changes so a
        // rejected setter leaves enabled/focus/arm state untouched.
        releaseRouteDirtyQueueReservations();
        addRouteDirtyReservationCandidate(node);
        if (popupToClose.hasValue())
        {
            addRouteLayoutDirtyReservationCandidates(popupToClose);
            addRouteLayoutDirtyReservationCandidates(dropdownForPopup(popupToClose));
        }
        if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
        {
            releaseRouteDirtyQueueReservations();
            return reservation;
        }
        auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
        if (Core::Status dirty = markPaintDirty(node); !dirty)
        {
            return dirty;
        }
        if (popupToClose.hasValue())
        {
            if (Core::Status closed = setPopupOpenState(popupToClose, false); !closed)
            {
                return closed;
            }
        }
        if (!enabled && capturedPointerNode == node)
        {
            dispatchPointerCancelForCurrentCapture();
            if (!contains(node))
            {
                return Core::success();
            }
        }
        enabledByNodeIndex[node.index()] = next;
        if (!enabled)
        {
            defaultActionPressState.clearNode(node);
            if (hoveredPrimaryButton == node)
            {
                hoveredPrimaryButton = {};
            }
            if (armedPrimaryButton == node)
            {
                clearArmedPrimaryButton();
            }
            if (armedSlider == node)
            {
                clearArmedSlider();
            }
            if (armedScrollView == node)
            {
                clearArmedScrollView();
            }
            if (armedTextEdit == node)
            {
                clearArmedTextEdit();
            }
            if (capturedPointerNode == node)
            {
                capturedPointerNode = {};
            }
            if (defaultActionFocusButton == node)
            {
                defaultActionFocusButton = {};
            }
            if (textInputFocus == node)
            {
                textInputFocus = {};
                resetImeCompositionState();
            }
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isEnabledFromUpdater(UINodeId updaterRoot, UINodeId node) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = const_cast<Impl*>(this)->resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        if (!isSemanticsPublishedKind((*nodeResult)->kind))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI enabled state requires a published widget node");
        }
        if (node.index() >= enabledByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI enabled state index is out of range");
        }
        return enabledByNodeIndex[node.index()] != 0;
    }

    [[nodiscard]] Core::Status setFocusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node, UIFocusScopeMode mode)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI focus scope is not owned by the updater root");
        }
        if (!isValidFocusScopeMode(mode))
        {
            return fail(UIErrorCode::InvalidFocusScope, "UI focus-scope mode is not recognized");
        }
        if (((*nodeResult)->kind == UIWidgetKind::Modal || (*nodeResult)->kind == UIWidgetKind::Popup) &&
            mode != UIFocusScopeMode::Contain)
        {
            return fail(UIErrorCode::InvalidFocusScope, "UI Modal and Popup nodes always contain focus");
        }
        UIFocusScopeMode& current = focusScopeModesByNodeIndex[node.index()];
        if (current == mode)
        {
            return Core::success();
        }
        if (Core::Status dirty = markHitTestDirty(node); !dirty)
        {
            return dirty;
        }
        current = mode;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIFocusScopeMode> focusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = const_cast<Impl*>(this)->resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI focus scope is not owned by the updater root");
        }
        return focusScopeModesByNodeIndex[node.index()];
    }

    [[nodiscard]] Core::Status setBoxPaintFromUpdater(UINodeId updaterRoot, UINodeId node, const UIBoxPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }

        const UIBoxPaint normalizedPaint = normalizeBoxPaint(paint);
        UIBoxPaint& currentPaint = boxPaintsByIndex[node.index()];
        if (currentPaint == normalizedPaint)
        {
            detachThemeBinding(node.index(), ThemeBindingBoxPaint);
            return Core::success();
        }
        if (Core::Status dirtyStatus = markPaintDirty(node); !dirtyStatus)
        {
            return dirtyStatus;
        }
        currentPaint = normalizedPaint;
        detachThemeBinding(node.index(), ThemeBindingBoxPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Status setButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                         const UIButtonPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto buttonResult = resolvePlainButton(button);
        if (!buttonResult)
        {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
        }
        if (button.index() >= buttonPaintsByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Button paint index is out of range");
        }
        UIButtonPaint& currentPaint = buttonPaintsByNodeIndex[button.index()];
        if (currentPaint == paint)
        {
            detachThemeBinding(button.index(), ThemeBindingButtonPaint);
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(button); !dirty)
        {
            return dirty;
        }
        currentPaint = paint;
        detachThemeBinding(button.index(), ThemeBindingButtonPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIButtonPaint> buttonPaintFromUpdater(UINodeId updaterRoot, UINodeId button) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto buttonResult = const_cast<Impl*>(this)->resolvePlainButton(button);
        if (!buttonResult)
        {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
        }
        if (button.index() >= buttonPaintsByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Button paint index is out of range");
        }
        return buttonPaintsByNodeIndex[button.index()];
    }

    [[nodiscard]] Core::Status setTextFromUpdater(UINodeId updaterRoot, UINodeId node, std::string_view utf8)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind))
        {
            return fail(UIErrorCode::InvalidText,
                        "UI text is only supported on Label, Button, RadioButton, and TextEdit nodes");
        }
        if ((record->kind == UIWidgetKind::TextEdit || record->kind == UIWidgetKind::RadioButton) &&
            containsLineBreak(utf8))
        {
            return fail(UIErrorCode::InvalidText,
                        "UI TextEdit and RadioButton accept one logical line without CR or LF");
        }
        if (utf8.size() > (std::numeric_limits<u32>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI text payload is too large");
        }

        auto metrics = measureWidgetText(utf8, textStatesByIndex[node.index()].style);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }

        WidgetTextState& state = textStatesByIndex[node.index()];
        const std::string_view current = textViewFor(node.index());
        const bool clearActiveIme =
            record->kind == UIWidgetKind::TextEdit && textInputFocus == node && imeComposition.active();
        if (state.hasContent == !utf8.empty() && current == utf8 && state.metrics == *metrics)
        {
            if (clearActiveIme)
            {
                return clearImeComposition();
            }
            return Core::success();
        }

        TextByteAllocation reservedAllocation{};
        bool reservedNewAllocation = false;
        if (!utf8.empty() && state.allocation.capacity < utf8.size())
        {
            auto allocation = textStorage.allocate(static_cast<u32>(utf8.size()));
            if (!allocation)
            {
                return Core::failure(allocation.error());
            }
            reservedAllocation = *allocation;
            reservedNewAllocation = true;
        }

        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
        {
            if (reservedNewAllocation)
            {
                textStorage.release(reservedAllocation);
            }
            return dirtyStatus;
        }
        if (Core::Status paintStatus = markPaintDirty(node); !paintStatus)
        {
            if (reservedNewAllocation)
            {
                textStorage.release(reservedAllocation);
            }
            return paintStatus;
        }

        if (utf8.empty())
        {
            textStorage.release(state.allocation);
            state.allocation = {};
            state.length = 0;
            state.metrics = {};
            state.hasContent = false;
            if (record->kind == UIWidgetKind::TextEdit)
            {
                textEditStatesByNodeIndex[node.index()].selection = {};
                if (clearActiveIme)
                {
                    resetImeCompositionState();
                }
            }
            return Core::success();
        }

        if (reservedNewAllocation)
        {
            textStorage.release(state.allocation);
            state.allocation = reservedAllocation;
        }
        textStorage.write(state.allocation, utf8);
        state.length = static_cast<u32>(utf8.size());
        state.metrics = *metrics;
        state.hasContent = true;
        if (record->kind == UIWidgetKind::TextEdit)
        {
            UITextSelection& selection = textEditStatesByNodeIndex[node.index()].selection;
            selection.anchorCodepoint = (std::min)(selection.anchorCodepoint, metrics->codepointCount);
            selection.caretCodepoint = (std::min)(selection.caretCodepoint, metrics->codepointCount);
            if (clearActiveIme)
            {
                resetImeCompositionState();
            }
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setTextStyleFromUpdater(UINodeId updaterRoot, UINodeId node, const UITextStyle& style)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind))
        {
            return fail(UIErrorCode::InvalidText,
                        "UI text style is only supported on Label, Button, RadioButton, and TextEdit nodes");
        }

        WidgetTextState& state = textStatesByIndex[node.index()];
        if (state.style == style)
        {
            detachThemeBinding(node.index(), ThemeBindingTextStyle);
            return Core::success();
        }

        UITextMetrics metrics{};
        if (state.hasContent)
        {
            auto measured = measureWidgetText(textViewFor(node.index()), style);
            if (!measured)
            {
                return Core::failure(measured.error());
            }
            metrics = *measured;
        }

        if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
        {
            return dirtyStatus;
        }
        if (Core::Status paintStatus = markPaintDirty(node); !paintStatus)
        {
            return paintStatus;
        }
        state.style = style;
        state.metrics = metrics;
        detachThemeBinding(node.index(), ThemeBindingTextStyle);
        return Core::success();
    }

    [[nodiscard]] Core::Result<std::string_view> textFromUpdater(UINodeId updaterRoot, UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind))
        {
            return fail(UIErrorCode::InvalidText,
                        "UI text is only supported on Label, Button, RadioButton, and TextEdit nodes");
        }
        return textViewFor(node.index());
    }

    [[nodiscard]] Core::Result<UITextStyle> textStyleFromUpdater(UINodeId updaterRoot, UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }
        const NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr || !supportsWidgetText(record->kind))
        {
            return fail(UIErrorCode::InvalidText,
                        "UI text style is only supported on Label, Button, RadioButton, and TextEdit nodes");
        }
        return textStatesByIndex[node.index()].style;
    }

    [[nodiscard]] Core::Status setTextSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit,
                                                           UITextSelection selection)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = resolveNode(textEdit);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        const NodeRecord* record = nodes.tryGet(textEdit.storageId());
        if (record == nullptr || record->kind != UIWidgetKind::TextEdit)
        {
            return fail(UIErrorCode::InvalidText, "UI selection requires a TextEdit node");
        }
        if (!isNodeWithinRoot(updaterRoot, textEdit))
        {
            return fail(UIErrorCode::InvalidNode, "UI TextEdit is not owned by the updater root");
        }
        const u32 codepointCount = textStatesByIndex[textEdit.index()].metrics.codepointCount;
        if (selection.anchorCodepoint > codepointCount || selection.caretCodepoint > codepointCount)
        {
            return fail(UIErrorCode::InvalidText, "UI TextEdit selection exceeds the text length");
        }
        TextEditState& state = textEditStatesByNodeIndex[textEdit.index()];
        if (state.selection == selection)
        {
            return Core::success();
        }
        if (Core::Status dirtyStatus = markPaintDirty(textEdit); !dirtyStatus)
        {
            return dirtyStatus;
        }
        if (textInputFocus == textEdit)
        {
            static_cast<void>(clearImeComposition());
        }
        state.selection = selection;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITextSelection> textSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto nodeResult = const_cast<Impl*>(this)->resolveNode(textEdit);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        const NodeRecord* record = nodes.tryGet(textEdit.storageId());
        if (record == nullptr || record->kind != UIWidgetKind::TextEdit)
        {
            return fail(UIErrorCode::InvalidText, "UI selection requires a TextEdit node");
        }
        if (!isNodeWithinRoot(updaterRoot, textEdit))
        {
            return fail(UIErrorCode::InvalidNode, "UI TextEdit is not owned by the updater root");
        }
        return textEditStatesByNodeIndex[textEdit.index()].selection;
    }

    [[nodiscard]] Core::Status setButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                          UIButtonActionCallback&& callback)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto buttonResult = resolveButton(button);
        if (!buttonResult)
        {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
        }
        if (!callback.hasValue())
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Button action callback is empty");
        }

        auto registration = buttonActionRegistry.stage(
            button, std::move(callback), routeDispatchDepth != 0);
        if (!registration)
        {
            return Core::failure(registration.error());
        }

        const auto rollbackRegistration = [&](Core::Error error) {
            buttonActionRegistry.rollback(*registration, routeDispatchDepth != 0);
            return Core::failure(std::move(error));
        };

        if (!contains(updaterRoot))
        {
            return rollbackRegistration(makeError(
                UIErrorCode::RootRequired,
                "UI tree updater root was released while setting a Button action"));
        }
        auto liveButtonResult = resolveButton(button);
        if (!liveButtonResult)
        {
            return rollbackRegistration(liveButtonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return rollbackRegistration(
                makeError(UIErrorCode::InvalidNode, "UI Button left the updater root while setting its action"));
        }
        if (!buttonActionRegistry.canCommit(*registration))
        {
            return rollbackRegistration(
                makeError(UIErrorCode::InvalidButtonAction, "UI Button action changed during callback transfer"));
        }
        Core::Status committed = buttonActionRegistry.commit(
            *registration, routeDispatchDepth != 0);
        if (!committed)
        {
            return rollbackRegistration(committed.error());
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status clearButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto buttonResult = resolveButton(button);
        if (!buttonResult)
        {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
        }

        buttonActionRegistry.clear(
            button, routeDispatchDepth != 0 ? buttonRouteSerial : 0, routeDispatchDepth != 0);
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId button)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto buttonResult = resolveButton(button);
        if (!buttonResult)
        {
            return Core::failure(buttonResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, button))
        {
            return fail(UIErrorCode::InvalidNode, "UI Button is not owned by the updater root");
        }
        return isButtonPressed(button);
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveCheckbox(UINodeId checkbox)
    {
        auto nodeResult = resolveNode(checkbox);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::Checkbox)
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Checkbox API requires a Checkbox node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Status setCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                            UIButtonActionCallback&& callback)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        return setButtonActionFromUpdater(updaterRoot, checkbox, std::move(callback));
    }

    [[nodiscard]] Core::Status clearCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        return clearButtonActionFromUpdater(updaterRoot, checkbox);
    }

    [[nodiscard]] Core::Status setCheckboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                           const UICheckboxPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, checkbox))
        {
            return fail(UIErrorCode::InvalidNode, "UI Checkbox is not owned by the updater root");
        }
        if (checkbox.index() >= checkboxPaintsByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Checkbox paint index out of range");
        }
        if (!std::isfinite(paint.checkedIndicatorInset) || paint.checkedIndicatorInset < 0.0F)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Checkbox paint inset must be finite and non-negative");
        }
        UICheckboxPaint& currentPaint = checkboxPaintsByNodeIndex[checkbox.index()];
        if (currentPaint == paint)
        {
            detachThemeBinding(checkbox.index(), ThemeBindingCheckboxPaint);
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(checkbox); !dirty)
        {
            return dirty;
        }
        currentPaint = paint;
        detachThemeBinding(checkbox.index(), ThemeBindingCheckboxPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UICheckboxPaint> checkboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto checkboxResult = const_cast<Impl*>(this)->resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, checkbox))
        {
            return fail(UIErrorCode::InvalidNode, "UI Checkbox is not owned by the updater root");
        }
        if (checkbox.index() >= checkboxPaintsByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Checkbox paint index out of range");
        }
        return checkboxPaintsByNodeIndex[checkbox.index()];
    }

    [[nodiscard]] Core::Status setCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox, bool checked)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, checkbox))
        {
            return fail(UIErrorCode::InvalidNode, "UI Checkbox is not owned by the updater root");
        }
        if (checkbox.index() >= checkboxCheckedByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Checkbox index out of range");
        }
        const u8 next = checked ? 1 : 0;
        if (checkboxCheckedByNodeIndex[checkbox.index()] != next)
        {
            if (Core::Status dirty = markPaintDirty(checkbox); !dirty)
            {
                return dirty;
            }
            checkboxCheckedByNodeIndex[checkbox.index()] = next;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        // const_cast: resolveCheckbox is non-const only for API reuse of resolveNode.
        auto checkboxResult = const_cast<Impl*>(this)->resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, checkbox))
        {
            return fail(UIErrorCode::InvalidNode, "UI Checkbox is not owned by the updater root");
        }
        if (checkbox.index() >= checkboxCheckedByNodeIndex.size())
        {
            return fail(Core::CoreErrorCode::Internal, "UI Checkbox index out of range");
        }
        return checkboxCheckedByNodeIndex[checkbox.index()] != 0;
    }

    [[nodiscard]] Core::Result<bool> isCheckboxPressedFromUpdater(UINodeId updaterRoot, UINodeId checkbox)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        auto checkboxResult = resolveCheckbox(checkbox);
        if (!checkboxResult)
        {
            return Core::failure(checkboxResult.error());
        }
        return isButtonPressedFromUpdater(updaterRoot, checkbox);
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveSlider(UINodeId slider)
    {
        auto nodeResult = resolveNode(slider);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::Slider)
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Slider API requires a Slider node");
        }
        return *nodeResult;
    }

    [[nodiscard]] static float quantizeSliderValue(double value, float minValue, float maxValue, float step) noexcept
    {
        const double minimum = static_cast<double>(minValue);
        const double maximum = static_cast<double>(maxValue);
        const double clamped = std::clamp(value, minimum, maximum);
        if (!(step > 0.0F) || !std::isfinite(step))
        {
            return static_cast<float>(clamped);
        }
        const double span = maximum - minimum;
        if (!(span > 0.0))
        {
            return minValue;
        }
        const double steps = std::round((clamped - minimum) / static_cast<double>(step));
        const double quantized = std::clamp(minimum + steps * static_cast<double>(step), minimum, maximum);
        return static_cast<float>(quantized);
    }

    // Map pointer X into [min,max] using last committed hit worldRect for the slider.
    [[nodiscard]] Core::Result<bool> applySliderValueFromPointer(UINodeId slider, UILogicalPoint position,
                                                                 Platform::PlatformFrameId platformFrame,
                                                                 u64 sourceSequence)
    {
        if (!slider.hasValue() || slider.index() >= sliderStatesByNodeIndex.size())
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(slider.storageId());
        if (record == nullptr || record->kind != UIWidgetKind::Slider)
        {
            return false;
        }
        SliderState& state = sliderStatesByNodeIndex[slider.index()];
        if (!(state.maxValue > state.minValue) || !std::isfinite(state.minValue) || !std::isfinite(state.maxValue))
        {
            return false;
        }

        UILogicalRect worldRect{};
        bool foundRect = false;
        const UICommittedHitView hit = committedHit();
        for (const UICommittedHitEntry& entry : hit.entries())
        {
            if (entry.node == slider)
            {
                worldRect = entry.worldRect;
                foundRect = true;
                break;
            }
        }
        if (!foundRect || !(worldRect.width > 0.0F))
        {
            return false;
        }

        const SliderTrackGeometry track = sliderTrackGeometry(worldRect, state);
        const float centerSpan = track.endCenterX - track.startCenterX;
        if (!(centerSpan > 0.0F))
        {
            return false;
        }
        const double t = std::clamp((static_cast<double>(position.x) - static_cast<double>(track.startCenterX)) /
                                        static_cast<double>(centerSpan),
                                    0.0, 1.0);
        const double raw = static_cast<double>(state.minValue) +
                           t * (static_cast<double>(state.maxValue) - static_cast<double>(state.minValue));
        const float next = quantizeSliderValue(raw, state.minValue, state.maxValue, state.step);
        if (next == state.value)
        {
            return false;
        }
        if (Core::Status dirty = markPaintDirty(slider); !dirty)
        {
            return Core::failure(dirty.error());
        }
        state.value = next;
        invokeSliderChangeCallback(captureSliderChangeCallback(slider), UISliderChangeEvent{
                                                                            .sliderNode = slider,
                                                                            .value = state.value,
                                                                            .platformFrame = platformFrame,
                                                                            .sourceSequence = sourceSequence,
                                                                        });
        return true;
    }

    [[nodiscard]] u32 textEditCodepointFromPointer(UINodeId textEdit, UILogicalPoint position) const noexcept
    {
        if (!isLiveTextEdit(textEdit))
        {
            return 0;
        }
        UILogicalRect worldRect{};
        bool foundRect = false;
        for (const UICommittedHitEntry& entry : committedHit().entries())
        {
            if (entry.node == textEdit)
            {
                worldRect = entry.worldRect;
                foundRect = true;
                break;
            }
        }
        const WidgetTextState& textState = textStatesByIndex[textEdit.index()];
        const u32 codepointCount = textState.metrics.codepointCount;
        if (!foundRect || codepointCount == 0)
        {
            return 0;
        }
        float advance = textState.style.logicalSize * textState.style.advanceScale;
        if (!(std::isfinite(advance) && advance > 0.0F))
        {
            advance = 1.0F;
        }
        const float textStartX = worldRect.x + layoutStylesByIndex[textEdit.index()].padding.left;
        const float relativeX = position.x - textStartX;
        if (!(relativeX > 0.0F))
        {
            return 0;
        }

        if (textRasterizer && textFace.hasValue())
        {
            auto raster = textRasterizer->raster(textFace, textViewFor(textEdit.index()), textState.style);
            if (raster && raster->glyphs.size() >= codepointCount)
            {
                float cursorX = 0.0F;
                for (u32 codepointIndex = 0; codepointIndex < codepointCount; ++codepointIndex)
                {
                    float glyphAdvance = raster->glyphs[codepointIndex].advance;
                    if (!(std::isfinite(glyphAdvance) && glyphAdvance > 0.0F))
                    {
                        glyphAdvance = advance;
                    }
                    const float midpoint = cursorX + glyphAdvance * 0.5F;
                    if (!std::isfinite(midpoint) || relativeX < midpoint)
                    {
                        return codepointIndex;
                    }
                    cursorX += glyphAdvance;
                    if (!std::isfinite(cursorX))
                    {
                        return codepointIndex + 1U;
                    }
                }
                return codepointCount;
            }
        }

        const float approximate = std::floor(relativeX / advance + 0.5F);
        if (!(std::isfinite(approximate) && approximate > 0.0F))
        {
            return 0;
        }
        return static_cast<u32>((std::min)(approximate, static_cast<float>(codepointCount)));
    }

    [[nodiscard]] Core::Status updateTextEditSelectionFromPointer(UINodeId textEdit, UILogicalPoint position,
                                                                  bool extendSelection)
    {
        if (!isLiveTextEdit(textEdit))
        {
            return Core::success();
        }
        TextEditState& state = textEditStatesByNodeIndex[textEdit.index()];
        UITextSelection next = state.selection;
        next.caretCodepoint = textEditCodepointFromPointer(textEdit, position);
        if (!extendSelection)
        {
            next.anchorCodepoint = next.caretCodepoint;
        }
        if (next == state.selection)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(textEdit); !dirty)
        {
            return dirty;
        }
        if (Core::Status composition = clearImeComposition(); !composition)
        {
            return composition;
        }
        state.selection = next;
        return Core::success();
    }

    [[nodiscard]] Core::Status setSliderRangeFromUpdater(UINodeId updaterRoot, UINodeId slider, float minValue,
                                                         float maxValue, float step)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        if (!std::isfinite(minValue) || !std::isfinite(maxValue) || !(maxValue > minValue) || (step < 0.0F) ||
            !std::isfinite(step))
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Slider range/step is invalid");
        }
        SliderState& state = sliderStatesByNodeIndex[slider.index()];
        const float nextValue = quantizeSliderValue(state.value, minValue, maxValue, step);
        if (state.minValue == minValue && state.maxValue == maxValue && state.step == step && state.value == nextValue)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(slider); !dirty)
        {
            return dirty;
        }
        const float previousValue = state.value;
        state.minValue = minValue;
        state.maxValue = maxValue;
        state.step = step;
        state.value = nextValue;
        if (state.value != previousValue)
        {
            invokeSliderChangeCallback(captureSliderChangeCallback(slider), UISliderChangeEvent{
                                                                                .sliderNode = slider,
                                                                                .value = state.value,
                                                                                .platformFrame = {},
                                                                                .sourceSequence = 0,
                                                                            });
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setSliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider, float value)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        if (!std::isfinite(value))
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Slider value must be finite");
        }
        SliderState& state = sliderStatesByNodeIndex[slider.index()];
        const float next = quantizeSliderValue(value, state.minValue, state.maxValue, state.step);
        if (next == state.value)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(slider); !dirty)
        {
            return dirty;
        }
        state.value = next;
        invokeSliderChangeCallback(captureSliderChangeCallback(slider), UISliderChangeEvent{
                                                                            .sliderNode = slider,
                                                                            .value = state.value,
                                                                            .platformFrame = {},
                                                                            .sourceSequence = 0,
                                                                        });
        return Core::success();
    }

    [[nodiscard]] Core::Result<float> sliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = const_cast<Impl*>(this)->resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        return sliderStatesByNodeIndex[slider.index()].value;
    }

    [[nodiscard]] Core::Status setSliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                         const UISliderPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        if (!std::isfinite(paint.contentInset) || paint.contentInset < 0.0F || !std::isfinite(paint.thumbWidth) ||
            paint.thumbWidth < 0.0F)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Slider paint metrics must be finite and non-negative");
        }
        SliderState& state = sliderStatesByNodeIndex[slider.index()];
        if (state.paint == paint)
        {
            detachThemeBinding(slider.index(), ThemeBindingSliderPaint);
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(slider); !dirty)
        {
            return dirty;
        }
        state.paint = paint;
        detachThemeBinding(slider.index(), ThemeBindingSliderPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UISliderPaint> sliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = const_cast<Impl*>(this)->resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        return sliderStatesByNodeIndex[slider.index()].paint;
    }

    [[nodiscard]] Core::Status setSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                                  UISliderChangeCallback&& callback)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        if (!callback.hasValue())
        {
            return fail(UIErrorCode::InvalidButtonAction, "UI Slider change callback is empty");
        }

        auto registration = sliderChangeCallbackRegistry.stage(
            slider, std::move(callback), routeDispatchDepth != 0);
        if (!registration)
        {
            return Core::failure(registration.error());
        }

        const auto rollbackRegistration = [&](Core::Error error) {
            sliderChangeCallbackRegistry.rollback(*registration, routeDispatchDepth != 0);
            return Core::failure(std::move(error));
        };

        if (!contains(updaterRoot))
        {
            return rollbackRegistration(makeError(
                UIErrorCode::RootRequired,
                "UI tree updater root was released while setting a Slider callback"));
        }
        auto liveSliderResult = resolveSlider(slider);
        if (!liveSliderResult)
        {
            return rollbackRegistration(liveSliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return rollbackRegistration(
                makeError(UIErrorCode::InvalidNode, "UI Slider left the updater root while setting its callback"));
        }
        if (!sliderChangeCallbackRegistry.canCommit(*registration))
        {
            return rollbackRegistration(makeError(
                UIErrorCode::InvalidButtonAction,
                "UI Slider change callback changed during callback transfer"));
        }

        sliderChangeCallbackRegistry.commit(*registration, routeDispatchDepth != 0);
        return Core::success();
    }

    [[nodiscard]] Core::Status clearSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        sliderChangeCallbackRegistry.clear(slider, routeDispatchDepth != 0);
        return Core::success();
    }

    [[nodiscard]] Core::Result<bool> isSliderDraggingFromUpdater(UINodeId updaterRoot, UINodeId slider) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto sliderResult = const_cast<Impl*>(this)->resolveSlider(slider);
        if (!sliderResult)
        {
            return Core::failure(sliderResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, slider))
        {
            return fail(UIErrorCode::InvalidNode, "UI Slider is not owned by the updater root");
        }
        return armedSlider == slider;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveScrollView(UINodeId scrollView)
    {
        auto nodeResult = resolveNode(scrollView);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::ScrollView)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ScrollView API requires a ScrollView node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Status markScrollOffsetDirty(UINodeId scrollView)
    {
        if (Core::Status dirty = markHitTestDirty(scrollView); !dirty)
        {
            return dirty;
        }
        const u32 index = scrollView.index();
        dirtyByIndex[index] |=
            UIDirty::Arrange | UIDirty::Composite | UIDirty::Paint | UIDirty::Semantics;
        phaseDirty |= PhaseLayout | PhaseHit | PhasePaint | PhaseSemantics;
        return Core::success();
    }

    [[nodiscard]] Core::Status markLayoutStyleDirty(UINodeId node)
    {
        return markLayoutDirtyBatch({node});
    }

    [[nodiscard]] bool isLiveScrollView(UINodeId scrollView) const noexcept
    {
        if (!scrollView.hasValue() || !contains(scrollView) || scrollView.index() >= scrollViewStatesByNodeIndex.size())
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(scrollView.storageId());
        return record != nullptr && record->kind == UIWidgetKind::ScrollView;
    }

    [[nodiscard]] bool isLiveListView(UINodeId listView) const noexcept
    {
        if (!listView.hasValue() || !contains(listView) || listView.index() >= listViewStatesByNodeIndex.size())
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(listView.storageId());
        return record != nullptr && record->kind == UIWidgetKind::ListView;
    }

    [[nodiscard]] bool isLiveTreeView(UINodeId treeView) const noexcept
    {
        if (!treeView.hasValue() || !contains(treeView) || treeView.index() >= treeViewStatesByNodeIndex.size())
        {
            return false;
        }
        const NodeRecord* record = nodes.tryGet(treeView.storageId());
        return record != nullptr && record->kind == UIWidgetKind::TreeView;
    }

    [[nodiscard]] bool isLiveVirtualView(UINodeId node) const noexcept
    {
        return isLiveListView(node) || isLiveTreeView(node);
    }

    [[nodiscard]] bool isLiveScrollable(UINodeId node) const noexcept
    {
        return isLiveScrollView(node) || isLiveVirtualView(node);
    }

    [[nodiscard]] ScrollBarGeometry committedScrollBarGeometry(UINodeId scrollView, UIScrollAxes axis) const noexcept
    {
        if (isLiveListView(scrollView))
        {
            if (axis != UIScrollAxes::Vertical)
            {
                return {};
            }
            const ListViewState& state = listViewStatesByNodeIndex[scrollView.index()];
            return makeListViewScrollBarGeometry(state.committedMetrics, state.committedViewportRect,
                                                 state.paint.scrollBar);
        }
        if (isLiveTreeView(scrollView))
        {
            if (axis != UIScrollAxes::Vertical)
            {
                return {};
            }
            const TreeViewState& state = treeViewStatesByNodeIndex[scrollView.index()];
            return makeTreeViewScrollBarGeometry(state.committedMetrics, state.committedViewportRect,
                                                 state.paint.scrollBar);
        }
        if (!isLiveScrollView(scrollView))
        {
            return {};
        }
        const ScrollViewState& state = scrollViewStatesByNodeIndex[scrollView.index()];
        return makeScrollBarGeometry(state.committedMetrics, state.committedViewportRect, state.paint, axis);
    }

    [[nodiscard]] static float scrollAxisOffset(UIScrollOffset offset, UIScrollAxes axis) noexcept
    {
        return axis == UIScrollAxes::Horizontal ? offset.x : offset.y;
    }

    static void setScrollAxisOffset(UIScrollOffset& offset, UIScrollAxes axis, float value) noexcept
    {
        if (axis == UIScrollAxes::Horizontal)
        {
            offset.x = value;
        } else
        {
            offset.y = value;
        }
    }

    [[nodiscard]] static float scrollAxisMaxOffset(const UIScrollViewMetrics& metrics, UIScrollAxes axis) noexcept
    {
        return axis == UIScrollAxes::Horizontal ? metrics.maxOffsetX() : metrics.maxOffsetY();
    }

    [[nodiscard]] Core::Result<bool> applyScrollOffsetFromInput(UINodeId scrollView, UIScrollOffset requested)
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
        {
            return false;
        }
        if (isLiveListView(scrollView))
        {
            ListViewState& state = listViewStatesByNodeIndex[scrollView.index()];
            requested.x = 0.0F;
            requested.y = normalizeFloat((std::clamp)(requested.y, 0.0F, state.committedMetrics.maxScrollOffset));
            if (state.requestedScrollOffset == requested.y)
            {
                return false;
            }
            if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
            {
                return Core::failure(dirty.error());
            }
            state.requestedScrollOffset = requested.y;
            return true;
        }
        if (isLiveTreeView(scrollView))
        {
            TreeViewState& state = treeViewStatesByNodeIndex[scrollView.index()];
            requested.x = 0.0F;
            requested.y = normalizeFloat((std::clamp)(requested.y, 0.0F, state.committedMetrics.maxScrollOffset));
            if (state.requestedScrollOffset == requested.y)
            {
                return false;
            }
            if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
            {
                return Core::failure(dirty.error());
            }
            state.requestedScrollOffset = requested.y;
            return true;
        }
        ScrollViewState& state = scrollViewStatesByNodeIndex[scrollView.index()];
        const UIScrollViewMetrics& metrics = state.committedMetrics;
        requested.x = hasScrollAxis(state.style.axes, UIScrollAxes::Horizontal)
                          ? normalizeFloat((std::clamp)(requested.x, 0.0F, metrics.maxOffsetX()))
                          : 0.0F;
        requested.y = hasScrollAxis(state.style.axes, UIScrollAxes::Vertical)
                          ? normalizeFloat((std::clamp)(requested.y, 0.0F, metrics.maxOffsetY()))
                          : 0.0F;
        if (state.requestedOffset == requested)
        {
            return false;
        }
        if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
        {
            return Core::failure(dirty.error());
        }
        state.requestedOffset = requested;
        return true;
    }

    [[nodiscard]] UIScrollOffset resolvedScrollWheelOffset(UINodeId scrollView, UILogicalPoint delta) const noexcept
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
        {
            return {};
        }
        if (isLiveListView(scrollView))
        {
            const ListViewState& state = listViewStatesByNodeIndex[scrollView.index()];
            const float wheelDelta = delta.y != 0.0F ? delta.y : delta.x;
            return UIScrollOffset{
                .x = 0.0F,
                .y = normalizeFloat((std::clamp)(state.requestedScrollOffset - wheelDelta * state.style.wheelStep, 0.0F,
                                                 state.committedMetrics.maxScrollOffset)),
            };
        }
        if (isLiveTreeView(scrollView))
        {
            const TreeViewState& state = treeViewStatesByNodeIndex[scrollView.index()];
            const float wheelDelta = delta.y != 0.0F ? delta.y : delta.x;
            return UIScrollOffset{
                .x = 0.0F,
                .y = normalizeFloat((std::clamp)(state.requestedScrollOffset - wheelDelta * state.style.wheelStep, 0.0F,
                                                 state.committedMetrics.maxScrollOffset)),
            };
        }
        const ScrollViewState& state = scrollViewStatesByNodeIndex[scrollView.index()];
        const bool horizontal = hasScrollAxis(state.style.axes, UIScrollAxes::Horizontal);
        const bool vertical = hasScrollAxis(state.style.axes, UIScrollAxes::Vertical);
        float horizontalDelta = delta.x;
        float verticalDelta = delta.y;
        if (horizontal && !vertical && horizontalDelta == 0.0F)
        {
            horizontalDelta = verticalDelta;
        }
        if (vertical && !horizontal && verticalDelta == 0.0F)
        {
            verticalDelta = horizontalDelta;
        }

        UIScrollOffset next = state.requestedOffset;
        if (horizontal)
        {
            next.x = normalizeFloat(next.x - horizontalDelta * state.style.wheelStep);
        }
        if (vertical)
        {
            next.y = normalizeFloat(next.y - verticalDelta * state.style.wheelStep);
        }
        next.x = horizontal ? normalizeFloat((std::clamp)(next.x, 0.0F, state.committedMetrics.maxOffsetX())) : 0.0F;
        next.y = vertical ? normalizeFloat((std::clamp)(next.y, 0.0F, state.committedMetrics.maxOffsetY())) : 0.0F;
        return next;
    }

    [[nodiscard]] bool scrollWheelWouldChange(UINodeId scrollView, UILogicalPoint delta) const noexcept
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
        {
            return false;
        }
        const UIScrollOffset resolved = resolvedScrollWheelOffset(scrollView, delta);
        if (isLiveListView(scrollView))
        {
            return listViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset != resolved.y;
        }
        if (isLiveTreeView(scrollView))
        {
            return treeViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset != resolved.y;
        }
        return scrollViewStatesByNodeIndex[scrollView.index()].requestedOffset != resolved;
    }

    [[nodiscard]] Core::Result<bool> applyScrollWheel(UINodeId scrollView, UILogicalPoint delta)
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView))
        {
            return false;
        }
        const UIScrollOffset next = resolvedScrollWheelOffset(scrollView, delta);
        return applyScrollOffsetFromInput(scrollView, next);
    }

    [[nodiscard]] Core::Result<bool> applyScrollThumbFromPointer(UINodeId scrollView, UIScrollAxes axis,
                                                                UILogicalPoint position, float grabOffset)
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView) ||
            (isLiveVirtualView(scrollView) && axis != UIScrollAxes::Vertical))
        {
            return false;
        }
        const ScrollBarGeometry geometry = committedScrollBarGeometry(scrollView, axis);
        const float maxOffset =
            isLiveListView(scrollView) ? listViewStatesByNodeIndex[scrollView.index()].committedMetrics.maxScrollOffset
            : isLiveTreeView(scrollView)
                ? treeViewStatesByNodeIndex[scrollView.index()].committedMetrics.maxScrollOffset
                : scrollAxisMaxOffset(scrollViewStatesByNodeIndex[scrollView.index()].committedMetrics, axis);
        const float trackStart = axis == UIScrollAxes::Horizontal ? geometry.track.x : geometry.track.y;
        const float trackExtent = axis == UIScrollAxes::Horizontal ? geometry.track.width : geometry.track.height;
        const float thumbExtent = axis == UIScrollAxes::Horizontal ? geometry.thumb.width : geometry.thumb.height;
        const float pointer = axis == UIScrollAxes::Horizontal ? position.x : position.y;
        const float travel = (std::max)(0.0F, trackExtent - thumbExtent);
        if (!geometry.visible || !(maxOffset > 0.0F) || !(travel > 0.0F))
        {
            return false;
        }

        const float thumbStart = (std::clamp)(pointer - grabOffset - trackStart, 0.0F, travel);
        UIScrollOffset next =
            isLiveListView(scrollView)
                ? UIScrollOffset{.x = 0.0F, .y = listViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset}
            : isLiveTreeView(scrollView)
                ? UIScrollOffset{.x = 0.0F, .y = treeViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset}
                                  : scrollViewStatesByNodeIndex[scrollView.index()].requestedOffset;
        setScrollAxisOffset(next, axis, normalizeFloat(maxOffset * (thumbStart / travel)));
        return applyScrollOffsetFromInput(scrollView, next);
    }

    [[nodiscard]] Core::Result<bool> applyScrollTrackPage(UINodeId scrollView, UIScrollAxes axis,
                                                         UILogicalPoint position)
    {
        if (!isLiveScrollable(scrollView) || !isNodeEnabled(scrollView) ||
            (isLiveVirtualView(scrollView) && axis != UIScrollAxes::Vertical))
        {
            return false;
        }
        const ScrollBarGeometry geometry = committedScrollBarGeometry(scrollView, axis);
        if (!geometry.visible)
        {
            return false;
        }
        const float pointer = axis == UIScrollAxes::Horizontal ? position.x : position.y;
        const float thumbStart = axis == UIScrollAxes::Horizontal ? geometry.thumb.x : geometry.thumb.y;
        const float pageExtent = isLiveListView(scrollView)
                                     ? listViewStatesByNodeIndex[scrollView.index()].committedMetrics.viewportSize.height
            : isLiveTreeView(scrollView)
                ? treeViewStatesByNodeIndex[scrollView.index()].committedMetrics.viewportSize.height
                                     : (axis == UIScrollAxes::Horizontal
                                            ? scrollViewStatesByNodeIndex[scrollView.index()]
                                                  .committedMetrics.viewportSize.width
                                            : scrollViewStatesByNodeIndex[scrollView.index()]
                                                  .committedMetrics.viewportSize.height);
        const float direction = pointer < thumbStart ? -1.0F : 1.0F;
        UIScrollOffset next =
            isLiveListView(scrollView)
                ? UIScrollOffset{.x = 0.0F, .y = listViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset}
            : isLiveTreeView(scrollView)
                ? UIScrollOffset{.x = 0.0F, .y = treeViewStatesByNodeIndex[scrollView.index()].requestedScrollOffset}
                                  : scrollViewStatesByNodeIndex[scrollView.index()].requestedOffset;
        setScrollAxisOffset(next, axis,
                            normalizeFloat(scrollAxisOffset(next, axis) + direction * pageExtent));
        return applyScrollOffsetFromInput(scrollView, next);
    }

    [[nodiscard]] ScrollBarPointerHit scrollBarPointerHit(std::span<const u32> routePath,
                                                         std::span<const UICommittedHitEntry> entries,
                                                         UILogicalPoint position) const noexcept
    {
        for (const u32 routeEntryIndex : routePath)
        {
            if (routeEntryIndex >= entries.size())
            {
                continue;
            }
            const UINodeId node = entries[routeEntryIndex].node;
            if (!isLiveScrollable(node) || !isNodeEnabled(node))
            {
                continue;
            }
            if (isLiveVirtualView(node))
            {
                const float maxOffset = isLiveListView(node)
                                            ? listViewStatesByNodeIndex[node.index()].committedMetrics.maxScrollOffset
                                            : treeViewStatesByNodeIndex[node.index()].committedMetrics.maxScrollOffset;
                if (!(maxOffset > 0.0F))
                {
                    continue;
                }
                const ScrollBarGeometry geometry = committedScrollBarGeometry(node, UIScrollAxes::Vertical);
                if (geometry.visible && containsPointHalfOpen(geometry.track, position))
                {
                    return ScrollBarPointerHit{
                        .scrollView = node,
                        .axis = UIScrollAxes::Vertical,
                        .geometry = geometry,
                        .thumb = containsPointHalfOpen(geometry.thumb, position),
                    };
                }
                continue;
            }
            const ScrollViewState& state = scrollViewStatesByNodeIndex[node.index()];
            for (const UIScrollAxes axis : {UIScrollAxes::Horizontal, UIScrollAxes::Vertical})
            {
                if (!hasScrollAxis(state.style.axes, axis) ||
                    !(scrollAxisMaxOffset(state.committedMetrics, axis) > 0.0F))
                {
                    continue;
                }
                const ScrollBarGeometry geometry = committedScrollBarGeometry(node, axis);
                if (!geometry.visible || !containsPointHalfOpen(geometry.track, position))
                {
                    continue;
                }
                return ScrollBarPointerHit{
                    .scrollView = node,
                    .axis = axis,
                    .geometry = geometry,
                    .thumb = containsPointHalfOpen(geometry.thumb, position),
                };
            }
        }
        return {};
    }

    [[nodiscard]] Core::Status setScrollViewStyleFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                            const UIScrollViewStyle& style)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        auto normalized = normalizeScrollViewStyle(style);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        ScrollViewState& state = scrollViewStatesByNodeIndex[scrollView.index()];
        if (state.style == *normalized)
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(scrollView); !dirty)
        {
            return dirty;
        }
        state.style = *normalized;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIScrollViewStyle> scrollViewStyleFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId scrollView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = const_cast<Impl*>(this)->resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        return scrollViewStatesByNodeIndex[scrollView.index()].style;
    }

    [[nodiscard]] Core::Status setScrollViewOffsetFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                             UIScrollOffset offset)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        auto normalized = normalizeScrollOffset(offset);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        ScrollViewState& state = scrollViewStatesByNodeIndex[scrollView.index()];
        if (state.requestedOffset == *normalized)
        {
            return Core::success();
        }
        if (Core::Status dirty = markScrollOffsetDirty(scrollView); !dirty)
        {
            return dirty;
        }
        state.requestedOffset = *normalized;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIScrollOffset> scrollViewOffsetFromUpdater(UINodeId updaterRoot,
                                                                           UINodeId scrollView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = const_cast<Impl*>(this)->resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        return scrollViewStatesByNodeIndex[scrollView.index()].requestedOffset;
    }

    [[nodiscard]] Core::Result<UIScrollViewMetrics> scrollViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                                 UINodeId scrollView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = const_cast<Impl*>(this)->resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        return scrollViewStatesByNodeIndex[scrollView.index()].committedMetrics;
    }

    [[nodiscard]] Core::Status setScrollViewPaintFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                            const UIScrollViewPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        auto normalized = normalizeScrollViewPaint(paint);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        ScrollViewState& state = scrollViewStatesByNodeIndex[scrollView.index()];
        if (state.paint == *normalized)
        {
            detachThemeBinding(scrollView.index(), ThemeBindingScrollViewPaint);
            return Core::success();
        }
        const bool layoutChanged = state.paint.thickness != normalized->thickness ||
                                   state.paint.minThumbExtent != normalized->minThumbExtent;
        Core::Status dirty = layoutChanged ? markLayoutStyleDirty(scrollView) : markPaintDirty(scrollView);
        if (!dirty)
        {
            return dirty;
        }
        state.paint = *normalized;
        detachThemeBinding(scrollView.index(), ThemeBindingScrollViewPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIScrollViewPaint> scrollViewPaintFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId scrollView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = const_cast<Impl*>(this)->resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        return scrollViewStatesByNodeIndex[scrollView.index()].paint;
    }

    [[nodiscard]] Core::Result<bool> isScrollViewDraggingFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId scrollView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto scrollResult = const_cast<Impl*>(this)->resolveScrollView(scrollView);
        if (!scrollResult)
        {
            return Core::failure(scrollResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, scrollView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ScrollView is not owned by the updater root");
        }
        return scrollThumbDragActive && armedScrollView == scrollView;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolvePopup(UINodeId popup)
    {
        auto nodeResult = resolveNode(popup);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::Popup || popup.index() >= popupStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Popup API requires a Popup node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveDropdown(UINodeId dropdown)
    {
        auto nodeResult = resolveNode(dropdown);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::Dropdown || dropdown.index() >= dropdownStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Dropdown API requires a Dropdown node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveDropdownItem(UINodeId item)
    {
        auto nodeResult = resolveNode(item);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::DropdownItem)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Dropdown item API requires a DropdownItem node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveListView(UINodeId listView)
    {
        auto nodeResult = resolveNode(listView);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::ListView || listView.index() >= listViewStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ListView API requires a ListView node");
        }
        return *nodeResult;
    }

    [[nodiscard]] UINodeId listViewForItem(UINodeId item) const noexcept
    {
        if (!contains(item))
        {
            return {};
        }
        const NodeRecord* itemRecord = nodes.tryGet(item.storageId());
        if (itemRecord == nullptr || itemRecord->kind != UIWidgetKind::ListViewItem ||
            itemRecord->parentIndex == InvalidNodeIndex)
        {
            return {};
        }
        const UINodeId parent = idForIndex(itemRecord->parentIndex);
        const NodeRecord* parentRecord = contains(parent) ? nodes.tryGet(parent.storageId()) : nullptr;
        return parentRecord != nullptr && parentRecord->kind == UIWidgetKind::ListView ? parent : UINodeId{};
    }

    [[nodiscard]] bool isSelectedListViewItem(UINodeId item) const noexcept
    {
        const UINodeId listView = listViewForItem(item);
        if (!listView.hasValue() || item.index() >= listViewItemStatesByNodeIndex.size())
        {
            return false;
        }
        const ListViewItemState& itemState = listViewItemStatesByNodeIndex[item.index()];
        const ListViewState& listState = listViewStatesByNodeIndex[listView.index()];
        return itemState.bound && listState.selection.hasValue() && itemState.key == listState.selection.key;
    }

    [[nodiscard]] Core::Status selectCommittedListViewItem(UINodeId item)
    {
        const UINodeId listView = listViewForItem(item);
        if (!listView.hasValue() || item.index() >= listViewItemStatesByNodeIndex.size() ||
            listView.index() >= listViewStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ListView selection requires a bound item row");
        }
        const ListViewItemState& itemState = listViewItemStatesByNodeIndex[item.index()];
        if (!itemState.committedBound || !itemState.committedEnabled)
        {
            return Core::success();
        }
        ListViewState& listState = listViewStatesByNodeIndex[listView.index()];
        const UIListViewSelection selection{
            .key = itemState.committedKey,
            .logicalIndex = itemState.committedLogicalIndex,
        };
        if (selection == listState.selection)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(listView); !dirty)
        {
            return dirty;
        }
        listState.selection = selection;
        return Core::success();
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveTreeView(UINodeId treeView)
    {
        auto nodeResult = resolveNode(treeView);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::TreeView || treeView.index() >= treeViewStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView API requires a TreeView node");
        }
        return *nodeResult;
    }

    [[nodiscard]] UINodeId treeViewForItem(UINodeId item) const noexcept
    {
        if (!contains(item))
        {
            return {};
        }
        const NodeRecord* itemRecord = nodes.tryGet(item.storageId());
        if (itemRecord == nullptr || itemRecord->kind != UIWidgetKind::TreeViewItem ||
            itemRecord->parentIndex == InvalidNodeIndex)
        {
            return {};
        }
        const UINodeId parent = idForIndex(itemRecord->parentIndex);
        const NodeRecord* parentRecord = contains(parent) ? nodes.tryGet(parent.storageId()) : nullptr;
        return parentRecord != nullptr && parentRecord->kind == UIWidgetKind::TreeView ? parent : UINodeId{};
    }

    [[nodiscard]] bool pointWithinCommittedTreeDisclosure(UINodeId item, UILogicalPoint point,
                                                          std::span<const UICommittedHitEntry> entries) const noexcept
    {
        const UINodeId treeView = treeViewForItem(item);
        if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size() ||
            treeView.index() >= treeViewStatesByNodeIndex.size())
        {
            return false;
        }
        const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
        if (!itemState.committedBound || !itemState.committedEnabled || !itemState.committedExpandable)
        {
            return false;
        }
        const u32 entryIndex = findHitEntryIndex(item, entries);
        if (entryIndex >= entries.size())
        {
            return false;
        }
        const UICommittedHitEntry& entry = entries[entryIndex];
        const UILogicalRect disclosure = makeTreeViewDisclosureRect(
            entry.worldRect, treeViewStatesByNodeIndex[treeView.index()].style, itemState.committedLevel);
        return containsPointHalfOpen(entry.worldRect, point) && containsPointHalfOpen(disclosure, point) &&
               containsPointHalfOpen(entry.effectiveClip, point);
    }

    [[nodiscard]] bool isSelectedTreeViewItem(UINodeId item) const noexcept
    {
        const UINodeId treeView = treeViewForItem(item);
        if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size())
        {
            return false;
        }
        const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
        const TreeViewState& treeState = treeViewStatesByNodeIndex[treeView.index()];
        return itemState.bound && treeState.selection.hasValue() && itemState.key == treeState.selection.key;
    }

    [[nodiscard]] Core::Status selectCommittedTreeViewItem(UINodeId item)
    {
        const UINodeId treeView = treeViewForItem(item);
        if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size() ||
            treeView.index() >= treeViewStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView selection requires a bound item row");
        }
        const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
        if (!itemState.committedBound || !itemState.committedEnabled)
        {
            return Core::success();
        }
        TreeViewState& treeState = treeViewStatesByNodeIndex[treeView.index()];
        const UITreeViewSelection selection{
            .key = itemState.committedKey,
            .logicalIndex = itemState.committedLogicalIndex,
            .level = itemState.committedLevel,
        };
        if (selection == treeState.selection)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(treeView); !dirty)
        {
            return dirty;
        }
        treeState.selection = selection;
        return Core::success();
    }

    [[nodiscard]] Core::Status toggleCommittedTreeViewItem(UINodeId item)
    {
        const UINodeId treeView = treeViewForItem(item);
        if (!treeView.hasValue() || item.index() >= treeViewItemStatesByNodeIndex.size() ||
            treeView.index() >= treeViewStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView expansion requires a bound item row");
        }
        const TreeViewItemState& itemState = treeViewItemStatesByNodeIndex[item.index()];
        if (!itemState.committedBound || !itemState.committedEnabled || !itemState.committedExpandable)
        {
            return Core::success();
        }
        TreeViewState& treeState = treeViewStatesByNodeIndex[treeView.index()];
        if (!treeState.dataSource.canSetItemExpanded())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView data source does not support expansion");
        }
        if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
        {
            return dirty;
        }
        if (!treeState.dataSource.setItemExpanded(treeState.dataSource.state, itemState.committedKey,
                                                  !itemState.committedExpanded))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView data source rejected expansion");
        }
        if (treeState.selection.key == itemState.committedKey)
        {
            treeState.selection.logicalIndex = itemState.committedLogicalIndex;
            treeState.selection.level = itemState.committedLevel;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setPopupOpenState(UINodeId popup, bool open)
    {
        PopupState& popupState = popupStatesByNodeIndex[popup.index()];
        if (popupState.open == open && (!open || activePopupNode == popup))
        {
            return Core::success();
        }

        const UINodeId dropdown = dropdownForPopup(popup);
        if (!dropdown.hasValue())
        {
            return fail(UIErrorCode::InvalidParent, "UI Popup is detached from its Dropdown anchor");
        }
        if (open && (!isNodeEnabled(dropdown) || !isNodeEnabled(popup)))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI Dropdown and Popup must be enabled before the Popup can open");
        }

        UINodeId previousPopup{};
        UINodeId previousDropdown{};
        if (open && activePopupNode.hasValue() && activePopupNode != popup && contains(activePopupNode) &&
            activePopupNode.index() < popupStatesByNodeIndex.size() &&
            popupStatesByNodeIndex[activePopupNode.index()].open)
        {
            previousPopup = activePopupNode;
            previousDropdown = dropdownForPopup(previousPopup);
        }
        if (Core::Status dirty = markLayoutDirtyBatch({popup, dropdown, previousPopup, previousDropdown}); !dirty)
        {
            return dirty;
        }

        if (previousPopup.hasValue())
        {
            popupStatesByNodeIndex[previousPopup.index()].open = false;
        }
        const bool focusWasInClosingPopup =
            (!open && isNodeWithinSubtree(popup, defaultActionFocusButton)) ||
            (previousPopup.hasValue() && isNodeWithinSubtree(previousPopup, defaultActionFocusButton));
        if (open)
        {
            focusRestoreByNodeIndex[popup.index()] = defaultActionFocusButton;
            popupState.open = true;
            activePopupNode = popup;
        } else
        {
            popupState.open = false;
            if (activePopupNode == popup)
            {
                activePopupNode = {};
            }
        }
        popupDismissPointerBarrierActive = false;
        dropdownCommandPressLatch.clear();

        if (focusWasInClosingPopup)
        {
            UINodeId nextFocus = open ? dropdown : focusRestoreByNodeIndex[popup.index()];
            if (!nextFocus.hasValue() || !contains(nextFocus) || !isNodeEnabled(nextFocus))
            {
                nextFocus = isNodeEnabled(dropdown) ? dropdown : UINodeId{};
            }
            defaultActionPressState.clearAll();
            clearArmedPrimaryButton();
            clearArmedSlider();
            clearArmedTextEdit();
            capturedPointerNode = {};
            resetImeCompositionState();
            textInputFocus = {};
            defaultActionFocusButton = nextFocus;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status setPopupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup,
                                                       const UIPopupStyle& style)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto popupResult = resolvePopup(popup);
        if (!popupResult)
        {
            return Core::failure(popupResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, popup))
        {
            return fail(UIErrorCode::InvalidNode, "UI Popup is not owned by the updater root");
        }
        auto normalized = normalizePopupStyle(style);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        PopupState& state = popupStatesByNodeIndex[popup.index()];
        if (state.style == *normalized)
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(popup); !dirty)
        {
            return dirty;
        }
        state.style = *normalized;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIPopupStyle> popupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto popupResult = const_cast<Impl*>(this)->resolvePopup(popup);
        if (!popupResult)
        {
            return Core::failure(popupResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, popup))
        {
            return fail(UIErrorCode::InvalidNode, "UI Popup is not owned by the updater root");
        }
        return popupStatesByNodeIndex[popup.index()].style;
    }

    [[nodiscard]] Core::Status setPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup, bool open)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto popupResult = resolvePopup(popup);
        if (!popupResult)
        {
            return Core::failure(popupResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, popup))
        {
            return fail(UIErrorCode::InvalidNode, "UI Popup is not owned by the updater root");
        }
        return setPopupOpenState(popup, open);
    }

    [[nodiscard]] Core::Result<bool> isPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup) const
    {
        auto styleResult = popupStyleFromUpdater(updaterRoot, popup);
        if (!styleResult)
        {
            return Core::failure(styleResult.error());
        }
        return popupStatesByNodeIndex[popup.index()].open;
    }

    [[nodiscard]] Core::Result<UIPopupMetrics> popupMetricsFromUpdater(UINodeId updaterRoot, UINodeId popup) const
    {
        auto styleResult = popupStyleFromUpdater(updaterRoot, popup);
        if (!styleResult)
        {
            return Core::failure(styleResult.error());
        }
        return popupStatesByNodeIndex[popup.index()].committedMetrics;
    }

    [[nodiscard]] Core::Status setDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown, bool open)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto dropdownResult = resolveDropdown(dropdown);
        if (!dropdownResult)
        {
            return Core::failure(dropdownResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, dropdown))
        {
            return fail(UIErrorCode::InvalidNode, "UI Dropdown is not owned by the updater root");
        }
        const UINodeId popup = popupForDropdown(dropdown);
        if (!popup.hasValue())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Dropdown has no live Popup");
        }
        return setPopupOpenState(popup, open);
    }

    [[nodiscard]] Core::Result<bool> isDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto dropdownResult = const_cast<Impl*>(this)->resolveDropdown(dropdown);
        if (!dropdownResult)
        {
            return Core::failure(dropdownResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, dropdown))
        {
            return fail(UIErrorCode::InvalidNode, "UI Dropdown is not owned by the updater root");
        }
        const UINodeId popup = popupForDropdown(dropdown);
        return popup.hasValue() && popupStatesByNodeIndex[popup.index()].open;
    }

    [[nodiscard]] Core::Status setDropdownSelectedItemFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                                 UINodeId item)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto dropdownResult = resolveDropdown(dropdown);
        if (!dropdownResult)
        {
            return Core::failure(dropdownResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, dropdown))
        {
            return fail(UIErrorCode::InvalidNode, "UI Dropdown is not owned by the updater root");
        }
        if (item.hasValue())
        {
            auto itemResult = resolveDropdownItem(item);
            if (!itemResult)
            {
                return Core::failure(itemResult.error());
            }
            if (!isNodeWithinRoot(updaterRoot, item) || dropdownForItem(item) != dropdown)
            {
                return fail(UIErrorCode::InvalidParent, "UI DropdownItem belongs to another Dropdown");
            }
        }

        DropdownState& state = dropdownStatesByNodeIndex[dropdown.index()];
        if (state.selectedItem == item)
        {
            return Core::success();
        }
        const UINodeId previousItem = state.selectedItem;
        if (Core::Status dirty = markLayoutDirtyBatch({dropdown, previousItem, item}); !dirty)
        {
            return dirty;
        }
        state.selectedItem = item;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UINodeId> dropdownSelectedItemFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId dropdown) const
    {
        auto openResult = isDropdownOpenFromUpdater(updaterRoot, dropdown);
        if (!openResult)
        {
            return Core::failure(openResult.error());
        }
        const UINodeId selected = dropdownStatesByNodeIndex[dropdown.index()].selectedItem;
        return contains(selected) ? selected : UINodeId{};
    }

    [[nodiscard]] Core::Result<bool> isDropdownItemSelectedFromUpdater(UINodeId updaterRoot,
                                                                       UINodeId item) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto itemResult = const_cast<Impl*>(this)->resolveDropdownItem(item);
        if (!itemResult)
        {
            return Core::failure(itemResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, item))
        {
            return fail(UIErrorCode::InvalidNode, "UI DropdownItem is not owned by the updater root");
        }
        return isSelectedDropdownItem(item);
    }

    [[nodiscard]] Core::Status setDropdownPaintFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                          const UIDropdownPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto dropdownResult = resolveDropdown(dropdown);
        if (!dropdownResult)
        {
            return Core::failure(dropdownResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, dropdown))
        {
            return fail(UIErrorCode::InvalidNode, "UI Dropdown is not owned by the updater root");
        }
        auto normalized = normalizeDropdownPaint(paint);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        UIDropdownPaint& current = dropdownStatesByNodeIndex[dropdown.index()].paint;
        if (current == *normalized)
        {
            detachThemeBinding(dropdown.index(), ThemeBindingDropdownPaint);
            return Core::success();
        }
        const bool layoutChanged = current.indicatorWidth != normalized->indicatorWidth ||
                                   current.indicatorHeight != normalized->indicatorHeight ||
                                   current.indicatorInset != normalized->indicatorInset;
        Core::Status dirty = layoutChanged ? markLayoutStyleDirty(dropdown) : markPaintDirty(dropdown);
        if (!dirty)
        {
            return dirty;
        }
        current = *normalized;
        detachThemeBinding(dropdown.index(), ThemeBindingDropdownPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIDropdownPaint> dropdownPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId dropdown) const
    {
        auto openResult = isDropdownOpenFromUpdater(updaterRoot, dropdown);
        if (!openResult)
        {
            return Core::failure(openResult.error());
        }
        return dropdownStatesByNodeIndex[dropdown.index()].paint;
    }

    [[nodiscard]] Core::Status validateListViewUpdater(UINodeId updaterRoot, UINodeId listView) const
    {
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto listResult = const_cast<Impl*>(this)->resolveListView(listView);
        if (!listResult)
        {
            return Core::failure(listResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, listView))
        {
            return fail(UIErrorCode::InvalidNode, "UI ListView is not owned by the updater root");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIListViewItemDescriptor> resolveListViewLogicalItem(UINodeId listView,
                                                                                   u64 logicalIndex) const
    {
        const ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        if (!state.dataSource.hasValue())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ListView has no data source");
        }
        const u64 itemCount = state.dataSource.itemCount(state.dataSource.state);
        if (logicalIndex >= itemCount)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ListView logical item index is out of range");
        }
        UIListViewItemDescriptor descriptor{};
        if (!state.dataSource.resolveItem(state.dataSource.state, logicalIndex, descriptor))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ListView data source failed to resolve an item");
        }
        if (descriptor.key == InvalidUIListViewItemKey || containsLineBreak(descriptor.label))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI ListView item requires a non-zero key and a single-line label");
        }
        return descriptor;
    }

    [[nodiscard]] Core::Status setListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                               UIListViewDataSource source)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return valid;
        }
        if (!source.hasValue())
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI ListView data source requires state, itemCount, and resolveItem");
        }
        if (Core::Status dirty = markLayoutStyleDirty(listView); !dirty)
        {
            return dirty;
        }
        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        state.dataSource = source;
        state.selection = {};
        state.requestedScrollOffset = 0.0F;
        return Core::success();
    }

    [[nodiscard]] Core::Status clearListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return valid;
        }
        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        if (!state.dataSource.hasValue())
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(listView); !dirty)
        {
            return dirty;
        }
        state.dataSource = {};
        state.selection = {};
        state.requestedScrollOffset = 0.0F;
        return Core::success();
    }

    [[nodiscard]] Core::Status invalidateListViewItemsFromUpdater(UINodeId updaterRoot, UINodeId listView)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return valid;
        }
        return markLayoutStyleDirty(listView);
    }

    [[nodiscard]] Core::Status setListViewStyleFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                          const UIListViewStyle& style)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return valid;
        }
        auto normalized = normalizeListViewStyle(style);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        if (state.style == *normalized)
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(listView); !dirty)
        {
            return dirty;
        }
        state.style = *normalized;
        const NodeRecord* listRecord = nodes.tryGet(listView.storageId());
        u32 child = listRecord == nullptr ? InvalidNodeIndex : listRecord->firstChildIndex;
        while (child != InvalidNodeIndex)
        {
            NodeRecord* childRecord = recordByIndex(child);
            if (childRecord == nullptr)
            {
                break;
            }
            layoutStylesByIndex[child].size.height = UILayoutLength::Px(state.style.rowHeight);
            child = childRecord->nextSiblingIndex;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIListViewStyle> listViewStyleFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId listView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return Core::failure(valid.error());
        }
        return listViewStatesByNodeIndex[listView.index()].style;
    }

    [[nodiscard]] Core::Status setListViewPaintFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                          const UIListViewPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return valid;
        }
        auto normalized = normalizeListViewPaint(paint);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        if (state.paint == *normalized)
        {
            detachThemeBinding(listView.index(), ThemeBindingListViewPaint);
            return Core::success();
        }
        const bool layoutChanged = state.paint.scrollBar.thickness != normalized->scrollBar.thickness ||
                                   state.paint.scrollBar.minThumbExtent != normalized->scrollBar.minThumbExtent;
        Core::Status dirty = layoutChanged ? markLayoutStyleDirty(listView) : markPaintDirty(listView);
        if (!dirty)
        {
            return dirty;
        }
        state.paint = *normalized;
        detachThemeBinding(listView.index(), ThemeBindingListViewPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIListViewPaint> listViewPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId listView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return Core::failure(valid.error());
        }
        return listViewStatesByNodeIndex[listView.index()].paint;
    }

    [[nodiscard]] Core::Result<UIListViewMetrics> listViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                            UINodeId listView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return Core::failure(valid.error());
        }
        return listViewStatesByNodeIndex[listView.index()].committedMetrics;
    }

    [[nodiscard]] Core::Status setListViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                                  u64 logicalIndex)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return valid;
        }
        auto descriptor = resolveListViewLogicalItem(listView, logicalIndex);
        if (!descriptor)
        {
            return Core::failure(descriptor.error());
        }
        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        const UIListViewSelection next{.key = descriptor->key, .logicalIndex = logicalIndex};
        if (state.selection == next)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(listView); !dirty)
        {
            return dirty;
        }
        state.selection = next;
        return Core::success();
    }

    [[nodiscard]] Core::Status clearListViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId listView)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return valid;
        }
        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        if (!state.selection.hasValue())
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(listView); !dirty)
        {
            return dirty;
        }
        state.selection = {};
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIListViewSelection> listViewSelectionFromUpdater(UINodeId updaterRoot,
                                                                                UINodeId listView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return Core::failure(valid.error());
        }
        return listViewStatesByNodeIndex[listView.index()].selection;
    }

    [[nodiscard]] Core::Status scrollListViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                               u64 logicalIndex,
                                                               UIListViewScrollAlignment alignment)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateListViewUpdater(updaterRoot, listView); !valid)
        {
            return valid;
        }
        if (!isValidListViewScrollAlignment(alignment))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ListView scroll alignment is not recognized");
        }
        auto descriptor = resolveListViewLogicalItem(listView, logicalIndex);
        if (!descriptor)
        {
            return Core::failure(descriptor.error());
        }
        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        const float rowStart = static_cast<float>(static_cast<double>(logicalIndex) * state.style.rowHeight);
        const float rowEnd = rowStart + state.style.rowHeight;
        const float viewportHeight = state.committedMetrics.viewportSize.height;
        const u64 logicalItemCount = state.dataSource.itemCount(state.dataSource.state);
        const double contentHeight64 = static_cast<double>(logicalItemCount) * state.style.rowHeight;
        if (!std::isfinite(contentHeight64) || contentHeight64 > (std::numeric_limits<float>::max)())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ListView logical content height is not representable");
        }
        const float maximumOffset =
            normalizeFloat((std::max)(0.0F, static_cast<float>(contentHeight64) - viewportHeight));
        float nextOffset = state.requestedScrollOffset;
        switch (alignment)
        {
        case UIListViewScrollAlignment::Start:
            nextOffset = rowStart;
            break;
        case UIListViewScrollAlignment::Center:
            nextOffset = rowStart - (viewportHeight - state.style.rowHeight) * 0.5F;
            break;
        case UIListViewScrollAlignment::End:
            nextOffset = rowEnd - viewportHeight;
            break;
        case UIListViewScrollAlignment::Nearest:
            if (rowStart < nextOffset)
            {
                nextOffset = rowStart;
            } else if (rowEnd > nextOffset + viewportHeight)
            {
                nextOffset = rowEnd - viewportHeight;
            }
            break;
        }
        nextOffset = normalizeFloat((std::clamp)(nextOffset, 0.0F, maximumOffset));
        if (nextOffset == state.requestedScrollOffset)
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(listView); !dirty)
        {
            return dirty;
        }
        state.requestedScrollOffset = nextOffset;
        return Core::success();
    }

    [[nodiscard]] Core::Status validateTreeViewUpdater(UINodeId updaterRoot, UINodeId treeView) const
    {
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto treeResult = const_cast<Impl*>(this)->resolveTreeView(treeView);
        if (!treeResult)
        {
            return Core::failure(treeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, treeView))
        {
            return fail(UIErrorCode::InvalidNode, "UI TreeView is not owned by the updater root");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITreeViewItemDescriptor> resolveTreeViewLogicalItem(UINodeId treeView,
                                                                                    u64 logicalIndex) const
    {
        const TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        if (!state.dataSource.hasValue())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView has no data source");
        }
        const u64 itemCount = state.dataSource.itemCount(state.dataSource.state);
        if (logicalIndex >= itemCount)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView logical item index is out of range");
        }
        UITreeViewItemDescriptor descriptor{};
        if (!state.dataSource.resolveItem(state.dataSource.state, logicalIndex, descriptor))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView data source failed to resolve an item");
        }
        if (descriptor.key == InvalidUITreeViewItemKey || containsLineBreak(descriptor.label) ||
            (descriptor.expanded && !descriptor.expandable))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI TreeView item requires a non-zero key, a single-line label, and valid expansion state");
        }
        return descriptor;
    }

    [[nodiscard]] Core::Status setTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                UITreeViewDataSource source)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return valid;
        }
        if (!source.hasValue())
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI TreeView data source requires state, itemCount, and resolveItem");
        }
        if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
        {
            return dirty;
        }
        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        state.dataSource = source;
        state.selection = {};
        state.requestedScrollOffset = 0.0F;
        return Core::success();
    }

    [[nodiscard]] Core::Status clearTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return valid;
        }
        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        if (!state.dataSource.hasValue())
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
        {
            return dirty;
        }
        state.dataSource = {};
        state.selection = {};
        state.requestedScrollOffset = 0.0F;
        return Core::success();
    }

    [[nodiscard]] Core::Status invalidateTreeViewItemsFromUpdater(UINodeId updaterRoot, UINodeId treeView)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return valid;
        }
        return markLayoutStyleDirty(treeView);
    }

    [[nodiscard]] Core::Status setTreeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                           const UITreeViewStyle& style)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return valid;
        }
        auto normalized = normalizeTreeViewStyle(style);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        if (state.style == *normalized)
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
        {
            return dirty;
        }
        state.style = *normalized;
        const NodeRecord* treeRecord = nodes.tryGet(treeView.storageId());
        u32 child = treeRecord == nullptr ? InvalidNodeIndex : treeRecord->firstChildIndex;
        while (child != InvalidNodeIndex)
        {
            NodeRecord* childRecord = recordByIndex(child);
            if (childRecord == nullptr)
            {
                break;
            }
            layoutStylesByIndex[child].size.height = UILayoutLength::Px(state.style.rowHeight);
            child = childRecord->nextSiblingIndex;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITreeViewStyle> treeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return Core::failure(valid.error());
        }
        return treeViewStatesByNodeIndex[treeView.index()].style;
    }

    [[nodiscard]] Core::Status setTreeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                           const UITreeViewPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return valid;
        }
        auto normalized = normalizeTreeViewPaint(paint);
        if (!normalized)
        {
            return Core::failure(normalized.error());
        }
        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        if (state.paint == *normalized)
        {
            detachThemeBinding(treeView.index(), ThemeBindingTreeViewPaint);
            return Core::success();
        }
        const bool layoutChanged = state.paint.scrollBar.thickness != normalized->scrollBar.thickness ||
                                   state.paint.scrollBar.minThumbExtent != normalized->scrollBar.minThumbExtent;
        Core::Status dirty = layoutChanged ? markLayoutStyleDirty(treeView) : markPaintDirty(treeView);
        if (!dirty)
        {
            return dirty;
        }
        state.paint = *normalized;
        detachThemeBinding(treeView.index(), ThemeBindingTreeViewPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITreeViewPaint> treeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return Core::failure(valid.error());
        }
        return treeViewStatesByNodeIndex[treeView.index()].paint;
    }

    [[nodiscard]] Core::Result<UITreeViewMetrics> treeViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                             UINodeId treeView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return Core::failure(valid.error());
        }
        return treeViewStatesByNodeIndex[treeView.index()].committedMetrics;
    }

    [[nodiscard]] Core::Status setTreeViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                   u64 logicalIndex)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return valid;
        }
        auto descriptor = resolveTreeViewLogicalItem(treeView, logicalIndex);
        if (!descriptor)
        {
            return Core::failure(descriptor.error());
        }
        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        const UITreeViewSelection next{
            .key = descriptor->key,
            .logicalIndex = logicalIndex,
            .level = descriptor->level,
        };
        if (state.selection == next)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(treeView); !dirty)
        {
            return dirty;
        }
        state.selection = next;
        return Core::success();
    }

    [[nodiscard]] Core::Status clearTreeViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId treeView)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return valid;
        }
        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        if (!state.selection.hasValue())
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(treeView); !dirty)
        {
            return dirty;
        }
        state.selection = {};
        return Core::success();
    }

    [[nodiscard]] Core::Result<UITreeViewSelection> treeViewSelectionFromUpdater(UINodeId updaterRoot,
                                                                                 UINodeId treeView) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return Core::failure(valid.error());
        }
        return treeViewStatesByNodeIndex[treeView.index()].selection;
    }

    [[nodiscard]] Core::Status setTreeViewItemExpandedFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                  u64 logicalIndex, bool expanded)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return valid;
        }
        auto descriptor = resolveTreeViewLogicalItem(treeView, logicalIndex);
        if (!descriptor)
        {
            return Core::failure(descriptor.error());
        }
        if (!descriptor->expandable)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView item is not expandable");
        }
        if (descriptor->expanded == expanded)
        {
            return Core::success();
        }
        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        if (!state.dataSource.canSetItemExpanded())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView data source does not support expansion");
        }
        if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
        {
            return dirty;
        }
        if (!state.dataSource.setItemExpanded(state.dataSource.state, descriptor->key, expanded))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView data source rejected expansion");
        }
        if (state.selection.key == descriptor->key)
        {
            state.selection.logicalIndex = logicalIndex;
            state.selection.level = descriptor->level;
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status scrollTreeViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                                u64 logicalIndex, UITreeViewScrollAlignment alignment)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (Core::Status valid = validateTreeViewUpdater(updaterRoot, treeView); !valid)
        {
            return valid;
        }
        if (!isValidTreeViewScrollAlignment(alignment))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView scroll alignment is not recognized");
        }
        auto descriptor = resolveTreeViewLogicalItem(treeView, logicalIndex);
        if (!descriptor)
        {
            return Core::failure(descriptor.error());
        }
        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        const float rowStart = static_cast<float>(static_cast<double>(logicalIndex) * state.style.rowHeight);
        const float rowEnd = rowStart + state.style.rowHeight;
        const float viewportHeight = state.committedMetrics.viewportSize.height;
        const u64 logicalItemCount = state.dataSource.itemCount(state.dataSource.state);
        const double contentHeight64 = static_cast<double>(logicalItemCount) * state.style.rowHeight;
        if (!std::isfinite(contentHeight64) || contentHeight64 > (std::numeric_limits<float>::max)())
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView logical content height is not representable");
        }
        const float maximumOffset =
            normalizeFloat((std::max)(0.0F, static_cast<float>(contentHeight64) - viewportHeight));
        float nextOffset = state.requestedScrollOffset;
        switch (alignment)
        {
        case UITreeViewScrollAlignment::Start:
            nextOffset = rowStart;
            break;
        case UITreeViewScrollAlignment::Center:
            nextOffset = rowStart - (viewportHeight - state.style.rowHeight) * 0.5F;
            break;
        case UITreeViewScrollAlignment::End:
            nextOffset = rowEnd - viewportHeight;
            break;
        case UITreeViewScrollAlignment::Nearest:
            if (rowStart < nextOffset)
            {
                nextOffset = rowStart;
            } else if (rowEnd > nextOffset + viewportHeight)
            {
                nextOffset = rowEnd - viewportHeight;
            }
            break;
        }
        nextOffset = normalizeFloat((std::clamp)(nextOffset, 0.0F, maximumOffset));
        if (nextOffset == state.requestedScrollOffset)
        {
            return Core::success();
        }
        if (Core::Status dirty = markLayoutStyleDirty(treeView); !dirty)
        {
            return dirty;
        }
        state.requestedScrollOffset = nextOffset;
        return Core::success();
    }

    void addDropdownActivationDirtyReservationCandidates(UINodeId control)
    {
        const NodeRecord* record = contains(control) ? nodes.tryGet(control.storageId()) : nullptr;
        if (record == nullptr)
        {
            return;
        }
        if (record->kind == UIWidgetKind::Dropdown)
        {
            const UINodeId popup = popupForDropdown(control);
            addRouteLayoutDirtyReservationCandidates(control);
            addRouteLayoutDirtyReservationCandidates(popup);
            if (popup.hasValue() && !popupStatesByNodeIndex[popup.index()].open && activePopupNode != popup)
            {
                addRouteLayoutDirtyReservationCandidates(activePopupNode);
                addRouteLayoutDirtyReservationCandidates(dropdownForPopup(activePopupNode));
            }
            return;
        }
        if (record->kind != UIWidgetKind::DropdownItem)
        {
            return;
        }

        const UINodeId dropdown = dropdownForItem(control);
        const UINodeId popup = dropdown.hasValue() ? popupForDropdown(dropdown) : UINodeId{};
        addRouteLayoutDirtyReservationCandidates(dropdown);
        addRouteLayoutDirtyReservationCandidates(popup);
        addRouteLayoutDirtyReservationCandidates(control);
        if (dropdown.hasValue())
        {
            addRouteLayoutDirtyReservationCandidates(dropdownStatesByNodeIndex[dropdown.index()].selectedItem);
        }
    }

    [[nodiscard]] Core::Result<bool> activateDropdownControl(UINodeId control)
    {
        const NodeRecord* record = contains(control) ? nodes.tryGet(control.storageId()) : nullptr;
        if (record == nullptr)
        {
            return fail(UIErrorCode::InvalidNode, "UI Dropdown activation target is stale");
        }
        if (record->kind == UIWidgetKind::Dropdown)
        {
            const UINodeId popup = popupForDropdown(control);
            if (!popup.hasValue())
            {
                return fail(UIErrorCode::InvalidControlValue, "UI Dropdown has no live Popup");
            }
            const bool nextOpen = !popupStatesByNodeIndex[popup.index()].open;
            if (Core::Status status = setPopupOpenState(popup, nextOpen); !status)
            {
                return Core::failure(status.error());
            }
            return true;
        }
        if (record->kind != UIWidgetKind::DropdownItem)
        {
            return false;
        }

        const UINodeId dropdown = dropdownForItem(control);
        const UINodeId popup = dropdown.hasValue() ? popupForDropdown(dropdown) : UINodeId{};
        if (!dropdown.hasValue() || !popup.hasValue())
        {
            return fail(UIErrorCode::InvalidParent, "UI DropdownItem is detached from its Dropdown");
        }
        DropdownState& dropdownState = dropdownStatesByNodeIndex[dropdown.index()];
        PopupState& popupState = popupStatesByNodeIndex[popup.index()];
        const UINodeId previousItem = dropdownState.selectedItem;
        const bool changed = previousItem != control || popupState.open;
        if (!changed)
        {
            return false;
        }
        if (Core::Status dirty = markLayoutDirtyBatch({dropdown, popup, previousItem, control}); !dirty)
        {
            return Core::failure(dirty.error());
        }

        dropdownState.selectedItem = control;
        popupState.open = false;
        if (activePopupNode == popup)
        {
            activePopupNode = {};
        }
        popupDismissPointerBarrierActive = false;
        defaultActionPressState.clearAll();
        resetImeCompositionState();
        textInputFocus = {};
        defaultActionFocusButton = dropdown;
        return true;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveProgressBar(UINodeId progressBar)
    {
        auto nodeResult = resolveNode(progressBar);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::ProgressBar)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ProgressBar API requires a ProgressBar node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolvePlainButton(UINodeId button)
    {
        auto nodeResult = resolveNode(button);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isButtonChromeKind((*nodeResult)->kind))
        {
            return fail(UIErrorCode::InvalidButtonAction,
                        "UI Button paint API requires a Button, Dropdown, or DropdownItem node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Status setProgressBarRangeFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                              float minValue, float maxValue)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto progressResult = resolveProgressBar(progressBar);
        if (!progressResult)
        {
            return Core::failure(progressResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, progressBar))
        {
            return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
        }
        if (!std::isfinite(minValue) || !std::isfinite(maxValue) || !(maxValue > minValue))
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI ProgressBar range must be finite with max greater than min");
        }
        ProgressBarState& state = progressBarStatesByNodeIndex[progressBar.index()];
        const float nextValue = std::clamp(state.value, minValue, maxValue);
        if (state.minValue == minValue && state.maxValue == maxValue && state.value == nextValue)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(progressBar); !dirty)
        {
            return dirty;
        }
        state.minValue = minValue;
        state.maxValue = maxValue;
        state.value = nextValue;
        return Core::success();
    }

    [[nodiscard]] Core::Status setProgressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar, float value)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto progressResult = resolveProgressBar(progressBar);
        if (!progressResult)
        {
            return Core::failure(progressResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, progressBar))
        {
            return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
        }
        if (!std::isfinite(value))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ProgressBar value must be finite");
        }
        ProgressBarState& state = progressBarStatesByNodeIndex[progressBar.index()];
        const float nextValue = std::clamp(value, state.minValue, state.maxValue);
        if (state.value == nextValue)
        {
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(progressBar); !dirty)
        {
            return dirty;
        }
        state.value = nextValue;
        return Core::success();
    }

    [[nodiscard]] Core::Result<float> progressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto progressResult = const_cast<Impl*>(this)->resolveProgressBar(progressBar);
        if (!progressResult)
        {
            return Core::failure(progressResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, progressBar))
        {
            return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
        }
        return progressBarStatesByNodeIndex[progressBar.index()].value;
    }

    [[nodiscard]] Core::Status setProgressBarPaintFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                              const UIProgressBarPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto progressResult = resolveProgressBar(progressBar);
        if (!progressResult)
        {
            return Core::failure(progressResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, progressBar))
        {
            return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
        }
        ProgressBarState& state = progressBarStatesByNodeIndex[progressBar.index()];
        if (state.paint == paint)
        {
            detachThemeBinding(progressBar.index(), ThemeBindingProgressBarPaint);
            return Core::success();
        }
        if (Core::Status dirty = markPaintDirty(progressBar); !dirty)
        {
            return dirty;
        }
        state.paint = paint;
        detachThemeBinding(progressBar.index(), ThemeBindingProgressBarPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIProgressBarPaint> progressBarPaintFromUpdater(UINodeId updaterRoot,
                                                                               UINodeId progressBar) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto progressResult = const_cast<Impl*>(this)->resolveProgressBar(progressBar);
        if (!progressResult)
        {
            return Core::failure(progressResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, progressBar))
        {
            return fail(UIErrorCode::InvalidNode, "UI ProgressBar is not owned by the updater root");
        }
        return progressBarStatesByNodeIndex[progressBar.index()].paint;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveRadioButton(UINodeId radioButton)
    {
        auto nodeResult = resolveNode(radioButton);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if ((*nodeResult)->kind != UIWidgetKind::RadioButton)
        {
            return fail(UIErrorCode::InvalidControlValue, "UI RadioButton API requires a RadioButton node");
        }
        return *nodeResult;
    }

    [[nodiscard]] Core::Status preflightDefaultActionActivationDirty(UINodeId target, bool pressedStateChanges) const
    {
        const NodeRecord* targetRecord = nodes.tryGet(target.storageId());
        if (targetRecord == nullptr || !isDefaultActivatableKind(targetRecord->kind))
        {
            return fail(UIErrorCode::InvalidNode, "UI default-action target is stale");
        }

        usize requiredQueueEntries = 0;
        const auto countNode = [this, &requiredQueueEntries](UINodeId node) {
            if (node.hasValue() && contains(node) && dirtyQueuedByIndex[node.index()] == 0 &&
                dirtyReservedByIndex[node.index()] == 0)
            {
                ++requiredQueueEntries;
            }
        };
        const bool targetStateChanges = pressedStateChanges || targetRecord->kind == UIWidgetKind::Checkbox;
        if (targetStateChanges)
        {
            countNode(target);
        }

        if (targetRecord->kind == UIWidgetKind::RadioButton)
        {
            const NodeRecord* parent = recordByIndex(targetRecord->parentIndex);
            if (parent == nullptr)
            {
                return fail(UIErrorCode::InvalidParent, "UI RadioButton requires a live parent group");
            }
            for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
            {
                const NodeRecord* child = recordByIndex(childIndex);
                if (child == nullptr)
                {
                    return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
                }
                const u32 nextSiblingIndex = child->nextSiblingIndex;
                if (child->kind == UIWidgetKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size() &&
                    radioButtonStatesByNodeIndex[childIndex].selected != (childIndex == target.index()) &&
                    !(targetStateChanges && childIndex == target.index()) && dirtyQueuedByIndex[childIndex] == 0 &&
                    dirtyReservedByIndex[childIndex] == 0)
                {
                    ++requiredQueueEntries;
                }
                childIndex = nextSiblingIndex;
            }
        }

        const usize occupiedQueueEntries = validDirtyQueueCount() + dirtyQueueReservationCount;
        if (occupiedQueueEntries > capacityConfig.dirtyQueueCapacity ||
            requiredQueueEntries > capacityConfig.dirtyQueueCapacity - occupiedQueueEntries)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI dirty queue capacity has been exhausted");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status applyRadioButtonSelection(UINodeId radioButton, bool selected)
    {
        NodeRecord* radioRecord = nodes.tryGet(radioButton.storageId());
        if (radioRecord == nullptr || radioRecord->kind != UIWidgetKind::RadioButton ||
            radioButton.index() >= radioButtonStatesByNodeIndex.size())
        {
            return fail(UIErrorCode::InvalidNode, "UI RadioButton is stale");
        }
        if (!selected)
        {
            RadioButtonState& state = radioButtonStatesByNodeIndex[radioButton.index()];
            if (!state.selected)
            {
                return Core::success();
            }
            if (Core::Status dirty = markPaintDirty(radioButton); !dirty)
            {
                return dirty;
            }
            state.selected = false;
            return Core::success();
        }

        const NodeRecord* parent = recordByIndex(radioRecord->parentIndex);
        if (parent == nullptr)
        {
            return fail(UIErrorCode::InvalidParent, "UI RadioButton requires a live parent group");
        }
        usize requiredQueueEntries = 0;
        bool selectionChanged = false;
        for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
            }
            const u32 nextSiblingIndex = child->nextSiblingIndex;
            if (child->kind == UIWidgetKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size())
            {
                const bool nextSelected = childIndex == radioButton.index();
                if (radioButtonStatesByNodeIndex[childIndex].selected != nextSelected)
                {
                    selectionChanged = true;
                    if (dirtyQueuedByIndex[childIndex] == 0 && dirtyReservedByIndex[childIndex] == 0)
                    {
                        ++requiredQueueEntries;
                    }
                }
            }
            childIndex = nextSiblingIndex;
        }
        if (!selectionChanged)
        {
            return Core::success();
        }
        usize occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        if (occupiedQueueEntries > capacityConfig.dirtyQueueCapacity ||
            requiredQueueEntries > capacityConfig.dirtyQueueCapacity - occupiedQueueEntries)
        {
            compactDirtyQueue();
            requiredQueueEntries = 0;
            for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
            {
                const NodeRecord* child = recordByIndex(childIndex);
                if (child == nullptr)
                {
                    return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
                }
                const u32 nextSiblingIndex = child->nextSiblingIndex;
                if (child->kind == UIWidgetKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size() &&
                    radioButtonStatesByNodeIndex[childIndex].selected != (childIndex == radioButton.index()) &&
                    dirtyQueuedByIndex[childIndex] == 0 && dirtyReservedByIndex[childIndex] == 0)
                {
                    ++requiredQueueEntries;
                }
                childIndex = nextSiblingIndex;
            }
            occupiedQueueEntries = occupiedDirtyQueueSlotCount();
        }
        if (occupiedQueueEntries > capacityConfig.dirtyQueueCapacity ||
            requiredQueueEntries > capacityConfig.dirtyQueueCapacity - occupiedQueueEntries)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI RadioButton group selection exceeds dirty queue capacity");
        }
        for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
        {
            const NodeRecord* child = recordByIndex(childIndex);
            if (child == nullptr)
            {
                return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
            }
            const u32 nextSiblingIndex = child->nextSiblingIndex;
            if (child->kind == UIWidgetKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size())
            {
                RadioButtonState& state = radioButtonStatesByNodeIndex[childIndex];
                const bool nextSelected = childIndex == radioButton.index();
                if (state.selected != nextSelected)
                {
                    if (dirtyQueuedByIndex[childIndex] == 0)
                    {
                        consumeDirtyQueueReservation(childIndex);
                        dirtyQueue.push_back(idForIndex(childIndex));
                        dirtyQueuedByIndex[childIndex] = 1;
                    }
                    dirtyByIndex[childIndex] |= UIDirty::Paint | UIDirty::Semantics;
                    state.selected = nextSelected;
                }
            }
            childIndex = nextSiblingIndex;
        }
        dirtyQueueHighWater = (std::max)(dirtyQueueHighWater, dirtyQueue.size());
        phaseDirty |= PhasePaint | PhaseSemantics;
        return Core::success();
    }

    [[nodiscard]] Core::Status setRadioButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                              const UIRadioButtonPaint& paint)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto radioResult = resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, radioButton))
        {
            return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
        }
        if (!std::isfinite(paint.selectedIndicatorInset) || paint.selectedIndicatorInset < 0.0F ||
            !std::isfinite(paint.labelGap) || paint.labelGap < 0.0F)
        {
            return fail(UIErrorCode::InvalidControlValue,
                        "UI RadioButton paint metrics must be finite and non-negative");
        }
        RadioButtonState& state = radioButtonStatesByNodeIndex[radioButton.index()];
        if (state.paint == paint)
        {
            detachThemeBinding(radioButton.index(), ThemeBindingRadioButtonPaint);
            return Core::success();
        }
        const bool layoutChanged = state.paint.labelGap != paint.labelGap;
        Core::Status dirty = layoutChanged ? markLayoutStyleDirty(radioButton) : markPaintDirty(radioButton);
        if (!dirty)
        {
            return dirty;
        }
        state.paint = paint;
        detachThemeBinding(radioButton.index(), ThemeBindingRadioButtonPaint);
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIRadioButtonPaint> radioButtonPaintFromUpdater(UINodeId updaterRoot,
                                                                               UINodeId radioButton) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto radioResult = const_cast<Impl*>(this)->resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, radioButton))
        {
            return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
        }
        return radioButtonStatesByNodeIndex[radioButton.index()].paint;
    }

    [[nodiscard]] Core::Status setRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                               UIButtonActionCallback&& callback)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        auto radioResult = resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        return setButtonActionFromUpdater(updaterRoot, radioButton, std::move(callback));
    }

    [[nodiscard]] Core::Status clearRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        auto radioResult = resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        return clearButtonActionFromUpdater(updaterRoot, radioButton);
    }

    [[nodiscard]] Core::Status setRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                                 bool selected)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto radioResult = resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, radioButton))
        {
            return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
        }
        return applyRadioButtonSelection(radioButton, selected);
    }

    [[nodiscard]] Core::Result<bool> isRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton) const
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        auto radioResult = const_cast<Impl*>(this)->resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, radioButton))
        {
            return fail(UIErrorCode::InvalidNode, "UI RadioButton is not owned by the updater root");
        }
        return radioButtonStatesByNodeIndex[radioButton.index()].selected;
    }

    [[nodiscard]] Core::Result<bool> isRadioButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId radioButton)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        auto radioResult = resolveRadioButton(radioButton);
        if (!radioResult)
        {
            return Core::failure(radioResult.error());
        }
        return isButtonPressedFromUpdater(updaterRoot, radioButton);
    }

    void appendCommittedTree(u32 index, u32& ordinal, std::pmr::vector<UICommittedNodeEntry>& output) const noexcept
    {
        const u32 rootIndex = index;
        u32 currentIndex = rootIndex;
        while (currentIndex != InvalidNodeIndex)
        {
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr)
            {
                return;
            }

            const u32 currentOrdinal = ordinal++;
            output.push_back(UICommittedNodeEntry{
                .node = idForIndex(currentIndex),
                .parent = idForIndex(record->parentIndex),
                .depth = record->depth,
                .preorder = currentOrdinal,
                .paintOrdinal = currentOrdinal,
                .kind = record->kind,
            });

            if (record->firstChildIndex != InvalidNodeIndex)
            {
                currentIndex = record->firstChildIndex;
                continue;
            }

            while (currentIndex != rootIndex)
            {
                record = recordByIndex(currentIndex);
                if (record == nullptr)
                {
                    return;
                }
                if (record->nextSiblingIndex != InvalidNodeIndex)
                {
                    currentIndex = record->nextSiblingIndex;
                    break;
                }
                currentIndex = record->parentIndex;
            }
            if (currentIndex == rootIndex)
            {
                currentIndex = InvalidNodeIndex;
            }
        }
    }

    [[nodiscard]] Core::Status commitStructure()
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI structure cannot be committed during pointer routing");
        }
        drainDeferredRootDestroys();
        return publishStructureIfDirty();
    }

    [[nodiscard]] Core::Status validateViewport(UILogicalSize viewportSize) const
    {
        if (!isFiniteNonNegative(viewportSize.width) || !isFiniteNonNegative(viewportSize.height))
        {
            return fail(UIErrorCode::InvalidLayout, "UI layout viewport must be finite and non-negative");
        }
        return Core::success();
    }

    void publishControlLayoutState(const std::pmr::vector<u32>& order) noexcept
    {
        for (const u32 index : order)
        {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr || record->kind != UIWidgetKind::ScrollView ||
                index >= scrollViewStatesByNodeIndex.size() || index >= scrollViewLayoutScratchByNodeIndex.size())
            {
                continue;
            }
            ScrollViewState& state = scrollViewStatesByNodeIndex[index];
            const ScrollViewLayoutScratch& layout = scrollViewLayoutScratchByNodeIndex[index];
            state.requestedOffset = layout.metrics.offset;
            state.committedMetrics = layout.metrics;
            state.committedViewportRect = layout.viewportRect;
        }
        for (const u32 index : order)
        {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr || record->kind != UIWidgetKind::Popup || index >= popupStatesByNodeIndex.size() ||
                index >= popupLayoutScratchByNodeIndex.size())
            {
                continue;
            }
            popupStatesByNodeIndex[index].committedMetrics = popupLayoutScratchByNodeIndex[index].metrics;
        }
        for (const u32 index : order)
        {
            const NodeRecord* record = recordByIndex(index);
            if (record == nullptr)
            {
                continue;
            }
            if (record->kind == UIWidgetKind::ListView && index < listViewStatesByNodeIndex.size() &&
                index < listViewLayoutScratchByNodeIndex.size())
            {
                ListViewState& state = listViewStatesByNodeIndex[index];
                state.requestedScrollOffset = listViewLayoutScratchByNodeIndex[index].metrics.scrollOffset;
                state.committedMetrics = listViewLayoutScratchByNodeIndex[index].metrics;
                state.committedViewportRect = listViewLayoutScratchByNodeIndex[index].viewportRect;
            } else if (record->kind == UIWidgetKind::ListViewItem && index < listViewItemStatesByNodeIndex.size())
            {
                ListViewItemState& item = listViewItemStatesByNodeIndex[index];
                item.committedKey = item.key;
                item.committedLogicalIndex = item.logicalIndex;
                item.committedBound = item.bound;
                item.committedEnabled = item.enabled;
            } else if (record->kind == UIWidgetKind::TreeView && index < treeViewStatesByNodeIndex.size() &&
                       index < treeViewLayoutScratchByNodeIndex.size())
            {
                TreeViewState& state = treeViewStatesByNodeIndex[index];
                state.requestedScrollOffset = treeViewLayoutScratchByNodeIndex[index].metrics.scrollOffset;
                state.committedMetrics = treeViewLayoutScratchByNodeIndex[index].metrics;
                state.committedViewportRect = treeViewLayoutScratchByNodeIndex[index].viewportRect;
            } else if (record->kind == UIWidgetKind::TreeViewItem && index < treeViewItemStatesByNodeIndex.size())
            {
                TreeViewItemState& item = treeViewItemStatesByNodeIndex[index];
                item.committedKey = item.key;
                item.committedLogicalIndex = item.logicalIndex;
                item.committedLevel = item.level;
                item.committedBound = item.bound;
                item.committedEnabled = item.enabled;
                item.committedExpandable = item.expandable;
                item.committedExpanded = item.expanded;
            }
        }
    }

    [[nodiscard]] Core::Status commitLayout(UILogicalSize viewportSize)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI layout cannot be committed during pointer routing");
        }
        drainDeferredRootDestroys();
        if (Core::Status viewportStatus = validateViewport(viewportSize); !viewportStatus)
        {
            return viewportStatus;
        }
        viewportSize.width = normalizeFloat(viewportSize.width);
        viewportSize.height = normalizeFloat(viewportSize.height);
        const bool viewportChanged = !hasCommittedViewport || viewportSize != committedViewportSize;
        const bool structureNeedsCommit = isPhaseDirty(PhaseStructure);
        const bool layoutNeedsCommit = structureNeedsCommit || isPhaseDirty(PhaseLayout) || viewportChanged;
        const bool hitNeedsCommit = isPhaseDirty(PhaseHit) || layoutNeedsCommit || committedHitRevision == 0;
        bool paintNeedsCommit = isPhaseDirty(PhasePaint) || layoutNeedsCommit || committedPaintRevision == 0;
        bool semanticsNeedsCommit =
            isPhaseDirty(PhaseSemantics) || layoutNeedsCommit || committedSemanticsRevision == 0;

        if (!layoutNeedsCommit && !hitNeedsCommit && !paintNeedsCommit && !semanticsNeedsCommit)
        {
            lastLayoutPass = {};
            lastHitRebuildCount = 0;
            lastPaintCacheRebuildCount = 0;
            lastPaintSnapshotRebuildCount = 0;
            return Core::success();
        }
        if (layoutNeedsCommit && nodes.activeCount() > capacityConfig.layoutSnapshotCapacity)
        {
            return fail(UIErrorCode::CapacityExceeded, "UI committed layout snapshot capacity has been exhausted");
        }

        const usize writeStructureBufferIndex = 1 - publishedBufferIndex;
        if (structureNeedsCommit)
        {
            buildCommittedStructure(committedBuffers[writeStructureBufferIndex]);
        }

        usize writeLayoutBufferIndex = publishedLayoutBufferIndex;
        LayoutPassStatistics pass{};
        std::span<const UICommittedLayoutEntry> candidateLayoutEntries{};
        if (layoutNeedsCommit)
        {
            const bool allowLayoutReuse = !structureNeedsCommit && !viewportChanged && layoutReuseCacheValid;
            layoutReuseCacheValid = false;
            layoutReuseInProgress = allowLayoutReuse;
            auto layoutReuseGuard = Core::makeScopeExit([this]() noexcept { layoutReuseInProgress = false; });
            writeLayoutBufferIndex = 1 - publishedLayoutBufferIndex;
            std::pmr::vector<UICommittedLayoutEntry>& writeLayout = committedLayoutBuffers[writeLayoutBufferIndex];
            writeLayout.clear();

            buildLayoutOrder(layoutOrderScratch);
            pass.passCount = layoutOrderScratch.empty() ? 0 : 1;
            prepareLayoutState(viewportSize, layoutOrderScratch, allowLayoutReuse);
            measureLayout(viewportSize, layoutOrderScratch, pass);
            if (Core::Status arranged = arrangeLayout(viewportSize, layoutOrderScratch, pass); !arranged)
            {
                return arranged;
            }
            if (Core::Status candidateStatus = validateLayoutCandidate(layoutOrderScratch); !candidateStatus)
            {
                return candidateStatus;
            }
            buildCommittedLayout(writeLayout, layoutOrderScratch);
            candidateLayoutEntries = std::span<const UICommittedLayoutEntry>(writeLayout.data(), writeLayout.size());
        } else
        {
            const std::pmr::vector<UICommittedLayoutEntry>& currentLayout =
                committedLayoutBuffers[publishedLayoutBufferIndex];
            candidateLayoutEntries =
                std::span<const UICommittedLayoutEntry>(currentLayout.data(), currentLayout.size());
        }

        const u64 candidateStructureRevision = committedRevision + (structureNeedsCommit ? 1u : 0u);
        const u64 candidateLayoutRevision = committedLayoutRevision + (layoutNeedsCommit ? 1u : 0u);
        // C1c-a derives paint order solely from committed tree preorder. A
        // future independent Order mutation must advance this stamp without
        // conflating it with hit-only policy changes.
        const u64 candidatePaintOrderRevision = candidateStructureRevision;
        usize writeHitBufferIndex = publishedHitBufferIndex;
        usize candidateHitTargetCount = committedHitTargetCount;
        u32 candidateActiveModalEntryIndex = committedActiveModalEntryIndex;
        if (hitNeedsCommit)
        {
            writeHitBufferIndex = 1 - publishedHitBufferIndex;
            auto hitResult = buildCommittedHit(committedHitBuffers[writeHitBufferIndex], candidateLayoutEntries);
            if (!hitResult)
            {
                return Core::failure(hitResult.error());
            }
            candidateHitTargetCount = hitResult->targetCount;
            candidateActiveModalEntryIndex = hitResult->activeModalEntryIndex;
        }
        const std::pmr::vector<UICommittedHitEntry>& candidateHitBuffer = committedHitBuffers[writeHitBufferIndex];
        const std::span<const UICommittedHitEntry> candidateHitEntries(candidateHitBuffer.data(),
                                                                       candidateHitBuffer.size());
        const UINodeId candidateActiveModalNode = candidateActiveModalEntryIndex < candidateHitEntries.size()
                                                      ? candidateHitEntries[candidateActiveModalEntryIndex].node
                                                      : UINodeId{};

        const UINodeId previousDefaultActionFocus = defaultActionFocusButton;
        const UINodeId previousTextInputFocus = textInputFocus;
        const UINodeId previousArmedPrimaryButton = armedPrimaryButton;
        const bool previousArmedPrimaryButtonPressed = armedPrimaryButtonPressed;
        const UINodeId previousHoveredPrimaryButton = hoveredPrimaryButton;
        const Detail::UIDefaultActionPressState previousDefaultActionPressState =
            defaultActionPressState;
        const UINodeId previousArmedSlider = armedSlider;
        const UINodeId previousArmedScrollView = armedScrollView;
        const UIScrollAxes previousArmedScrollAxis = armedScrollAxis;
        const float previousScrollDragGrabOffset = scrollDragGrabOffset;
        const bool previousScrollThumbDragActive = scrollThumbDragActive;
        const UINodeId previousArmedTextEdit = armedTextEdit;
        const UINodeId previousCapturedPointer = capturedPointerNode;
        const usize previousPublishedHitBufferIndex = publishedHitBufferIndex;
        const Detail::UIImeCompositionState previousImeComposition = imeComposition;
        struct FocusRestoreRollbackEntry final {
            u32 index = InvalidNodeIndex;
            UINodeId value{};
        };
        std::array<FocusRestoreRollbackEntry, 2> focusRestoreRollbackEntries{};
        usize focusRestoreRollbackCount = 0;
        const auto setFocusRestore = [&](u32 index, UINodeId value) noexcept {
            if (index >= focusRestoreByNodeIndex.size())
            {
                return;
            }
            bool alreadySaved = false;
            for (usize saved = 0; saved < focusRestoreRollbackCount; ++saved)
            {
                alreadySaved = alreadySaved || focusRestoreRollbackEntries[saved].index == index;
            }
            if (!alreadySaved && focusRestoreRollbackCount < focusRestoreRollbackEntries.size())
            {
                focusRestoreRollbackEntries[focusRestoreRollbackCount++] = {
                    .index = index,
                    .value = focusRestoreByNodeIndex[index],
                };
            }
            focusRestoreByNodeIndex[index] = value;
        };
        auto focusRollback = Core::makeScopeExit([&]() noexcept {
            defaultActionFocusButton = previousDefaultActionFocus;
            textInputFocus = previousTextInputFocus;
            armedPrimaryButton = previousArmedPrimaryButton;
            armedPrimaryButtonPressed = previousArmedPrimaryButtonPressed;
            hoveredPrimaryButton = previousHoveredPrimaryButton;
            defaultActionPressState = previousDefaultActionPressState;
            armedSlider = previousArmedSlider;
            armedScrollView = previousArmedScrollView;
            armedScrollAxis = previousArmedScrollAxis;
            scrollDragGrabOffset = previousScrollDragGrabOffset;
            scrollThumbDragActive = previousScrollThumbDragActive;
            armedTextEdit = previousArmedTextEdit;
            capturedPointerNode = previousCapturedPointer;
            imeComposition = previousImeComposition;
            for (usize saved = 0; saved < focusRestoreRollbackCount; ++saved)
            {
                const FocusRestoreRollbackEntry& entry = focusRestoreRollbackEntries[saved];
                if (entry.index < focusRestoreByNodeIndex.size())
                {
                    focusRestoreByNodeIndex[entry.index] = entry.value;
                }
            }
        });

        const auto firstFocusCandidateWithin = [&](u32 scopeEntryIndex) noexcept {
            for (u32 entryIndex = 0; entryIndex < candidateHitEntries.size(); ++entryIndex)
            {
                const UICommittedHitEntry& entry = candidateHitEntries[entryIndex];
                if (entry.policy == UIPointerHitPolicy::Targetable && contains(entry.node) &&
                    isNodeEnabled(entry.node) && isKeyboardFocusableKind(entry.kind) &&
                    hitEntryAllowedByModal(entry, candidateActiveModalEntryIndex) &&
                    hitEntryIsWithinScope(entryIndex, scopeEntryIndex, candidateHitEntries))
                {
                    return entry.node;
                }
            }
            return UINodeId{};
        };

        UINodeId desiredFocus = defaultActionFocusButton;
        const bool modalChanged = candidateActiveModalNode != activeModalNode;
        if (modalChanged)
        {
            UINodeId restoreFocus = defaultActionFocusButton;
            if (activeModalNode.hasValue())
            {
                if (contains(activeModalNode) && activeModalNode.index() < focusRestoreByNodeIndex.size())
                {
                    restoreFocus = focusRestoreByNodeIndex[activeModalNode.index()];
                } else if (hasPendingDestroyedModalRestoreFocus)
                {
                    restoreFocus = pendingDestroyedModalRestoreFocus;
                }
            }

            const u32 previousModalCandidateEntry = findHitEntryIndex(activeModalNode, candidateHitEntries);
            const bool openingNestedModal =
                candidateActiveModalEntryIndex < candidateHitEntries.size() &&
                previousModalCandidateEntry < candidateHitEntries.size() &&
                hitEntryIsWithinScope(candidateActiveModalEntryIndex, previousModalCandidateEntry, candidateHitEntries);
            if (openingNestedModal)
            {
                restoreFocus = defaultActionFocusButton;
            }

            if (activeModalNode.hasValue() && contains(activeModalNode) && !openingNestedModal)
            {
                setFocusRestore(activeModalNode.index(), {});
            }

            if (candidateActiveModalNode.hasValue())
            {
                const bool returningToExistingModal =
                    candidateActiveModalNode.index() < focusRestoreByNodeIndex.size() &&
                    focusRestoreByNodeIndex[candidateActiveModalNode.index()].hasValue() && !openingNestedModal;
                if (!returningToExistingModal)
                {
                    setFocusRestore(candidateActiveModalNode.index(), restoreFocus);
                }
                desiredFocus = returningToExistingModal ? restoreFocus : defaultActionFocusButton;
            } else
            {
                desiredFocus = restoreFocus;
            }
        }

        if (!isKeyboardFocusCandidate(desiredFocus, candidateHitEntries, candidateActiveModalEntryIndex))
        {
            desiredFocus = candidateActiveModalNode.hasValue()
                               ? firstFocusCandidateWithin(candidateActiveModalEntryIndex)
                               : UINodeId{};
        }

        const u32 desiredFocusEntryIndex = findHitEntryIndex(desiredFocus, candidateHitEntries);
        const bool desiredFocusIsTextEdit = desiredFocusEntryIndex < candidateHitEntries.size() &&
                                            candidateHitEntries[desiredFocusEntryIndex].kind == UIWidgetKind::TextEdit;
        const UINodeId desiredTextFocus = desiredFocusIsTextEdit ? desiredFocus : UINodeId{};
        const bool focusChanged = desiredFocus != defaultActionFocusButton || desiredTextFocus != textInputFocus;
        if (focusChanged)
        {
            defaultActionFocusButton = desiredFocus;
            textInputFocus = desiredTextFocus;
            defaultActionPressState.clearAll();
            if (desiredTextFocus != previousTextInputFocus)
            {
                resetImeCompositionState();
            }
            paintNeedsCommit = true;
            semanticsNeedsCommit = true;
        }

        const bool clearTextEditArm =
            armedTextEdit.hasValue() &&
            (!isLiveTextEdit(armedTextEdit) ||
             !isKeyboardFocusCandidate(armedTextEdit, candidateHitEntries, candidateActiveModalEntryIndex) ||
             armedTextEdit != desiredFocus);
        const bool clearPrimaryButtonArm =
            armedPrimaryButton.hasValue() &&
            !isPointerInteractionCandidate(armedPrimaryButton, candidateHitEntries, candidateActiveModalEntryIndex);
        const bool clearSliderArm =
            armedSlider.hasValue() &&
            !isPointerInteractionCandidate(armedSlider, candidateHitEntries, candidateActiveModalEntryIndex);
        const bool clearScrollViewArm =
            armedScrollView.hasValue() &&
            !isPointerInteractionCandidate(armedScrollView, candidateHitEntries, candidateActiveModalEntryIndex);
        const bool clearButtonHover =
            hoveredPrimaryButton.hasValue() &&
            !isPointerInteractionCandidate(hoveredPrimaryButton, candidateHitEntries, candidateActiveModalEntryIndex);
        const bool clearPointerCapture =
            capturedPointerNode.hasValue() &&
            !isPointerCaptureCandidate(capturedPointerNode, candidateHitEntries, candidateActiveModalEntryIndex);
        if (clearTextEditArm)
        {
            armedTextEdit = {};
            paintNeedsCommit = true;
        }
        if (clearPrimaryButtonArm || clearSliderArm || clearScrollViewArm || clearButtonHover)
        {
            if (clearPrimaryButtonArm)
            {
                clearArmedPrimaryButton();
            }
            if (clearSliderArm)
            {
                clearArmedSlider();
            }
            if (clearScrollViewArm)
            {
                clearArmedScrollView();
            }
            if (clearButtonHover)
            {
                clearHoveredPrimaryButton();
            }
            paintNeedsCommit = true;
        }

        usize writePaintBufferIndex = publishedPaintBufferIndex;
        usize candidatePaintCacheRebuildCount = 0;
        if (paintNeedsCommit)
        {
            auto paintCapacity = validatePaintCandidateCapacity(candidateLayoutEntries);
            if (!paintCapacity)
            {
                return Core::failure(paintCapacity.error());
            }
            writePaintBufferIndex = 1 - publishedPaintBufferIndex;
            candidatePaintCacheRebuildCount = rebuildDirtyPaintCaches(candidateLayoutEntries);
            buildCommittedPaint(committedPaintBuffers[writePaintBufferIndex], candidateLayoutEntries);
        }

        usize writeSemanticsBufferIndex = publishedSemanticsBufferIndex;
        if (semanticsNeedsCommit)
        {
            writeSemanticsBufferIndex = 1 - publishedSemanticsBufferIndex;
            if (Core::Status status = buildCommittedSemantics(committedSemanticsBuffers[writeSemanticsBufferIndex],
                                                              committedSemanticsTextBuffers[writeSemanticsBufferIndex],
                                                              candidateLayoutEntries);
                !status)
            {
                return status;
            }
        }

        if (structureNeedsCommit)
        {
            publishedBufferIndex = writeStructureBufferIndex;
            ++committedRevision;
        }
        if (layoutNeedsCommit)
        {
            publishedLayoutBufferIndex = writeLayoutBufferIndex;
            ++committedLayoutRevision;
            committedLayoutStructureRevision = candidateStructureRevision;
            committedViewportSize = viewportSize;
            hasCommittedViewport = true;
        }
        if (hitNeedsCommit)
        {
            publishedHitBufferIndex = writeHitBufferIndex;
            ++committedHitRevision;
            committedHitStructureRevision = candidateStructureRevision;
            committedHitLayoutRevision = candidateLayoutRevision;
            committedHitPaintOrderRevision = candidatePaintOrderRevision;
            committedHitTargetCount = candidateHitTargetCount;
            committedActiveModalEntryIndex = candidateActiveModalEntryIndex;
            activeModalNode = candidateActiveModalNode;
        }
        if (paintNeedsCommit)
        {
            publishedPaintBufferIndex = writePaintBufferIndex;
            ++committedPaintRevision;
            committedPaintStructureRevision = candidateStructureRevision;
            committedPaintLayoutRevision = candidateLayoutRevision;
            committedPaintOrderRevision = candidatePaintOrderRevision;
            committedPaintViewportSize = viewportSize;
        }
        if (semanticsNeedsCommit)
        {
            publishedSemanticsBufferIndex = writeSemanticsBufferIndex;
            ++committedSemanticsRevision;
            committedSemanticsStructureRevision = candidateStructureRevision;
            committedSemanticsLayoutRevision = candidateLayoutRevision;
            committedSemanticsViewportSize = viewportSize;
        }
        lastLayoutPass = layoutNeedsCommit ? pass : LayoutPassStatistics{};
        lastHitRebuildCount = hitNeedsCommit ? 1 : 0;
        lastPaintCacheRebuildCount = candidatePaintCacheRebuildCount;
        lastPaintSnapshotRebuildCount = paintNeedsCommit ? 1 : 0;
        if (layoutNeedsCommit)
        {
            publishControlLayoutState(layoutOrderScratch);
            layoutReuseCacheValid = true;
        }
        clearDirtyState();
        pendingDestroyedModalRestoreFocus = {};
        hasPendingDestroyedModalRestoreFocus = false;
        focusRollback.release();
        if (clearPointerCapture)
        {
            const auto& previousHitEntries = committedHitBuffers[previousPublishedHitBufferIndex];
            dispatchPointerCancelToCapture(previousHitEntries);
        }
        return Core::success();
    }

    [[nodiscard]] UICommittedStructureView committedStructure() const noexcept
    {
        const std::pmr::vector<UICommittedNodeEntry>& entries = committedBuffers[publishedBufferIndex];
        return UICommittedStructureView{
            std::span<const UICommittedNodeEntry>(entries.data(), entries.size()),
            committedRevision,
        };
    }

    [[nodiscard]] UICommittedLayoutView committedLayout() const noexcept
    {
        const std::pmr::vector<UICommittedLayoutEntry>& entries = committedLayoutBuffers[publishedLayoutBufferIndex];
        return UICommittedLayoutView{
            std::span<const UICommittedLayoutEntry>(entries.data(), entries.size()),
            committedLayoutStructureRevision,
            committedLayoutRevision,
        };
    }

    [[nodiscard]] UICommittedHitView committedHit() const noexcept
    {
        const std::pmr::vector<UICommittedHitEntry>& entries = committedHitBuffers[publishedHitBufferIndex];
        return UICommittedHitView{
            std::span<const UICommittedHitEntry>(entries.data(), entries.size()),
            committedHitStructureRevision,
            committedHitLayoutRevision,
            committedHitPaintOrderRevision,
            committedHitRevision,
            committedActiveModalEntryIndex,
        };
    }

    [[nodiscard]] UICommittedPaintView committedPaint() const noexcept
    {
        const std::pmr::vector<UICommittedPaintEntry>& entries = committedPaintBuffers[publishedPaintBufferIndex];
        return UICommittedPaintView{
            std::span<const UICommittedPaintEntry>(entries.data(), entries.size()),
            committedPaintViewportSize,
            committedPaintStructureRevision,
            committedPaintLayoutRevision,
            committedPaintOrderRevision,
            committedPaintRevision,
        };
    }

    [[nodiscard]] UICommittedSemanticsView committedSemantics() const noexcept
    {
        const std::pmr::vector<UISemanticsEntry>& entries = committedSemanticsBuffers[publishedSemanticsBufferIndex];
        return UICommittedSemanticsView{
            std::span<const UISemanticsEntry>(entries.data(), entries.size()),
            committedSemanticsViewportSize,
            committedSemanticsStructureRevision,
            committedSemanticsLayoutRevision,
            committedSemanticsRevision,
        };
    }

    [[nodiscard]] std::span<const u8> glyphAtlasPixels() const noexcept
    {
        if (!glyphAtlas)
        {
            return {};
        }
        return glyphAtlas->pagePixels();
    }

    [[nodiscard]] u32 glyphAtlasWidth() const noexcept
    {
        return glyphAtlas ? glyphAtlas->capacity().width : 0U;
    }

    [[nodiscard]] u32 glyphAtlasHeight() const noexcept
    {
        return glyphAtlas ? glyphAtlas->capacity().height : 0U;
    }

    [[nodiscard]] Core::Status openTextFont(std::span<const std::byte> fontBytes, i32 faceIndex)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!textRasterizer)
        {
            return fail(UIErrorCode::InvalidFont, "UI context has no text rasterizer");
        }

        auto newFace = textRasterizer->openFace(fontBytes, faceIndex);
        if (!newFace)
        {
            return Core::failure(newFace.error());
        }

        if (textFace.hasValue())
        {
            static_cast<void>(textRasterizer->closeFace(textFace));
            textFace = {};
        }
        textFace = *newFace;
        if (glyphAtlas)
        {
            glyphAtlas->clear();
        }

        // Remeasure retained text and dirty layout/paint for all text nodes.
        for (u32 index = 0; index < static_cast<u32>(textStatesByIndex.size()); ++index)
        {
            WidgetTextState& state = textStatesByIndex[index];
            if (!state.hasContent)
            {
                continue;
            }
            const UINodeId node = idForIndex(index);
            if (!node.hasValue() || !contains(node))
            {
                continue;
            }
            auto metrics = measureWidgetText(textViewFor(index), state.style);
            if (!metrics)
            {
                return Core::failure(metrics.error());
            }
            state.metrics = *metrics;
            if (Core::Status dirtyStatus = markLayoutStyleDirty(node); !dirtyStatus)
            {
                return dirtyStatus;
            }
            if (Core::Status paintStatus = markPaintDirty(node); !paintStatus)
            {
                return paintStatus;
            }
        }
        return Core::success();
    }

    [[nodiscard]] UIPointerHitQueryResult queryPointerHit(UILogicalPoint point) const noexcept
    {
        const UICommittedHitView hit = committedHit();
        UIPointerHitQueryResult result{
            .structureRevision = hit.structureRevision(),
            .layoutRevision = hit.layoutRevision(),
            .paintOrderRevision = hit.paintOrderRevision(),
            .hitRevision = hit.hitRevision(),
            .modalBarrierActive = hit.activeModalEntryIndex() < hit.entries().size(),
        };
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
        {
            return result;
        }

        const std::span<const UICommittedHitEntry> entries = hit.entries();
        for (usize reverseIndex = entries.size(); reverseIndex > 0; --reverseIndex)
        {
            ++result.visitedEntryCount;
            const usize entryIndex = reverseIndex - 1;
            const UICommittedHitEntry& entry = entries[entryIndex];
            if ((hit.activeModalEntryIndex() < entries.size() &&
                 entry.modalScopeEntryIndex != hit.activeModalEntryIndex()) ||
                entry.policy != UIPointerHitPolicy::Targetable || !containsPointHalfOpen(entry.worldRect, point) ||
                !containsPointHalfOpen(entry.effectiveClip, point) || entry.rootEntryIndex >= entries.size())
            {
                continue;
            }

            const UICommittedHitEntry& root = entries[entry.rootEntryIndex];
            result.target = UIPointerHitTarget{
                .node = entry.node,
                .rootNode = root.node,
                .hitEntryIndex = static_cast<u32>(entryIndex),
                .rootEntryIndex = entry.rootEntryIndex,
                .worldRect = entry.worldRect,
                .effectiveClip = entry.effectiveClip,
                .paintOrdinal = entry.paintOrdinal,
            };
            return result;
        }
        return result;
    }

    [[nodiscard]] Core::Result<std::pair<u32, u32>> addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor,
                                                                             UIRoutedPointerCallback&& callback,
                                                                             UINodeId updaterRoot)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (updaterRoot.hasValue() && !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        auto nodeResult = resolveNode(descriptor.node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (updaterRoot.hasValue() && !isNodeWithinRoot(updaterRoot, descriptor.node))
        {
            return fail(UIErrorCode::InvalidNode, "UI routed pointer listener node is not owned by the updater root");
        }
        if (!isValidRoutedPointerEventKind(descriptor.kind) || !isValidEventPhaseMask(descriptor.phases) ||
            !callback.hasValue())
        {
            return fail(UIErrorCode::InvalidRoutedPointerListener,
                        "UI routed pointer listener descriptor or callback is invalid");
        }
        auto registrationResult = routedPointerListenerRegistry.stage(
            descriptor, std::move(callback), routeDispatchDepth != 0);
        if (!registrationResult)
        {
            return Core::failure(registrationResult.error());
        }
        const Detail::UIRoutedPointerListenerRegistration registration = *registrationResult;
        auto rollbackRegistration = [this, &registration](Core::Error error)
            -> Core::Result<std::pair<u32, u32>> {
            routedPointerListenerRegistry.rollback(registration, routeDispatchDepth != 0);
            return Core::failure(std::move(error));
        };

        // Moving a valid fixed-inline callable may execute user move/destructor
        // code. Revalidate generation ownership before publishing the slot.
        if (updaterRoot.hasValue() && !contains(updaterRoot))
        {
            return rollbackRegistration(
                makeError(UIErrorCode::RootRequired,
                          "UI tree updater root was released while registering a routed pointer listener"));
        }
        auto liveNodeResult = resolveNode(descriptor.node);
        if (!liveNodeResult)
        {
            return rollbackRegistration(liveNodeResult.error());
        }
        if (updaterRoot.hasValue() && !isNodeWithinRoot(updaterRoot, descriptor.node))
        {
            return rollbackRegistration(
                makeError(UIErrorCode::InvalidNode,
                          "UI routed pointer listener node left the updater root during registration"));
        }
        Core::Status commitStatus = routedPointerListenerRegistry.commit(
            registration, routedPointerListenerStatePublisher(), routeDispatchDepth != 0);
        if (!commitStatus)
        {
            return rollbackRegistration(commitStatus.error());
        }
        return std::pair<u32, u32>{registration.listenerIndex, registration.generation};
    }

    [[nodiscard]] Core::Result<std::pair<u32, u32>>
    addRoutedPointerListenerFromUpdater(UINodeId updaterRoot, UIRoutedPointerListenerDesc descriptor,
                                        UIRoutedPointerCallback&& callback)
    {
        if (!updaterRoot.hasValue())
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        return addRoutedPointerListener(descriptor, std::move(callback), updaterRoot);
    }

    [[nodiscard]] Core::Status validatePointerInput(const UIPointerInputEvent& input) const
    {
        if (!input.platformFrame.hasValue() || input.sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput,
                        "UI pointer input requires a platform frame and source sequence");
        }
        if (!input.window.hasValue())
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI pointer input owner window is empty");
        }
        if (input.window != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI pointer input belongs to another owner window");
        }
        if (input.pointer != Platform::PrimaryPointerId || !isValidRoutedPointerEventKind(input.kind) ||
            input.kind == UIRoutedPointerEventKind::PointerCancel || !std::isfinite(input.position.x) ||
            !std::isfinite(input.position.y) || !std::isfinite(input.delta.x) || !std::isfinite(input.delta.y))
        {
            return fail(UIErrorCode::InvalidPointerInput,
                        "UI pointer input kind, identity, position, or delta is invalid or synthetic-only");
        }
        if ((input.kind == UIRoutedPointerEventKind::ButtonDown || input.kind == UIRoutedPointerEventKind::ButtonUp) &&
            input.button >= Platform::PointerButton::Count)
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI pointer button is invalid");
        }
        return Core::success();
    }

    void dispatchRoutedPointerListeners(UINodeId node, UIEventPhase phase, UIRoutedPointerEventKind kind,
                                        u64 registrationSerialBoundary, UIRoutedPointerEvent& event,
                                        UIPointerRouteResult& result) noexcept
    {
        if (!contains(node))
        {
            return;
        }
        Detail::UIRoutedPointerEventAccess::setRouteState(event, phase, node, result.routedTarget.node,
                                                          result.routedTarget.rootNode);
        result.listenerInvocationCount += routedPointerListenerRegistry.dispatch(
            node, kind, phaseMaskFor(phase), registrationSerialBoundary, event);
    }

    void finishRoutedPointerDispatch() noexcept
    {
        if (routeDispatchDepth == 0)
        {
            return;
        }
        --routeDispatchDepth;
        if (routeDispatchDepth != 0)
        {
            return;
        }
        drainDeferredRoutedPointerListenerReleases();
        reclaimInactiveRoutedPointerListeners();
        buttonActionRegistry.reclaim(false);
        sliderChangeCallbackRegistry.reclaim(false);
    }

    void dispatchPointerCancelToCapture(std::span<const UICommittedHitEntry> entries) noexcept
    {
        if (pointerCancelDispatchInProgress || !capturedPointerNode.hasValue() || !hasLastPointerInput)
        {
            return;
        }

        const UINodeId targetNode = capturedPointerNode;
        const u32 targetEntryIndex = findHitEntryIndex(targetNode, entries);
        if (!contains(targetNode) || targetEntryIndex >= entries.size() ||
            entries[targetEntryIndex].rootEntryIndex >= entries.size())
        {
            capturedPointerNode = {};
            return;
        }

        pointerCancelRoutePathScratch.clear();
        u32 entryIndex = targetEntryIndex;
        while (entryIndex != InvalidUIHitEntryIndex)
        {
            if (entryIndex >= entries.size() ||
                pointerCancelRoutePathScratch.size() >= capacityConfig.routePathCapacity)
            {
                pointerCancelRoutePathScratch.clear();
                capturedPointerNode = {};
                return;
            }
            pointerCancelRoutePathScratch.push_back(entryIndex);
            if (entryIndex == entries[targetEntryIndex].rootEntryIndex)
            {
                break;
            }
            entryIndex = entries[entryIndex].parentEntryIndex;
        }
        if (pointerCancelRoutePathScratch.empty() ||
            pointerCancelRoutePathScratch.back() != entries[targetEntryIndex].rootEntryIndex)
        {
            pointerCancelRoutePathScratch.clear();
            capturedPointerNode = {};
            return;
        }

        const UICommittedHitEntry& targetEntry = entries[targetEntryIndex];
        const UICommittedHitEntry& rootEntry = entries[targetEntry.rootEntryIndex];
        UIPointerRouteResult result{
            .routedTarget =
                {
                    .node = targetEntry.node,
                    .rootNode = rootEntry.node,
                    .hitEntryIndex = targetEntryIndex,
                    .rootEntryIndex = targetEntry.rootEntryIndex,
                    .worldRect = targetEntry.worldRect,
                    .effectiveClip = targetEntry.effectiveClip,
                    .paintOrdinal = targetEntry.paintOrdinal,
                },
            .routeDepth = pointerCancelRoutePathScratch.size(),
            .routedThroughPointerCapture = true,
        };
        UIPointerInputEvent cancelInput = lastPointerInput;
        cancelInput.kind = UIRoutedPointerEventKind::PointerCancel;
        cancelInput.delta = {};
        UIRoutedPointerEvent routedEvent = Detail::UIRoutedPointerEventAccess::Create(cancelInput);
        Detail::UIRoutedPointerEventAccess::setPointerCaptureRoute(routedEvent, true);
        const u64 registrationSerialBoundary = routedPointerListenerRegistry.registrationSerial();

        pointerCancelDispatchInProgress = true;
        ++routeDispatchDepth;
        auto dispatchCleanup = Core::makeScopeExit([this]() noexcept {
            capturedPointerNode = {};
            pointerCancelDispatchInProgress = false;
            pointerCancelRoutePathScratch.clear();
            finishRoutedPointerDispatch();
        });

        for (usize reversePathIndex = pointerCancelRoutePathScratch.size(); reversePathIndex > 1; --reversePathIndex)
        {
            if (!contains(targetNode))
            {
                return;
            }
            const UINodeId currentNode = entries[pointerCancelRoutePathScratch[reversePathIndex - 1]].node;
            if (contains(currentNode))
            {
                dispatchRoutedPointerListeners(currentNode, UIEventPhase::Capture,
                                               UIRoutedPointerEventKind::PointerCancel, registrationSerialBoundary,
                                               routedEvent, result);
            }
            if (routedEvent.isPropagationStopped())
            {
                return;
            }
        }

        if (!contains(targetNode))
        {
            return;
        }
        dispatchRoutedPointerListeners(targetNode, UIEventPhase::Target, UIRoutedPointerEventKind::PointerCancel,
                                       registrationSerialBoundary, routedEvent, result);
        if (routedEvent.isPropagationStopped() || !contains(targetNode))
        {
            return;
        }

        for (usize pathIndex = 1; pathIndex < pointerCancelRoutePathScratch.size(); ++pathIndex)
        {
            if (!contains(targetNode))
            {
                return;
            }
            const UINodeId currentNode = entries[pointerCancelRoutePathScratch[pathIndex]].node;
            if (contains(currentNode))
            {
                dispatchRoutedPointerListeners(currentNode, UIEventPhase::Bubble,
                                               UIRoutedPointerEventKind::PointerCancel, registrationSerialBoundary,
                                               routedEvent, result);
            }
            if (routedEvent.isPropagationStopped())
            {
                return;
            }
        }
    }

    void dispatchPointerCancelForCurrentCapture() noexcept
    {
        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        dispatchPointerCancelToCapture(entries);
    }

    void dispatchPointerCancelForSubtree(UINodeId subtreeRoot) noexcept
    {
        if (!pointerCancelDispatchInProgress && isNodeWithinSubtree(subtreeRoot, capturedPointerNode))
        {
            dispatchPointerCancelForCurrentCapture();
        }
    }

    [[nodiscard]] Core::Result<UIPointerRouteResult> routePointerInput(const UIPointerInputEvent& input)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress, "UI pointer routing is already in progress");
        }
        drainDeferredRootDestroys();
        if (Core::Status inputStatus = validatePointerInput(input); !inputStatus)
        {
            return Core::failure(inputStatus.error());
        }
        lastPointerInput = input;
        hasLastPointerInput = true;

        UIPointerRouteResult result{
            .pointQuery = queryPointerHit(input.position),
        };
        const UICommittedHitView hit = committedHit();
        const std::span<const UICommittedHitEntry> entries = hit.entries();
        const auto hitTargetForEntry = [&](u32 entryIndex) noexcept {
            if (entryIndex >= entries.size())
            {
                return UIPointerHitTarget{};
            }
            const UICommittedHitEntry& entry = entries[entryIndex];
            if (entry.rootEntryIndex >= entries.size())
            {
                return UIPointerHitTarget{};
            }
            return UIPointerHitTarget{
                .node = entry.node,
                .rootNode = entries[entry.rootEntryIndex].node,
                .hitEntryIndex = entryIndex,
                .rootEntryIndex = entry.rootEntryIndex,
                .worldRect = entry.worldRect,
                .effectiveClip = entry.effectiveClip,
                .paintOrdinal = entry.paintOrdinal,
            };
        };
        const UINodeId captureAtRouteStart = capturedPointerNode;
        const u32 capturedEntryIndex = findHitEntryIndex(capturedPointerNode, entries);
        if (capturedEntryIndex < entries.size() &&
            isPointerCaptureCandidate(capturedPointerNode, entries, committedActiveModalEntryIndex))
        {
            result.routedTarget = hitTargetForEntry(capturedEntryIndex);
            result.routedThroughPointerCapture = result.routedTarget.hasValue();
        } else
        {
            if (capturedPointerNode.hasValue())
            {
                dispatchPointerCancelToCapture(entries);
            }
            result.routedTarget = result.pointQuery.target;
        }
        result.blockedByModal = result.pointQuery.modalBarrierActive && !result.pointQuery.hasTarget();
        routePathScratch.clear();
        if (result.routedTarget.hasValue())
        {
            if (!contains(result.routedTarget.node))
            {
                result.targetInvalidated = true;
            } else
            {
                u32 entryIndex = result.routedTarget.hitEntryIndex;
                while (entryIndex != InvalidUIHitEntryIndex)
                {
                    if (entryIndex >= entries.size())
                    {
                        return fail(Core::CoreErrorCode::Internal, "UI committed pointer route entry index is invalid");
                    }
                    if (routePathScratch.size() >= capacityConfig.routePathCapacity)
                    {
                        routePathScratch.clear();
                        return fail(UIErrorCode::CapacityExceeded, "UI pointer route path capacity has been exhausted");
                    }
                    routePathScratch.push_back(entryIndex);
                    if (entryIndex == result.routedTarget.rootEntryIndex)
                    {
                        break;
                    }
                    entryIndex = entries[entryIndex].parentEntryIndex;
                    if (routePathScratch.size() > entries.size())
                    {
                        routePathScratch.clear();
                        return fail(Core::CoreErrorCode::Internal,
                                    "UI committed pointer route ancestry contains a cycle");
                    }
                }
                if (routePathScratch.empty() || routePathScratch.back() != result.routedTarget.rootEntryIndex ||
                    entries[routePathScratch.back()].node != result.routedTarget.rootNode)
                {
                    routePathScratch.clear();
                    return fail(Core::CoreErrorCode::Internal, "UI committed pointer route root is invalid");
                }
                result.routeDepth = routePathScratch.size();
            }
        }

        if (buttonRouteSerial == (std::numeric_limits<u64>::max)())
        {
            routePathScratch.clear();
            return fail(UIErrorCode::CapacityExceeded, "UI Button route serial is exhausted");
        }

        UINodeId nearestButton{};
        UINodeId nearestSlider{};
        bool pointWithinArmedButton = false;
        UINodeId physicalNearestButton{};
        u32 physicalEntryIndex = result.pointQuery.target.hitEntryIndex;
        usize physicalDepth = 0;
        while (physicalEntryIndex < entries.size())
        {
            const UICommittedHitEntry& physicalEntry = entries[physicalEntryIndex];
            if (physicalEntry.node == armedPrimaryButton)
            {
                pointWithinArmedButton = true;
            }
            if (!physicalNearestButton.hasValue() && isNodeEnabled(physicalEntry.node) &&
                isDefaultActivatableKind(physicalEntry.kind))
            {
                physicalNearestButton = physicalEntry.node;
            }
            if (physicalEntryIndex == result.pointQuery.target.rootEntryIndex)
            {
                break;
            }
            physicalEntryIndex = physicalEntry.parentEntryIndex;
            if (++physicalDepth > entries.size())
            {
                return fail(Core::CoreErrorCode::Internal, "UI committed physical pointer ancestry contains a cycle");
            }
        }
        for (const u32 routeEntryIndex : routePathScratch)
        {
            if (routeEntryIndex >= entries.size())
            {
                routePathScratch.clear();
                return fail(Core::CoreErrorCode::Internal, "UI committed Button route entry index is invalid");
            }
            const UINodeId routeNode = entries[routeEntryIndex].node;
            if (!nearestButton.hasValue() && isNodeEnabled(routeNode))
            {
                if (isDefaultActivatableKind(entries[routeEntryIndex].kind))
                {
                    nearestButton = routeNode;
                }
            }
            if (!nearestSlider.hasValue() && isNodeEnabled(routeNode))
            {
                if (entries[routeEntryIndex].kind == UIWidgetKind::Slider)
                {
                    nearestSlider = routeNode;
                }
            }
        }

        const NodeRecord* nearestButtonRecord =
            nearestButton.hasValue() && contains(nearestButton) ? nodes.tryGet(nearestButton.storageId()) : nullptr;
        const UINodeId nearestListView =
            nearestButtonRecord != nullptr && nearestButtonRecord->kind == UIWidgetKind::ListViewItem
                ? listViewForItem(nearestButton)
                : UINodeId{};
        const UINodeId nearestTreeView =
            nearestButtonRecord != nullptr && nearestButtonRecord->kind == UIWidgetKind::TreeViewItem
                ? treeViewForItem(nearestButton)
                : UINodeId{};
        const bool nearestTreeDisclosureAtRouteStart =
            nearestTreeView.hasValue() && pointWithinCommittedTreeDisclosure(nearestButton, input.position, entries);

        const UINodeId targetNode = result.routedTarget.node;
        const bool targetNodeEnabledAtRouteStart = isNodeEnabled(targetNode);
        const bool primaryButtonDown =
            input.kind == UIRoutedPointerEventKind::ButtonDown && input.button == Platform::PointerButton::Primary;
        const bool primaryButtonUp =
            input.kind == UIRoutedPointerEventKind::ButtonUp && input.button == Platform::PointerButton::Primary;
        const UINodeId popupAtRouteStart =
            activePopupNode.hasValue() && contains(activePopupNode) &&
                    activePopupNode.index() < popupStatesByNodeIndex.size() &&
                    popupStatesByNodeIndex[activePopupNode.index()].open
                ? activePopupNode
                : UINodeId{};
        const UINodeId popupDropdownAtRouteStart = dropdownForPopup(popupAtRouteStart);
        const u32 popupEntryIndexAtRouteStart = findHitEntryIndex(popupAtRouteStart, entries);
        const bool pointInsideActivePopup =
            popupEntryIndexAtRouteStart < entries.size() &&
            containsPointHalfOpen(entries[popupEntryIndexAtRouteStart].worldRect, input.position) &&
            containsPointHalfOpen(entries[popupEntryIndexAtRouteStart].effectiveClip, input.position);
        const UINodeId physicalTarget = result.pointQuery.target.node;
        const bool pointTargetsActivePopup = isNodeWithinSubtree(popupAtRouteStart, physicalTarget);
        const bool pointTargetsPopupDropdown =
            isNodeWithinSubtree(popupDropdownAtRouteStart, physicalTarget) && !pointTargetsActivePopup;
        const bool dismissActivePopupOnPrimaryDown = primaryButtonDown && popupAtRouteStart.hasValue() &&
                                                     !pointInsideActivePopup && !pointTargetsPopupDropdown;
        const bool blockPopupChromeClickThrough = primaryButtonDown && popupAtRouteStart.hasValue() &&
                                                  pointInsideActivePopup && !pointTargetsActivePopup;
        const bool popupBarrierAtRouteStart = popupDismissPointerBarrierActive;
        const ScrollBarPointerHit scrollBarHitAtRouteStart =
            primaryButtonDown ? scrollBarPointerHit(routePathScratch, entries, input.position) : ScrollBarPointerHit{};
        const UINodeId armedButtonAtRouteStart = armedPrimaryButton;
        const UINodeId armedSliderAtRouteStart = armedSlider;
        const UINodeId armedScrollViewAtRouteStart = armedScrollView;
        const UIScrollAxes armedScrollAxisAtRouteStart = armedScrollAxis;
        const float scrollDragGrabOffsetAtRouteStart = scrollDragGrabOffset;
        const bool scrollThumbDragAtRouteStart = scrollThumbDragActive;
        const UINodeId armedTextEditAtRouteStart = armedTextEdit;
        const bool armedTreeDisclosureAtRouteStart = armedTreeDisclosure;
        const bool hadArmedInteraction = armedButtonAtRouteStart.hasValue();
        const bool hadArmedSlider = armedSliderAtRouteStart.hasValue();
        const bool hadArmedScrollView = armedScrollViewAtRouteStart.hasValue();
        const bool hadArmedTextEdit = armedTextEditAtRouteStart.hasValue();
        const bool pointWithinArmedTreeDisclosure =
            armedTreeDisclosureAtRouteStart &&
            pointWithinCommittedTreeDisclosure(armedButtonAtRouteStart, input.position, entries);
        releaseRouteDirtyQueueReservations();
        if (dismissActivePopupOnPrimaryDown)
        {
            addRouteLayoutDirtyReservationCandidates(popupAtRouteStart);
            addRouteLayoutDirtyReservationCandidates(popupDropdownAtRouteStart);
        }
        const UINodeId nextHoveredButton = resolvedHoveredPrimaryButton(physicalNearestButton);
        if (nextHoveredButton != hoveredPrimaryButton)
        {
            const UINodeId previousHover =
                hoveredPrimaryButton.hasValue() && contains(hoveredPrimaryButton) ? hoveredPrimaryButton : UINodeId{};
            addRouteDirtyReservationCandidate(previousHover);
            addRouteDirtyReservationCandidate(nextHoveredButton);
        }

        if (primaryButtonDown)
        {
            addRouteDirtyReservationCandidate(defaultActionFocusButton);
            addRouteDirtyReservationCandidate(textInputFocus);
            addRouteDirtyReservationCandidate(armedSliderAtRouteStart);
            addRouteDirtyReservationCandidate(armedScrollViewAtRouteStart);
            const NodeRecord* targetRecord =
                targetNode.hasValue() && contains(targetNode) ? nodes.tryGet(targetNode.storageId()) : nullptr;
            if (scrollBarHitAtRouteStart.hasValue())
            {
                addRouteDirtyReservationCandidate(scrollBarHitAtRouteStart.scrollView);
            } else if (targetRecord != nullptr && targetRecord->kind == UIWidgetKind::TextEdit &&
                targetNodeEnabledAtRouteStart)
            {
                addRouteDirtyReservationCandidate(targetNode);
            } else if (nearestSlider.hasValue() && isNodeEnabled(nearestSlider))
            {
                addRouteDirtyReservationCandidate(nearestSlider);
            } else if (nearestButton.hasValue() && isNodeEnabled(nearestButton))
            {
                addRouteDirtyReservationCandidate(nearestButton);
                addRouteDirtyReservationCandidate(nearestListView);
                addRouteDirtyReservationCandidate(nearestTreeView);
            }
        } else if (input.kind == UIRoutedPointerEventKind::Move)
        {
            if (hadArmedSlider)
            {
                addRouteDirtyReservationCandidate(armedSliderAtRouteStart);
            }
            if (hadArmedScrollView)
            {
                addRouteDirtyReservationCandidate(armedScrollViewAtRouteStart);
            }
            if (hadArmedTextEdit)
            {
                addRouteDirtyReservationCandidate(armedTextEditAtRouteStart);
            }
            if (hadArmedInteraction && armedPrimaryButtonPressed != pointWithinArmedButton)
            {
                addRouteDirtyReservationCandidate(armedButtonAtRouteStart);
            }
        } else if (primaryButtonUp)
        {
            if (hadArmedSlider)
            {
                addRouteDirtyReservationCandidate(armedSliderAtRouteStart);
            }
            if (hadArmedScrollView)
            {
                addRouteDirtyReservationCandidate(armedScrollViewAtRouteStart);
            }
            if (hadArmedTextEdit)
            {
                addRouteDirtyReservationCandidate(armedTextEditAtRouteStart);
            }
            if (hadArmedInteraction)
            {
                addRouteDirtyReservationCandidate(armedButtonAtRouteStart);
                const NodeRecord* armedRecord =
                    contains(armedButtonAtRouteStart) ? nodes.tryGet(armedButtonAtRouteStart.storageId()) : nullptr;
                if (pointWithinArmedButton && isNodeEnabled(armedButtonAtRouteStart) && armedRecord != nullptr &&
                    armedRecord->kind == UIWidgetKind::RadioButton)
                {
                    const NodeRecord* parent = recordByIndex(armedRecord->parentIndex);
                    if (parent != nullptr)
                    {
                        for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
                        {
                            const NodeRecord* child = recordByIndex(childIndex);
                            if (child == nullptr)
                            {
                                break;
                            }
                            const u32 nextSiblingIndex = child->nextSiblingIndex;
                            if (child->kind == UIWidgetKind::RadioButton &&
                                childIndex < radioButtonStatesByNodeIndex.size() &&
                                radioButtonStatesByNodeIndex[childIndex].selected !=
                                    (childIndex == armedButtonAtRouteStart.index()))
                            {
                                addRouteDirtyReservationCandidate(idForIndex(childIndex));
                            }
                            childIndex = nextSiblingIndex;
                        }
                    }
                }
                if (pointWithinArmedButton && isNodeEnabled(armedButtonAtRouteStart))
                {
                    addDropdownActivationDirtyReservationCandidates(armedButtonAtRouteStart);
                    addRouteDirtyReservationCandidate(listViewForItem(armedButtonAtRouteStart));
                    const UINodeId armedTreeView = treeViewForItem(armedButtonAtRouteStart);
                    if (pointWithinArmedTreeDisclosure)
                    {
                        addRouteLayoutDirtyReservationCandidates(armedTreeView);
                    } else
                    {
                        addRouteDirtyReservationCandidate(armedTreeView);
                    }
                }
            }
        } else if (input.kind == UIRoutedPointerEventKind::Wheel)
        {
            for (const u32 routeEntryIndex : routePathScratch)
            {
                if (routeEntryIndex < entries.size())
                {
                    const UINodeId routeNode = entries[routeEntryIndex].node;
                    if (scrollWheelWouldChange(routeNode, input.delta))
                    {
                        addRouteDirtyReservationCandidate(routeNode);
                    }
                }
            }
        }
        if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
        {
            if (primaryButtonUp && reservation.error().code == UIErrorCode::CapacityExceeded)
            {
                const UINodeId releasedButton = armedPrimaryButton;
                const UINodeId releasedSlider = armedSlider;
                const UINodeId releasedScrollView = armedScrollView;
                const UINodeId releasedTextEdit = armedTextEdit;
                clearArmedPrimaryButton();
                clearArmedSlider();
                clearArmedScrollView();
                clearArmedTextEdit();
                capturedPointerNode = {};
                // Primary Up is a release barrier even when queue capacity is
                // exhausted. Existing dirty work will rebuild paint globally;
                // otherwise this best-effort mark publishes the released state.
                static_cast<void>(markPaintDirtyBatch({
                    releasedButton,
                    releasedSlider,
                    releasedScrollView,
                    releasedTextEdit,
                }));
            }
            releaseRouteDirtyQueueReservations();
            return Core::failure(reservation.error());
        }
        auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
        const u64 actionRegistrationSerialBoundary = buttonActionRegistry.registrationSerial();
        const Detail::UIButtonActionInvocation actionCandidate =
            primaryButtonUp && hadArmedInteraction && pointWithinArmedButton && isNodeEnabled(armedButtonAtRouteStart)
                ? captureButtonAction(armedButtonAtRouteStart, actionRegistrationSerialBoundary)
                : Detail::UIButtonActionInvocation{};
        const u64 currentButtonRouteSerial = ++buttonRouteSerial;
        const u64 registrationSerialBoundary = routedPointerListenerRegistry.registrationSerial();
        UIRoutedPointerEvent routedEvent = Detail::UIRoutedPointerEventAccess::Create(input);
        Detail::UIRoutedPointerEventAccess::setPointerCaptureRoute(routedEvent, result.routedThroughPointerCapture);
        ++routeDispatchDepth;
        auto dispatchCleanup = Core::makeScopeExit([this]() noexcept { finishRoutedPointerDispatch(); });

        if (!routePathScratch.empty() && !result.targetInvalidated)
        {
            for (usize reversePathIndex = routePathScratch.size(); reversePathIndex > 1; --reversePathIndex)
            {
                if (!contains(targetNode))
                {
                    result.targetInvalidated = true;
                    break;
                }
                const UINodeId currentNode = entries[routePathScratch[reversePathIndex - 1]].node;
                if (contains(currentNode))
                {
                    dispatchRoutedPointerListeners(currentNode, UIEventPhase::Capture, input.kind,
                                                   registrationSerialBoundary, routedEvent, result);
                }
                if (routedEvent.isPropagationStopped())
                {
                    break;
                }
            }

            if (!routedEvent.isPropagationStopped() && !result.targetInvalidated)
            {
                if (!contains(targetNode))
                {
                    result.targetInvalidated = true;
                } else
                {
                    dispatchRoutedPointerListeners(targetNode, UIEventPhase::Target, input.kind,
                                                   registrationSerialBoundary, routedEvent, result);
                }
            }

            if (!routedEvent.isPropagationStopped() && !result.targetInvalidated)
            {
                for (usize pathIndex = 1; pathIndex < routePathScratch.size(); ++pathIndex)
                {
                    if (!contains(targetNode))
                    {
                        result.targetInvalidated = true;
                        break;
                    }
                    const UINodeId currentNode = entries[routePathScratch[pathIndex]].node;
                    if (contains(currentNode))
                    {
                        dispatchRoutedPointerListeners(currentNode, UIEventPhase::Bubble, input.kind,
                                                       registrationSerialBoundary, routedEvent, result);
                    }
                    if (routedEvent.isPropagationStopped())
                    {
                        break;
                    }
                }
            }
        }

        if (targetNode.hasValue() && !contains(targetNode))
        {
            result.targetInvalidated = true;
        }
        if (primaryButtonUp)
        {
            // Physical Up is an unconditional capture release barrier, even if
            // a later repaint or callback-side mutation makes the route fail.
            capturedPointerNode = {};
        }

        bool preserveFocusForPopupBarrier = false;
        if (primaryButtonDown && (dismissActivePopupOnPrimaryDown || blockPopupChromeClickThrough))
        {
            if (dismissActivePopupOnPrimaryDown && !routedEvent.isDefaultActionPrevented() &&
                contains(popupAtRouteStart) && popupAtRouteStart.index() < popupStatesByNodeIndex.size() &&
                popupStatesByNodeIndex[popupAtRouteStart.index()].open)
            {
                if (Core::Status closed = setPopupOpenState(popupAtRouteStart, false); !closed)
                {
                    return Core::failure(closed.error());
                }
            }
            popupDismissPointerBarrierActive = true;
            routedEvent.preventDefaultAction();
            routedEvent.consumeInputTransition();
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            preserveFocusForPopupBarrier = true;
        } else if (primaryButtonUp && popupBarrierAtRouteStart)
        {
            popupDismissPointerBarrierActive = false;
            routedEvent.preventDefaultAction();
            routedEvent.consumeInputTransition();
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            preserveFocusForPopupBarrier = true;
        }

        Core::Status hoverPaintStatus = updateHoveredPrimaryButton(physicalNearestButton);
        const bool deferHoverFailureForRelease =
            primaryButtonUp && (hadArmedInteraction || hadArmedSlider || hadArmedScrollView || hadArmedTextEdit);
        if (!hoverPaintStatus && !deferHoverFailureForRelease)
        {
            return Core::failure(hoverPaintStatus.error());
        }

        if (primaryButtonDown)
        {
            clearArmedPrimaryButton();
            clearArmedSlider();
            clearArmedScrollView();
            clearArmedTextEdit();
            capturedPointerNode = {};
            const NodeRecord* targetRecord =
                targetNode.hasValue() && contains(targetNode) ? nodes.tryGet(targetNode.storageId()) : nullptr;
            const bool targetsTextEdit = targetRecord != nullptr && targetRecord->kind == UIWidgetKind::TextEdit &&
                                         targetNodeEnabledAtRouteStart;
            const UINodeId previousKeyboardFocus = defaultActionFocusButton;
            const UINodeId previousTextFocus = textInputFocus;
            const bool preserveFocusForModalBarrier = result.blockedByModal && !targetNode.hasValue();
            const bool allowsDefaultAction = !routedEvent.isDefaultActionPrevented();
            const bool willUseScrollBar = allowsDefaultAction && scrollBarHitAtRouteStart.hasValue() &&
                                          isLiveScrollable(scrollBarHitAtRouteStart.scrollView) &&
                                          isNodeEnabled(scrollBarHitAtRouteStart.scrollView);
            const bool willFocusTextEdit = allowsDefaultAction && !willUseScrollBar && targetsTextEdit;
            const bool willArmSlider =
                allowsDefaultAction && !willUseScrollBar && !willFocusTextEdit && nearestSlider.hasValue() &&
                isNodeEnabled(nearestSlider);
            UINodeId nextKeyboardFocus = preserveFocusForModalBarrier || preserveFocusForPopupBarrier
                                             ? previousKeyboardFocus
                                             : UINodeId{};
            UINodeId interactionPaintNode{};
            if (willUseScrollBar)
            {
                interactionPaintNode = scrollBarHitAtRouteStart.thumb ? scrollBarHitAtRouteStart.scrollView
                                                                     : UINodeId{};
            } else if (willFocusTextEdit)
            {
                nextKeyboardFocus = targetNode;
                interactionPaintNode = targetNode;
            } else if (willArmSlider)
            {
                interactionPaintNode = nearestSlider;
            } else if (allowsDefaultAction && nearestButton.hasValue() && isNodeEnabled(nearestButton))
            {
                const NodeRecord* nextButtonRecord = nodes.tryGet(nearestButton.storageId());
                if (nextButtonRecord != nullptr && isDefaultActivatableKind(nextButtonRecord->kind))
                {
                    nextKeyboardFocus = nextButtonRecord->kind == UIWidgetKind::ListViewItem ? nearestListView
                                        : nextButtonRecord->kind == UIWidgetKind::TreeViewItem ? nearestTreeView
                                                                                              : nearestButton;
                    interactionPaintNode = nearestButton;
                }
            }
            const UINodeId dirtyPreviousKeyboard = previousKeyboardFocus.hasValue() &&
                                                           previousKeyboardFocus != nextKeyboardFocus &&
                                                           contains(previousKeyboardFocus)
                                                       ? previousKeyboardFocus
                                                       : UINodeId{};
            const UINodeId dirtyPreviousText =
                previousTextFocus.hasValue() && previousTextFocus != nextKeyboardFocus && contains(previousTextFocus)
                    ? previousTextFocus
                    : UINodeId{};
            const UINodeId dirtyPreviousSlider = armedSliderAtRouteStart.hasValue() &&
                                                         armedSliderAtRouteStart != interactionPaintNode &&
                                                         contains(armedSliderAtRouteStart)
                                                     ? armedSliderAtRouteStart
                                                     : UINodeId{};
            const UINodeId dirtyPreviousScrollView =
                scrollThumbDragAtRouteStart && armedScrollViewAtRouteStart.hasValue() &&
                        armedScrollViewAtRouteStart != interactionPaintNode && contains(armedScrollViewAtRouteStart)
                    ? armedScrollViewAtRouteStart
                    : UINodeId{};
            const UINodeId interactionFocusNode =
                nextKeyboardFocus.hasValue() && nextKeyboardFocus != interactionPaintNode ? nextKeyboardFocus
                                                                                          : UINodeId{};
            if (Core::Status dirty = markPaintDirtyBatch({
                    dirtyPreviousKeyboard,
                    dirtyPreviousText,
                    dirtyPreviousSlider,
                    dirtyPreviousScrollView,
                    interactionPaintNode,
                    interactionFocusNode,
                });
                !dirty)
            {
                return Core::failure(dirty.error());
            }
            if (!preserveFocusForModalBarrier && !preserveFocusForPopupBarrier)
            {
                clearDefaultActionFocus();
                if (!willFocusTextEdit && textInputFocus.hasValue())
                {
                    clearImeFocus();
                }
            }
            // Only one Primary interaction may be armed. Scrollbar chrome wins
            // at its committed track; otherwise an exact TextEdit target wins,
            // then Slider drag, then Button/Checkbox.
            if (preserveFocusForModalBarrier)
            {
                // A Modal backdrop owns the input but is not a focus target.
            } else if (preserveFocusForPopupBarrier)
            {
                // Popup chrome and outside-dismiss clicks are click-through
                // barriers and preserve the focus restored by Popup close.
            } else if (willUseScrollBar)
            {
                armedScrollView = scrollBarHitAtRouteStart.scrollView;
                armedScrollAxis = scrollBarHitAtRouteStart.axis;
                scrollThumbDragActive = scrollBarHitAtRouteStart.thumb;
                if (scrollThumbDragActive)
                {
                    const float pointer = armedScrollAxis == UIScrollAxes::Horizontal ? input.position.x
                                                                                     : input.position.y;
                    const float thumbStart = armedScrollAxis == UIScrollAxes::Horizontal
                                                 ? scrollBarHitAtRouteStart.geometry.thumb.x
                                                 : scrollBarHitAtRouteStart.geometry.thumb.y;
                    scrollDragGrabOffset = normalizeFloat(pointer - thumbStart);
                }
                capturedPointerNode = armedScrollView;
                routedEvent.consumeInputTransition();
                static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
                if (!scrollThumbDragActive)
                {
                    if (auto applied = applyScrollTrackPage(armedScrollView, armedScrollAxis, input.position); !applied)
                    {
                        return Core::failure(applied.error());
                    }
                }
            } else if (willFocusTextEdit)
            {
                const UINodeId previousFocus = textInputFocus;
                if (previousFocus != targetNode)
                {
                    if (Core::Status composition = clearImeComposition(); !composition)
                    {
                        return Core::failure(composition.error());
                    }
                }
                textInputFocus = targetNode;
                defaultActionFocusButton = targetNode;
                armedTextEdit = targetNode;
                capturedPointerNode = targetNode;
                if (Core::Status selection = updateTextEditSelectionFromPointer(targetNode, input.position, false);
                    !selection)
                {
                    return Core::failure(selection.error());
                }
                routedEvent.consumeInputTransition();
                static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            } else if (willArmSlider)
            {
                armedSlider = nearestSlider;
                capturedPointerNode = nearestSlider;
                routedEvent.consumeInputTransition();
                static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
                if (auto applied = applySliderValueFromPointer(nearestSlider, input.position, input.platformFrame,
                                                               input.sourceSequence);
                    !applied)
                {
                    return Core::failure(applied.error());
                }
            } else if (nextKeyboardFocus.hasValue())
            {
                const NodeRecord* buttonRecord = nodes.tryGet(nearestButton.storageId());
                if (buttonRecord != nullptr && isDefaultActivatableKind(buttonRecord->kind))
                {
                    armedPrimaryButton = nearestButton;
                    armedPrimaryButtonPressed = true;
                    armedTreeDisclosure = nearestTreeDisclosureAtRouteStart;
                    defaultActionFocusButton = nextKeyboardFocus;
                    capturedPointerNode = nearestButton;
                    clearImeFocus();
                    routedEvent.consumeInputTransition();
                    static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
                }
            }
        } else if (input.kind == UIRoutedPointerEventKind::Wheel && !routedEvent.isDefaultActionPrevented())
        {
            for (const u32 routeEntryIndex : routePathScratch)
            {
                if (routeEntryIndex >= entries.size())
                {
                    continue;
                }
                const UINodeId routeNode = entries[routeEntryIndex].node;
                if (!isLiveScrollable(routeNode) || !isNodeEnabled(routeNode))
                {
                    continue;
                }
                auto applied = applyScrollWheel(routeNode, input.delta);
                if (!applied)
                {
                    return Core::failure(applied.error());
                }
                if (*applied)
                {
                    routedEvent.consumeInputTransition();
                    break;
                }
            }
        } else if (input.kind == UIRoutedPointerEventKind::Move && hadArmedScrollView &&
                   armedScrollView == armedScrollViewAtRouteStart)
        {
            if (!isLiveScrollable(armedScrollViewAtRouteStart) || !isNodeEnabled(armedScrollViewAtRouteStart))
            {
                clearArmedScrollView();
                if (capturedPointerNode == armedScrollViewAtRouteStart)
                {
                    capturedPointerNode = {};
                }
            } else
            {
                static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
                if (scrollThumbDragAtRouteStart)
                {
                    auto applied = applyScrollThumbFromPointer(armedScrollViewAtRouteStart,
                                                               armedScrollAxisAtRouteStart, input.position,
                                                               scrollDragGrabOffsetAtRouteStart);
                    if (!applied)
                    {
                        return Core::failure(applied.error());
                    }
                }
            }
        } else if (input.kind == UIRoutedPointerEventKind::Move && hadArmedSlider &&
                   armedSlider == armedSliderAtRouteStart)
        {
            if (!isNodeEnabled(armedSliderAtRouteStart))
            {
                clearArmedSlider();
            } else
            {
                static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
                if (auto applied = applySliderValueFromPointer(armedSliderAtRouteStart, input.position,
                                                               input.platformFrame, input.sourceSequence);
                    !applied)
                {
                    return Core::failure(applied.error());
                }
            }
        } else if (input.kind == UIRoutedPointerEventKind::Move && hadArmedTextEdit &&
                   armedTextEdit == armedTextEditAtRouteStart)
        {
            if (!isNodeEnabled(armedTextEditAtRouteStart))
            {
                clearArmedTextEdit();
            } else
            {
                if (Core::Status selection =
                        updateTextEditSelectionFromPointer(armedTextEditAtRouteStart, input.position, true);
                    !selection)
                {
                    return Core::failure(selection.error());
                }
                static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            }
        } else if (input.kind == UIRoutedPointerEventKind::Move && hadArmedInteraction &&
                   armedPrimaryButton == armedButtonAtRouteStart)
        {
            if (!isNodeEnabled(armedButtonAtRouteStart))
            {
                clearArmedPrimaryButton();
            } else
            {
                if (armedPrimaryButtonPressed != pointWithinArmedButton)
                {
                    if (Core::Status dirty = markPaintDirty(armedButtonAtRouteStart); !dirty)
                    {
                        return Core::failure(dirty.error());
                    }
                    armedPrimaryButtonPressed = pointWithinArmedButton;
                }
                static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            }
        } else if (primaryButtonUp && hadArmedScrollView)
        {
            const bool scrollViewStillArmed = armedScrollView == armedScrollViewAtRouteStart;
            Core::Status releasePaint = Core::success();
            if (scrollViewStillArmed && scrollThumbDragAtRouteStart && contains(armedScrollViewAtRouteStart))
            {
                releasePaint = markPaintDirty(armedScrollViewAtRouteStart);
            }
            clearArmedScrollView();
            if (!hoverPaintStatus)
            {
                return Core::failure(hoverPaintStatus.error());
            }
            if (!releasePaint)
            {
                return Core::failure(releasePaint.error());
            }
            routedEvent.consumeInputTransition();
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            if (scrollViewStillArmed && scrollThumbDragAtRouteStart &&
                isLiveScrollable(armedScrollViewAtRouteStart) && isNodeEnabled(armedScrollViewAtRouteStart) &&
                !routedEvent.isDefaultActionPrevented())
            {
                auto applied = applyScrollThumbFromPointer(armedScrollViewAtRouteStart,
                                                           armedScrollAxisAtRouteStart, input.position,
                                                           scrollDragGrabOffsetAtRouteStart);
                if (!applied)
                {
                    return Core::failure(applied.error());
                }
            }
        } else if (primaryButtonUp && hadArmedSlider)
        {
            const bool sliderStillArmed = armedSlider == armedSliderAtRouteStart;
            Core::Status releasePaint = Core::success();
            if (sliderStillArmed && contains(armedSliderAtRouteStart))
            {
                releasePaint = markPaintDirty(armedSliderAtRouteStart);
            }
            clearArmedSlider();
            if (!hoverPaintStatus)
            {
                return Core::failure(hoverPaintStatus.error());
            }
            if (!releasePaint)
            {
                return Core::failure(releasePaint.error());
            }
            routedEvent.consumeInputTransition();
            if (sliderStillArmed && isNodeEnabled(armedSliderAtRouteStart) && !routedEvent.isDefaultActionPrevented())
            {
                if (auto applied = applySliderValueFromPointer(armedSliderAtRouteStart, input.position,
                                                               input.platformFrame, input.sourceSequence);
                    !applied)
                {
                    return Core::failure(applied.error());
                }
            }
        } else if (primaryButtonUp && hadArmedTextEdit)
        {
            const bool textEditStillArmed = armedTextEdit == armedTextEditAtRouteStart;
            clearArmedTextEdit();
            if (!hoverPaintStatus)
            {
                return Core::failure(hoverPaintStatus.error());
            }
            routedEvent.consumeInputTransition();
            if (textEditStillArmed && isNodeEnabled(armedTextEditAtRouteStart))
            {
                if (Core::Status selection =
                        updateTextEditSelectionFromPointer(armedTextEditAtRouteStart, input.position, true);
                    !selection)
                {
                    return Core::failure(selection.error());
                }
            }
        } else if (primaryButtonUp && hadArmedInteraction)
        {
            const bool interactionStillArmed = armedPrimaryButton == armedButtonAtRouteStart;
            Core::Status releasePaint = Core::success();
            if (interactionStillArmed && contains(armedButtonAtRouteStart))
            {
                releasePaint = markPaintDirty(armedButtonAtRouteStart);
            }
            clearArmedPrimaryButton();
            if (!hoverPaintStatus)
            {
                return Core::failure(hoverPaintStatus.error());
            }
            if (!releasePaint)
            {
                return Core::failure(releasePaint.error());
            }
            routedEvent.consumeInputTransition();
            if (!routedEvent.isDefaultActionPrevented() && interactionStillArmed && pointWithinArmedButton &&
                isNodeEnabled(armedButtonAtRouteStart))
            {
                if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                    armedRecord != nullptr && armedRecord->kind == UIWidgetKind::Checkbox &&
                    armedButtonAtRouteStart.index() < checkboxCheckedByNodeIndex.size())
                {
                    if (Core::Status dirty = markPaintDirty(armedButtonAtRouteStart); !dirty)
                    {
                        return Core::failure(dirty.error());
                    }
                    checkboxCheckedByNodeIndex[armedButtonAtRouteStart.index()] =
                        checkboxCheckedByNodeIndex[armedButtonAtRouteStart.index()] == 0 ? 1 : 0;
                }
                if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                    armedRecord != nullptr && armedRecord->kind == UIWidgetKind::RadioButton)
                {
                    if (Core::Status selected = applyRadioButtonSelection(armedButtonAtRouteStart, true); !selected)
                    {
                        return Core::failure(selected.error());
                    }
                }
                if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                    armedRecord != nullptr && (armedRecord->kind == UIWidgetKind::Dropdown ||
                                               armedRecord->kind == UIWidgetKind::DropdownItem))
                {
                    if (auto activated = activateDropdownControl(armedButtonAtRouteStart); !activated)
                    {
                        return Core::failure(activated.error());
                    }
                }
                if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                    armedRecord != nullptr && armedRecord->kind == UIWidgetKind::ListViewItem)
                {
                    if (Core::Status selected = selectCommittedListViewItem(armedButtonAtRouteStart); !selected)
                    {
                        return Core::failure(selected.error());
                    }
                }
                if (const NodeRecord* armedRecord = nodes.tryGet(armedButtonAtRouteStart.storageId());
                    armedRecord != nullptr && armedRecord->kind == UIWidgetKind::TreeViewItem)
                {
                    Core::Status treeAction = armedTreeDisclosureAtRouteStart
                                                  ? pointWithinArmedTreeDisclosure
                                                        ? toggleCommittedTreeViewItem(armedButtonAtRouteStart)
                                                        : Core::success()
                                                  : selectCommittedTreeViewItem(armedButtonAtRouteStart);
                    if (!treeAction)
                    {
                        return Core::failure(treeAction.error());
                    }
                }
                invokeButtonAction(actionCandidate,
                                   UIButtonActionEvent{
                                       .buttonNode = armedButtonAtRouteStart,
                                       .source = UIButtonActivationSource::PrimaryPointer,
                                       .platformFrame = input.platformFrame,
                                       .sourceSequence = input.sourceSequence,
                                   },
                                   currentButtonRouteSerial);
            }
        }

        const bool releaseCaptureRequested =
            Detail::UIRoutedPointerEventAccess::pointerCaptureReleaseRequested(routedEvent);
        const UINodeId requestedCapture = Detail::UIRoutedPointerEventAccess::pointerCaptureRequest(routedEvent);
        if (primaryButtonUp || releaseCaptureRequested)
        {
            capturedPointerNode = {};
        } else if (requestedCapture.hasValue() &&
                   isPointerCaptureCandidate(requestedCapture, entries, committedActiveModalEntryIndex))
        {
            capturedPointerNode = requestedCapture;
        }

        if (result.pointQuery.modalBarrierActive)
        {
            routedEvent.consumeInputTransition();
            static_cast<void>(routedEvent.claimPointerButton(Platform::PointerButton::Primary));
            if ((input.kind == UIRoutedPointerEventKind::ButtonDown ||
                 input.kind == UIRoutedPointerEventKind::ButtonUp) &&
                input.button < Platform::PointerButton::Count)
            {
                static_cast<void>(routedEvent.claimPointerButton(input.button));
            }
        }

        result.claimedPointerButtons = Detail::UIRoutedPointerEventAccess::claimedPointerButtons(routedEvent);
        result.consumed = routedEvent.isInputTransitionConsumed();
        result.stopped = routedEvent.isPropagationStopped();
        result.pointerCaptureChanged = capturedPointerNode != captureAtRouteStart;
        return result;
    }

    [[nodiscard]] Core::Status cancelPointerInteraction(Platform::WindowId routedWindow)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!routedWindow.hasValue())
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI Pointer interaction cancellation requires a Window");
        }
        if (routedWindow != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI Pointer interaction cancellation belongs to another Window");
        }
        const UINodeId cancelledButton = armedPrimaryButton;
        const UINodeId cancelledSlider = armedSlider;
        const UINodeId cancelledScrollView = armedScrollView;
        const UINodeId cancelledFocus = defaultActionFocusButton;
        const UINodeId cancelledHover = hoveredPrimaryButton;
        clearArmedPrimaryButton();
        clearArmedSlider();
        clearArmedScrollView();
        clearArmedTextEdit();
        capturedPointerNode = {};
        popupDismissPointerBarrierActive = false;
        dropdownCommandPressLatch.clear();
        listViewCommandPressLatch.clear();
        treeViewCommandPressLatch.clear();
        defaultActionPressState.clearAll();
        clearImeFocus();
        clearDefaultActionFocus();
        clearHoveredPrimaryButton();
        // Cancellation is a state barrier: a full dirty queue must not leave
        // any pointer interaction armed. Existing dirty work will rebuild the
        // paint/semantics snapshot; otherwise this best-effort mark schedules
        // the cleared control state for the next commit.
        static_cast<void>(markPaintDirtyBatch({
            cancelledButton,
            cancelledSlider,
            cancelledScrollView,
            cancelledFocus,
            cancelledHover,
        }));
        return Core::success();
    }

    [[nodiscard]] Core::Status cancelDefaultActionInteraction(Platform::WindowId routedWindow,
                                                              std::optional<Platform::GamepadId> gamepad)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!routedWindow.hasValue())
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI default-action cancellation requires a Window");
        }
        if (routedWindow != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI default-action cancellation belongs to another Window");
        }

        dropdownCommandPressLatch.clear();
        listViewCommandPressLatch.clear();
        treeViewCommandPressLatch.clear();

        if (gamepad.has_value())
        {
            const UINodeId cancelledTarget = defaultActionPressState.pressedTarget(*gamepad);
            defaultActionPressState.clearGamepad(*gamepad);
            if (cancelledTarget.hasValue() && contains(cancelledTarget) && !isButtonPressed(cancelledTarget))
            {
                static_cast<void>(markPaintDirty(cancelledTarget));
            }
            return Core::success();
        }

        const UINodeId cancelledTarget = defaultActionFocusButton;
        defaultActionPressState.clearAll();
        if (cancelledTarget.hasValue() && contains(cancelledTarget))
        {
            static_cast<void>(markPaintDirty(cancelledTarget));
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIDefaultActionResult>
    routeDefaultActionActivate(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                               UIButtonActivationSource source, std::optional<Platform::DigitalControlIdentity> control)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI default-action activation cannot nest during routing");
        }
        drainDeferredRootDestroys();
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput,
                        "UI default-action activation requires a platform frame and sequence");
        }
        if (source != UIButtonActivationSource::Keyboard && source != UIButtonActivationSource::Gamepad &&
            source != UIButtonActivationSource::Accessibility)
        {
            return fail(UIErrorCode::InvalidButtonAction,
                        "UI default-action activation source must be Keyboard, Gamepad, or Accessibility");
        }
        if (control.has_value())
        {
            if (Core::Status validControl = defaultActionPressState.validateControl(source, *control);
                !validControl)
            {
                return Core::failure(validControl.error());
            }
            const UINodeId existingTarget = defaultActionPressState.pressedTarget(*control);
            if (existingTarget.hasValue())
            {
                const NodeRecord* existingRecord =
                    contains(existingTarget) ? nodes.tryGet(existingTarget.storageId()) : nullptr;
                if ((existingTarget == defaultActionFocusButton &&
                     isCommittedKeyboardFocusCandidate(existingTarget)) ||
                    (existingRecord != nullptr && existingRecord->kind == UIWidgetKind::DropdownItem))
                {
                    // Native key repeat and duplicate gamepad Down remain owned
                    // by UI without re-running toggle/callback side effects.
                    return UIDefaultActionResult{
                        .consumed = true,
                        .activated = false,
                    };
                }
                defaultActionPressState.clearPressedTarget(*control);
            }
        }
        if (!isCommittedKeyboardFocusCandidate(defaultActionFocusButton))
        {
            if (textInputFocus == defaultActionFocusButton)
            {
                clearImeFocus();
            } else
            {
                clearDefaultActionFocus();
            }
            return UIDefaultActionResult{};
        }
        const NodeRecord* record = nodes.tryGet(defaultActionFocusButton.storageId());
        if (record != nullptr && record->kind == UIWidgetKind::TextEdit)
        {
            if (textInputFocus != defaultActionFocusButton)
            {
                clearDefaultActionFocus();
                return UIDefaultActionResult{};
            }
            return UIDefaultActionResult{.consumed = true, .activated = false};
        }
        if (record == nullptr || !isDefaultActivatableKind(record->kind))
        {
            clearDefaultActionFocus();
            return UIDefaultActionResult{};
        }

        if (buttonRouteSerial == (std::numeric_limits<u64>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI Button route serial is exhausted");
        }
        const UINodeId activationTarget = defaultActionFocusButton;
        const bool pressedStateChanges = control.has_value() && !isButtonPressed(activationTarget);
        releaseRouteDirtyQueueReservations();
        auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
        if (pressedStateChanges || record->kind == UIWidgetKind::Checkbox)
        {
            addRouteDirtyReservationCandidate(activationTarget);
        }
        if (record->kind == UIWidgetKind::RadioButton)
        {
            const NodeRecord* parent = recordByIndex(record->parentIndex);
            if (parent == nullptr)
            {
                return fail(UIErrorCode::InvalidParent, "UI RadioButton requires a live parent group");
            }
            for (u32 childIndex = parent->firstChildIndex; childIndex != InvalidNodeIndex;)
            {
                const NodeRecord* child = recordByIndex(childIndex);
                if (child == nullptr)
                {
                    return fail(Core::CoreErrorCode::Internal, "UI RadioButton group is invalid");
                }
                const u32 nextSiblingIndex = child->nextSiblingIndex;
                if (child->kind == UIWidgetKind::RadioButton && childIndex < radioButtonStatesByNodeIndex.size() &&
                    radioButtonStatesByNodeIndex[childIndex].selected != (childIndex == activationTarget.index()))
                {
                    addRouteDirtyReservationCandidate(idForIndex(childIndex));
                }
                childIndex = nextSiblingIndex;
            }
        }
        addDropdownActivationDirtyReservationCandidates(activationTarget);
        if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
        {
            return Core::failure(reservation.error());
        }
        if (Core::Status dirty = preflightDefaultActionActivationDirty(activationTarget, pressedStateChanges); !dirty)
        {
            return Core::failure(dirty.error());
        }
        if (pressedStateChanges)
        {
            if (Core::Status dirty = markPaintDirty(activationTarget); !dirty)
            {
                return Core::failure(dirty.error());
            }
        }
        if (record->kind == UIWidgetKind::Checkbox && activationTarget.index() < checkboxCheckedByNodeIndex.size())
        {
            if (Core::Status dirty = markPaintDirty(activationTarget); !dirty)
            {
                return Core::failure(dirty.error());
            }
            checkboxCheckedByNodeIndex[activationTarget.index()] =
                checkboxCheckedByNodeIndex[activationTarget.index()] == 0 ? 1 : 0;
        }
        if (record->kind == UIWidgetKind::RadioButton)
        {
            if (Core::Status selected = applyRadioButtonSelection(activationTarget, true); !selected)
            {
                return Core::failure(selected.error());
            }
        }
        bool dropdownActivated = false;
        if (record->kind == UIWidgetKind::Dropdown || record->kind == UIWidgetKind::DropdownItem)
        {
            auto activated = activateDropdownControl(activationTarget);
            if (!activated)
            {
                return Core::failure(activated.error());
            }
            dropdownActivated = *activated;
        }
        if (control.has_value())
        {
            defaultActionPressState.setPressedTarget(*control, activationTarget);
        }
        const u64 actionRegistrationSerialBoundary = buttonActionRegistry.registrationSerial();
        const Detail::UIButtonActionInvocation actionCandidate =
            captureButtonAction(activationTarget, actionRegistrationSerialBoundary);
        if (!actionCandidate.hasValue())
        {
            // Focused control without a registered action still consumes Accept
            // so gameplay does not also fire. Selection controls already
            // changed state above; a bare Button without a callback did not.
            const bool activated = record->kind == UIWidgetKind::Checkbox || record->kind == UIWidgetKind::RadioButton ||
                                   dropdownActivated;
            return UIDefaultActionResult{.consumed = true, .activated = activated};
        }
        const u64 currentButtonRouteSerial = ++buttonRouteSerial;
        ++routeDispatchDepth;
        auto dispatchCleanup = Core::makeScopeExit([this]() noexcept { finishRoutedPointerDispatch(); });
        invokeButtonAction(actionCandidate,
                           UIButtonActionEvent{
                               .buttonNode = activationTarget,
                               .source = source,
                               .platformFrame = platformFrame,
                               .sourceSequence = sourceSequence,
                           },
                           currentButtonRouteSerial);
        return UIDefaultActionResult{.consumed = true, .activated = true};
    }

    [[nodiscard]] Core::Status performAccessibilityAction(const UIAccessibilityAction& action)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI accessibility action cannot nest during routing");
        }
        drainDeferredRootDestroys();
        auto nodeResult = resolveNode(action.node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        NodeRecord* record = *nodeResult;
        if (!isNodeEnabled(action.node))
        {
            return fail(UIErrorCode::InvalidAccessibilityAction,
                        "UI accessibility action target is disabled");
        }

        switch (action.kind)
        {
        case UIAccessibilityActionKind::Focus:
            return requestFocus(action.node);
        case UIAccessibilityActionKind::Invoke:
            if (record->kind != UIWidgetKind::Button)
            {
                return fail(UIErrorCode::InvalidAccessibilityAction,
                            "UI accessibility Invoke requires a Button");
            }
            break;
        case UIAccessibilityActionKind::Toggle:
            if (record->kind != UIWidgetKind::Checkbox && record->kind != UIWidgetKind::RadioButton)
            {
                return fail(UIErrorCode::InvalidAccessibilityAction,
                            "UI accessibility Toggle requires a Checkbox or RadioButton");
            }
            break;
        case UIAccessibilityActionKind::SetRangeValue: {
            if (record->kind != UIWidgetKind::Slider || !std::isfinite(action.rangeValue) ||
                action.rangeValue < static_cast<double>(sliderStatesByNodeIndex[action.node.index()].minValue) ||
                action.rangeValue > static_cast<double>(sliderStatesByNodeIndex[action.node.index()].maxValue) ||
                action.rangeValue < static_cast<double>((std::numeric_limits<float>::lowest)()) ||
                action.rangeValue > static_cast<double>((std::numeric_limits<float>::max)()))
            {
                return fail(UIErrorCode::InvalidAccessibilityAction,
                            "UI accessibility range value is invalid for the target Slider");
            }
            const UINodeId root = idForIndex(record->rootIndex);
            return setSliderValueFromUpdater(root, action.node, static_cast<float>(action.rangeValue));
        }
        case UIAccessibilityActionKind::SetTextValue: {
            if (record->kind != UIWidgetKind::TextEdit)
            {
                return fail(UIErrorCode::InvalidAccessibilityAction,
                            "UI accessibility text value requires a TextEdit");
            }
            const UINodeId root = idForIndex(record->rootIndex);
            return setTextFromUpdater(root, action.node, action.textValue);
        }
        }

        if (accessibilityActionSequence == (std::numeric_limits<u64>::max)())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI accessibility action sequence is exhausted");
        }
        if (Core::Status focus = requestFocus(action.node); !focus)
        {
            return focus;
        }
        auto activated = routeDefaultActionActivate(Platform::PlatformFrameId{1}, ++accessibilityActionSequence,
                                                    UIButtonActivationSource::Accessibility, std::nullopt);
        if (!activated)
        {
            return Core::failure(activated.error());
        }
        if (!activated->consumed)
        {
            return fail(UIErrorCode::InvalidAccessibilityAction,
                        "UI accessibility action target could not be activated");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Result<UIDefaultActionResult>
    routeDefaultActionRelease(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                              UIButtonActivationSource source, const Platform::DigitalControlIdentity& control)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput,
                        "UI default-action release requires a platform frame and sequence");
        }
        if (Core::Status validControl = defaultActionPressState.validateControl(source, control);
            !validControl)
        {
            return Core::failure(validControl.error());
        }

        const UINodeId releasedTarget = defaultActionPressState.pressedTarget(control);
        if (!releasedTarget.hasValue())
        {
            return UIDefaultActionResult{};
        }

        defaultActionPressState.clearPressedTarget(control);
        if (contains(releasedTarget) && !isButtonPressed(releasedTarget))
        {
            // Physical Up cannot be replayed. Keep release as a successful
            // input barrier even when repaint capacity is temporarily full.
            static_cast<void>(markPaintDirty(releasedTarget));
        }
        return UIDefaultActionResult{
            .consumed = true,
            .activated = false,
        };
    }

    [[nodiscard]] Core::Status applyExplicitFocus(UINodeId nextFocus)
    {
        const UINodeId previousFocus = defaultActionFocusButton;
        const UINodeId previousTextFocus = textInputFocus;
        if (previousFocus == nextFocus &&
            ((nextFocus.hasValue() && isLiveTextEdit(nextFocus)) ? previousTextFocus == nextFocus
                                                                 : !previousTextFocus.hasValue()))
        {
            return Core::success();
        }

        if (Core::Status dirty = preflightPaintDirtyBatch({
                previousFocus,
                previousTextFocus,
                nextFocus,
            });
            !dirty)
        {
            return dirty;
        }
        if (Core::Status dirty = markPaintDirtyBatch({
                previousFocus,
                previousTextFocus,
                nextFocus,
            });
            !dirty)
        {
            return dirty;
        }

        defaultActionPressState.clearAll();
        if (previousTextFocus != nextFocus)
        {
            resetImeCompositionState();
        }
        defaultActionFocusButton = nextFocus;
        const NodeRecord* nextRecord =
            nextFocus.hasValue() && contains(nextFocus) ? nodes.tryGet(nextFocus.storageId()) : nullptr;
        textInputFocus = nextRecord != nullptr && nextRecord->kind == UIWidgetKind::TextEdit ? nextFocus : UINodeId{};
        return Core::success();
    }

    [[nodiscard]] Core::Status requestFocus(UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI focus cannot be changed during pointer routing");
        }
        drainDeferredRootDestroys();
        auto nodeResult = resolveNode(node);
        if (!nodeResult)
        {
            return Core::failure(nodeResult.error());
        }
        if (!isCommittedKeyboardFocusCandidate(node))
        {
            return fail(UIErrorCode::InvalidFocusTarget,
                        "UI focus target is not an enabled committed keyboard-focus candidate");
        }
        return applyExplicitFocus(node);
    }

    [[nodiscard]] Core::Status clearFocus()
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return ownerThread;
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI focus cannot be cleared during pointer routing");
        }
        drainDeferredRootDestroys();
        return applyExplicitFocus({});
    }

    [[nodiscard]] Core::Status requestFocusFromUpdater(UINodeId updaterRoot, UINodeId node)
    {
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!isNodeWithinRoot(updaterRoot, node))
        {
            return fail(UIErrorCode::InvalidNode, "UI focus target is not owned by the updater root");
        }
        return requestFocus(node);
    }

    [[nodiscard]] Core::Status clearFocusFromUpdater(UINodeId updaterRoot)
    {
        if (!updaterRoot.hasValue() || !contains(updaterRoot))
        {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (defaultActionFocusButton.hasValue() && !isNodeWithinRoot(updaterRoot, defaultActionFocusButton))
        {
            return Core::success();
        }
        return clearFocus();
    }

    [[nodiscard]] UINodeId activeFocusScope() const noexcept
    {
        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        const u32 focusEntryIndex = findHitEntryIndex(defaultActionFocus(), entries);
        if (focusEntryIndex < entries.size())
        {
            const u32 scopeEntryIndex = entries[focusEntryIndex].focusScopeEntryIndex;
            if (scopeEntryIndex < entries.size())
            {
                return entries[scopeEntryIndex].node;
            }
        }
        return activeModalNode;
    }

    [[nodiscard]] UINodeId activeModal() const noexcept
    {
        return activeModalNode;
    }

    [[nodiscard]] UINodeId pointerCapture() const noexcept
    {
        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        return isPointerCaptureCandidate(capturedPointerNode, entries, committedActiveModalEntryIndex)
                   ? capturedPointerNode
                   : UINodeId{};
    }

    [[nodiscard]] UINodeId activePopup() const noexcept
    {
        if (!activePopupNode.hasValue() || !contains(activePopupNode) ||
            activePopupNode.index() >= popupStatesByNodeIndex.size() ||
            !popupStatesByNodeIndex[activePopupNode.index()].open)
        {
            return {};
        }
        const NodeRecord* record = nodes.tryGet(activePopupNode.storageId());
        return record != nullptr && record->kind == UIWidgetKind::Popup ? activePopupNode : UINodeId{};
    }

    [[nodiscard]] Core::Result<UIDropdownCommandResult> routeDropdownCommand(UIDropdownCommand command,
                                                                             bool pressed)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI Dropdown command cannot run during pointer routing");
        }
        drainDeferredRootDestroys();

        if (!dropdownCommandPressLatch.accepts(command))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI Dropdown command is not recognized");
        }

        if (!pressed)
        {
            return UIDropdownCommandResult{
                .consumed = dropdownCommandPressLatch.release(command),
                .changed = false,
                .focus = defaultActionFocus(),
            };
        }
        if (dropdownCommandPressLatch.isLatched(command))
        {
            return UIDropdownCommandResult{
                .consumed = true,
                .changed = false,
                .focus = defaultActionFocus(),
            };
        }

        const UINodeId popup = activePopup();
        const UINodeId dropdown = dropdownForPopup(popup);
        if (!popup.hasValue() || !dropdown.hasValue())
        {
            return UIDropdownCommandResult{};
        }

        if (command == UIDropdownCommand::Dismiss)
        {
            if (Core::Status closed = setPopupOpenState(popup, false); !closed)
            {
                return Core::failure(closed.error());
            }
            dropdownCommandPressLatch.latch(command);
            return UIDropdownCommandResult{
                .consumed = true,
                .changed = true,
                .focus = defaultActionFocus(),
            };
        }

        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        if (command == UIDropdownCommand::PreviousItem || command == UIDropdownCommand::NextItem)
        {
            const bool next = command == UIDropdownCommand::NextItem;
            u32 firstCandidate = InvalidUIHitEntryIndex;
            u32 lastCandidate = InvalidUIHitEntryIndex;
            u32 previousCandidate = InvalidUIHitEntryIndex;
            u32 nextCandidate = InvalidUIHitEntryIndex;
            u32 selectedCandidate = InvalidUIHitEntryIndex;
            bool currentFound = false;
            for (u32 entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
            {
                const UICommittedHitEntry& entry = entries[entryIndex];
                if (!contains(entry.node) || entry.kind != UIWidgetKind::DropdownItem ||
                    dropdownForItem(entry.node) != dropdown || !isNodeEnabled(entry.node) ||
                    entry.policy != UIPointerHitPolicy::Targetable ||
                    !hitEntryAllowedByModal(entry, committedActiveModalEntryIndex))
                {
                    continue;
                }
                if (firstCandidate == InvalidUIHitEntryIndex)
                {
                    firstCandidate = entryIndex;
                }
                lastCandidate = entryIndex;
                if (entry.node == dropdownStatesByNodeIndex[dropdown.index()].selectedItem)
                {
                    selectedCandidate = entryIndex;
                }
                if (currentFound && nextCandidate == InvalidUIHitEntryIndex)
                {
                    nextCandidate = entryIndex;
                }
                if (entry.node == defaultActionFocusButton)
                {
                    currentFound = true;
                } else if (!currentFound)
                {
                    previousCandidate = entryIndex;
                }
            }

            u32 targetEntryIndex = InvalidUIHitEntryIndex;
            if (!currentFound)
            {
                targetEntryIndex = selectedCandidate != InvalidUIHitEntryIndex
                                       ? selectedCandidate
                                       : (next ? firstCandidate : lastCandidate);
            } else if (next)
            {
                targetEntryIndex = nextCandidate != InvalidUIHitEntryIndex
                                       ? nextCandidate
                                       : findHitEntryIndex(defaultActionFocusButton, entries);
            } else
            {
                targetEntryIndex = previousCandidate != InvalidUIHitEntryIndex
                                       ? previousCandidate
                                       : findHitEntryIndex(defaultActionFocusButton, entries);
            }

            bool changed = false;
            if (targetEntryIndex < entries.size())
            {
                const UINodeId nextFocus = entries[targetEntryIndex].node;
                changed = nextFocus != defaultActionFocusButton;
                if (changed)
                {
                    if (Core::Status focused = applyExplicitFocus(nextFocus); !focused)
                    {
                        return Core::failure(focused.error());
                    }
                }
            }
            dropdownCommandPressLatch.latch(command);
            return UIDropdownCommandResult{
                .consumed = true,
                .changed = changed,
                .focus = defaultActionFocus(),
            };
        }

        const u32 dropdownEntryIndex = findHitEntryIndex(dropdown, entries);
        u32 firstCandidate = InvalidUIHitEntryIndex;
        u32 lastCandidate = InvalidUIHitEntryIndex;
        u32 previousCandidate = InvalidUIHitEntryIndex;
        u32 nextCandidate = InvalidUIHitEntryIndex;
        if (dropdownEntryIndex < entries.size())
        {
            const u32 scopeEntryIndex = entries[dropdownEntryIndex].focusScopeEntryIndex;
            for (u32 entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
            {
                const UICommittedHitEntry& entry = entries[entryIndex];
                if (!contains(entry.node) || entry.node == dropdown || isNodeWithinSubtree(popup, entry.node) ||
                    !isNodeEnabled(entry.node) || entry.policy != UIPointerHitPolicy::Targetable ||
                    !isKeyboardFocusableKind(entry.kind) ||
                    !hitEntryAllowedByModal(entry, committedActiveModalEntryIndex) ||
                    !hitEntryIsWithinScope(entryIndex, scopeEntryIndex, entries))
                {
                    continue;
                }
                if (firstCandidate == InvalidUIHitEntryIndex)
                {
                    firstCandidate = entryIndex;
                }
                lastCandidate = entryIndex;
                if (entryIndex < dropdownEntryIndex)
                {
                    previousCandidate = entryIndex;
                } else if (entryIndex > dropdownEntryIndex && nextCandidate == InvalidUIHitEntryIndex)
                {
                    nextCandidate = entryIndex;
                }
            }
        }

        const bool exitNext = command == UIDropdownCommand::ExitNext;
        const u32 exitEntryIndex = exitNext
                                       ? (nextCandidate != InvalidUIHitEntryIndex ? nextCandidate : firstCandidate)
                                       : (previousCandidate != InvalidUIHitEntryIndex ? previousCandidate : lastCandidate);
        const UINodeId exitFocus = exitEntryIndex < entries.size() ? entries[exitEntryIndex].node : dropdown;
        const UINodeId previousFocus = defaultActionFocusButton;
        releaseRouteDirtyQueueReservations();
        auto reservationCleanup = Core::makeScopeExit([this]() noexcept { releaseRouteDirtyQueueReservations(); });
        addRouteLayoutDirtyReservationCandidates(popup);
        addRouteLayoutDirtyReservationCandidates(dropdown);
        addRouteDirtyReservationCandidate(previousFocus);
        addRouteDirtyReservationCandidate(textInputFocus);
        addRouteDirtyReservationCandidate(exitFocus);
        if (Core::Status reservation = reserveRouteDirtyQueueSlots(); !reservation)
        {
            return Core::failure(reservation.error());
        }
        if (Core::Status closed = setPopupOpenState(popup, false); !closed)
        {
            return Core::failure(closed.error());
        }
        if (defaultActionFocusButton != exitFocus)
        {
            if (Core::Status focused = applyExplicitFocus(exitFocus); !focused)
            {
                return Core::failure(focused.error());
            }
        }
        dropdownCommandPressLatch.latch(command);
        return UIDropdownCommandResult{
            .consumed = true,
            .changed = true,
            .focus = defaultActionFocus(),
        };
    }

    [[nodiscard]] Core::Result<UIListViewCommandResult> routeListViewCommand(UIListViewCommand command,
                                                                             bool pressed)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI ListView command cannot run during pointer routing");
        }
        drainDeferredRootDestroys();

        if (!listViewCommandPressLatch.accepts(command))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI ListView command is not recognized");
        }
        if (!pressed)
        {
            return UIListViewCommandResult{
                .consumed = listViewCommandPressLatch.release(command)};
        }
        if (listViewCommandPressLatch.isLatched(command))
        {
            return UIListViewCommandResult{.consumed = true};
        }

        UINodeId listView = defaultActionFocusButton;
        const NodeRecord* focusRecord =
            contains(listView) ? nodes.tryGet(listView.storageId()) : nullptr;
        if (focusRecord != nullptr && focusRecord->kind == UIWidgetKind::ListViewItem)
        {
            listView = listViewForItem(listView);
            focusRecord = contains(listView) ? nodes.tryGet(listView.storageId()) : nullptr;
        }
        if (focusRecord == nullptr || focusRecord->kind != UIWidgetKind::ListView || !isNodeEnabled(listView))
        {
            return UIListViewCommandResult{};
        }

        ListViewState& state = listViewStatesByNodeIndex[listView.index()];
        const u64 itemCount = state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
        const auto consumeWithoutChange = [&]() noexcept {
            listViewCommandPressLatch.latch(command);
            return UIListViewCommandResult{
                .consumed = true,
                .selection = state.selection,
            };
        };
        if (command == UIListViewCommand::Activate)
        {
            listViewCommandPressLatch.latch(command);
            return UIListViewCommandResult{
                .consumed = true,
                .changed = false,
                .activated = state.selection.hasValue(),
                .selection = state.selection,
            };
        }
        if (itemCount == 0)
        {
            return consumeWithoutChange();
        }

        const u64 pageItems = (std::max)(
            u64{1}, static_cast<u64>(std::floor(state.committedMetrics.viewportSize.height / state.style.rowHeight)));
        u64 candidate = 0;
        switch (command)
        {
        case UIListViewCommand::PreviousItem:
            candidate = state.selection.hasValue() && state.selection.logicalIndex != 0
                            ? state.selection.logicalIndex - 1
                            : 0;
            break;
        case UIListViewCommand::NextItem:
            candidate = state.selection.hasValue()
                            ? (std::min)(itemCount - 1, state.selection.logicalIndex + 1)
                            : 0;
            break;
        case UIListViewCommand::PreviousPage:
            candidate = state.selection.hasValue() && state.selection.logicalIndex > pageItems
                            ? state.selection.logicalIndex - pageItems
                            : 0;
            break;
        case UIListViewCommand::NextPage:
            candidate = state.selection.hasValue()
                            ? (std::min)(itemCount - 1, state.selection.logicalIndex + pageItems)
                            : 0;
            break;
        case UIListViewCommand::FirstItem:
            candidate = 0;
            break;
        case UIListViewCommand::LastItem:
            candidate = itemCount - 1;
            break;
        case UIListViewCommand::Activate:
            break;
        }

        const bool searchBackward = command == UIListViewCommand::PreviousItem ||
                                    command == UIListViewCommand::PreviousPage ||
                                    command == UIListViewCommand::LastItem;
        UIListViewItemDescriptor descriptor{};
        bool found = false;
        for (u64 visited = 0; visited < itemCount; ++visited)
        {
            auto resolved = resolveListViewLogicalItem(listView, candidate);
            if (!resolved)
            {
                return Core::failure(resolved.error());
            }
            if (resolved->enabled)
            {
                descriptor = *resolved;
                found = true;
                break;
            }
            if (searchBackward)
            {
                if (candidate == 0)
                {
                    break;
                }
                --candidate;
            } else
            {
                if (candidate + 1 >= itemCount)
                {
                    break;
                }
                ++candidate;
            }
        }
        if (!found)
        {
            return consumeWithoutChange();
        }

        const UIListViewSelection nextSelection{.key = descriptor.key, .logicalIndex = candidate};
        const bool changed = state.selection != nextSelection;
        if (changed)
        {
            if (Core::Status dirty = markPaintDirty(listView); !dirty)
            {
                return Core::failure(dirty.error());
            }
            state.selection = nextSelection;
        }
        const UINodeId updaterRoot = idForIndex(focusRecord->rootIndex);
        if (Core::Status revealed = scrollListViewToIndexFromUpdater(
                updaterRoot, listView, candidate, UIListViewScrollAlignment::Nearest);
            !revealed)
        {
            return Core::failure(revealed.error());
        }
        listViewCommandPressLatch.latch(command);
        return UIListViewCommandResult{
            .consumed = true,
            .changed = changed,
            .activated = false,
            .selection = state.selection,
        };
    }

    [[nodiscard]] Core::Result<UITreeViewCommandResult> routeTreeViewCommand(UITreeViewCommand command, bool pressed)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI TreeView command cannot run during pointer routing");
        }
        drainDeferredRootDestroys();

        if (!treeViewCommandPressLatch.accepts(command))
        {
            return fail(UIErrorCode::InvalidControlValue, "UI TreeView command is not recognized");
        }
        if (!pressed)
        {
            return UITreeViewCommandResult{
                .consumed = treeViewCommandPressLatch.release(command)};
        }
        if (treeViewCommandPressLatch.isLatched(command))
        {
            return UITreeViewCommandResult{.consumed = true};
        }

        UINodeId treeView = defaultActionFocusButton;
        const NodeRecord* focusRecord = contains(treeView) ? nodes.tryGet(treeView.storageId()) : nullptr;
        if (focusRecord != nullptr && focusRecord->kind == UIWidgetKind::TreeViewItem)
        {
            treeView = treeViewForItem(treeView);
            focusRecord = contains(treeView) ? nodes.tryGet(treeView.storageId()) : nullptr;
        }
        if (focusRecord == nullptr || focusRecord->kind != UIWidgetKind::TreeView || !isNodeEnabled(treeView))
        {
            return UITreeViewCommandResult{};
        }

        TreeViewState& state = treeViewStatesByNodeIndex[treeView.index()];
        const u64 itemCount = state.dataSource.hasValue() ? state.dataSource.itemCount(state.dataSource.state) : 0;
        const auto consumeWithoutChange = [&]() noexcept {
            treeViewCommandPressLatch.latch(command);
            return UITreeViewCommandResult{
                .consumed = true,
                .selection = state.selection,
            };
        };
        u64 selectedIndex = 0;
        UITreeViewItemDescriptor selectedDescriptor{};
        bool hasSelectedItem = false;
        if (state.selection.hasValue())
        {
            if (state.selection.logicalIndex < itemCount)
            {
                auto resolved = resolveTreeViewLogicalItem(treeView, state.selection.logicalIndex);
                if (!resolved)
                {
                    return Core::failure(resolved.error());
                }
                if (resolved->key == state.selection.key)
                {
                    selectedIndex = state.selection.logicalIndex;
                    selectedDescriptor = *resolved;
                    hasSelectedItem = true;
                }
            }
            if (!hasSelectedItem)
            {
                for (u64 logicalIndex = 0; logicalIndex < itemCount; ++logicalIndex)
                {
                    auto resolved = resolveTreeViewLogicalItem(treeView, logicalIndex);
                    if (!resolved)
                    {
                        return Core::failure(resolved.error());
                    }
                    if (resolved->key == state.selection.key)
                    {
                        selectedIndex = logicalIndex;
                        selectedDescriptor = *resolved;
                        hasSelectedItem = true;
                        break;
                    }
                }
            }
        }

        if (command == UITreeViewCommand::Activate)
        {
            if (!hasSelectedItem || !selectedDescriptor.enabled)
            {
                return consumeWithoutChange();
            }
            treeViewCommandPressLatch.latch(command);
            return UITreeViewCommandResult{
                .consumed = true,
                .activated = true,
                .selection = state.selection,
            };
        }
        if (itemCount == 0)
        {
            return consumeWithoutChange();
        }

        const UINodeId updaterRoot = idForIndex(focusRecord->rootIndex);
        auto applyExpansion = [&](bool expanded) -> Core::Result<UITreeViewCommandResult> {
            if (!hasSelectedItem || !selectedDescriptor.enabled || !selectedDescriptor.expandable)
            {
                return consumeWithoutChange();
            }
            if (Core::Status changed =
                    setTreeViewItemExpandedFromUpdater(updaterRoot, treeView, selectedIndex, expanded);
                !changed)
            {
                return Core::failure(changed.error());
            }
            treeViewCommandPressLatch.latch(command);
            return UITreeViewCommandResult{
                .consumed = true,
                .changed = true,
                .expansionChanged = true,
                .selection = state.selection,
            };
        };

        if (command == UITreeViewCommand::ToggleExpanded)
        {
            return applyExpansion(!selectedDescriptor.expanded);
        }
        if (command == UITreeViewCommand::CollapseOrParent && hasSelectedItem && selectedDescriptor.enabled &&
            selectedDescriptor.expandable && selectedDescriptor.expanded)
        {
            return applyExpansion(false);
        }
        if (command == UITreeViewCommand::ExpandOrFirstChild && hasSelectedItem && selectedDescriptor.enabled &&
            selectedDescriptor.expandable && !selectedDescriptor.expanded)
        {
            return applyExpansion(true);
        }

        u64 candidate = 0;
        UITreeViewItemDescriptor candidateDescriptor{};
        bool found = false;
        bool searchBackward = false;
        switch (command)
        {
        case UITreeViewCommand::PreviousItem:
        case UITreeViewCommand::PreviousPage:
        case UITreeViewCommand::NextItem:
        case UITreeViewCommand::NextPage:
        case UITreeViewCommand::FirstItem:
        case UITreeViewCommand::LastItem: {
            const u64 pageItems =
                (std::max)(u64{1}, static_cast<u64>(
                                       std::floor(state.committedMetrics.viewportSize.height / state.style.rowHeight)));
            switch (command)
            {
            case UITreeViewCommand::PreviousItem:
                candidate = hasSelectedItem && selectedIndex != 0 ? selectedIndex - 1 : 0;
                break;
            case UITreeViewCommand::NextItem:
                candidate = hasSelectedItem ? (std::min)(itemCount - 1, selectedIndex + 1) : 0;
                break;
            case UITreeViewCommand::PreviousPage:
                candidate = hasSelectedItem && selectedIndex > pageItems ? selectedIndex - pageItems : 0;
                break;
            case UITreeViewCommand::NextPage:
                candidate = hasSelectedItem ? (std::min)(itemCount - 1, selectedIndex + pageItems) : 0;
                break;
            case UITreeViewCommand::FirstItem:
                candidate = 0;
                break;
            case UITreeViewCommand::LastItem:
                candidate = itemCount - 1;
                break;
            default:
                break;
            }
            searchBackward = command == UITreeViewCommand::LastItem ||
                             (hasSelectedItem && (command == UITreeViewCommand::PreviousItem ||
                                                  command == UITreeViewCommand::PreviousPage));
            for (u64 visited = 0; visited < itemCount; ++visited)
            {
                auto resolved = resolveTreeViewLogicalItem(treeView, candidate);
                if (!resolved)
                {
                    return Core::failure(resolved.error());
                }
                if (resolved->enabled)
                {
                    candidateDescriptor = *resolved;
                    found = true;
                    break;
                }
                if (searchBackward)
                {
                    if (candidate == 0)
                    {
                        break;
                    }
                    --candidate;
                } else
                {
                    if (candidate + 1 >= itemCount)
                    {
                        break;
                    }
                    ++candidate;
                }
            }
            break;
        }
        case UITreeViewCommand::CollapseOrParent: {
            if (!hasSelectedItem || selectedDescriptor.level == 0)
            {
                return consumeWithoutChange();
            }
            u32 ancestorLevel = selectedDescriptor.level;
            for (u64 logicalIndex = selectedIndex; logicalIndex-- > 0;)
            {
                auto resolved = resolveTreeViewLogicalItem(treeView, logicalIndex);
                if (!resolved)
                {
                    return Core::failure(resolved.error());
                }
                if (resolved->level >= ancestorLevel)
                {
                    continue;
                }
                ancestorLevel = resolved->level;
                if (resolved->enabled)
                {
                    candidate = logicalIndex;
                    candidateDescriptor = *resolved;
                    found = true;
                    break;
                }
                if (ancestorLevel == 0)
                {
                    break;
                }
            }
            break;
        }
        case UITreeViewCommand::ExpandOrFirstChild: {
            if (!hasSelectedItem)
            {
                return consumeWithoutChange();
            }
            const u64 childLevel = static_cast<u64>(selectedDescriptor.level) + 1;
            for (u64 logicalIndex = selectedIndex + 1; logicalIndex < itemCount; ++logicalIndex)
            {
                auto resolved = resolveTreeViewLogicalItem(treeView, logicalIndex);
                if (!resolved)
                {
                    return Core::failure(resolved.error());
                }
                if (resolved->level <= selectedDescriptor.level)
                {
                    break;
                }
                if (static_cast<u64>(resolved->level) == childLevel && resolved->enabled)
                {
                    candidate = logicalIndex;
                    candidateDescriptor = *resolved;
                    found = true;
                    break;
                }
            }
            break;
        }
        case UITreeViewCommand::ToggleExpanded:
        case UITreeViewCommand::Activate:
            break;
        }

        if (!found)
        {
            return consumeWithoutChange();
        }
        const UITreeViewSelection nextSelection{
            .key = candidateDescriptor.key,
            .logicalIndex = candidate,
            .level = candidateDescriptor.level,
        };
        const bool changed = state.selection != nextSelection;
        if (changed)
        {
            if (Core::Status dirty = markPaintDirty(treeView); !dirty)
            {
                return Core::failure(dirty.error());
            }
            state.selection = nextSelection;
        }
        if (Core::Status revealed =
                scrollTreeViewToIndexFromUpdater(updaterRoot, treeView, candidate, UITreeViewScrollAlignment::Nearest);
            !revealed)
        {
            return Core::failure(revealed.error());
        }
        treeViewCommandPressLatch.latch(command);
        return UITreeViewCommandResult{
            .consumed = true,
            .changed = changed,
            .selection = state.selection,
        };
    }

    [[nodiscard]] Core::Result<UIDefaultFocusStepResult> routeDefaultActionFocusStep(bool reverse)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        if (routeDispatchDepth != 0)
        {
            return fail(UIErrorCode::PointerRouteAlreadyInProgress,
                        "UI focus traversal cannot run during pointer routing");
        }
        drainDeferredRootDestroys();

        const auto& entries = committedHitBuffers[publishedHitBufferIndex];
        u32 scopeEntryIndex = committedActiveModalEntryIndex;
        const u32 currentFocusEntryIndex = findHitEntryIndex(defaultActionFocusButton, entries);
        if (currentFocusEntryIndex < entries.size() &&
            entries[currentFocusEntryIndex].focusScopeEntryIndex < entries.size())
        {
            scopeEntryIndex = entries[currentFocusEntryIndex].focusScopeEntryIndex;
        }

        u32 firstCandidateEntryIndex = InvalidUIHitEntryIndex;
        u32 lastCandidateEntryIndex = InvalidUIHitEntryIndex;
        u32 previousCandidateEntryIndex = InvalidUIHitEntryIndex;
        u32 nextCandidateEntryIndex = InvalidUIHitEntryIndex;
        bool currentFocusFound = false;
        usize candidateCount = 0;
        for (u32 entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
        {
            const UICommittedHitEntry& entry = entries[entryIndex];
            if (!contains(entry.node) || !isNodeEnabled(entry.node) || entry.policy != UIPointerHitPolicy::Targetable ||
                !isKeyboardFocusableKind(entry.kind) ||
                !hitEntryAllowedByModal(entry, committedActiveModalEntryIndex) ||
                !hitEntryIsWithinScope(entryIndex, scopeEntryIndex, entries))
            {
                continue;
            }

            ++candidateCount;
            if (firstCandidateEntryIndex == InvalidUIHitEntryIndex)
            {
                firstCandidateEntryIndex = entryIndex;
            }
            lastCandidateEntryIndex = entryIndex;
            if (currentFocusFound && nextCandidateEntryIndex == InvalidUIHitEntryIndex)
            {
                nextCandidateEntryIndex = entryIndex;
            }
            if (entry.node == defaultActionFocusButton)
            {
                currentFocusFound = true;
            } else if (!currentFocusFound)
            {
                previousCandidateEntryIndex = entryIndex;
            }
        }
        if (candidateCount == 0)
        {
            if (Core::Status cleared = applyExplicitFocus({}); !cleared)
            {
                return Core::failure(cleared.error());
            }
            return UIDefaultFocusStepResult{};
        }

        const u32 nextEntryIndex =
            currentFocusFound
                ? (reverse ? (previousCandidateEntryIndex != InvalidUIHitEntryIndex ? previousCandidateEntryIndex
                                                                                    : lastCandidateEntryIndex)
                           : (nextCandidateEntryIndex != InvalidUIHitEntryIndex ? nextCandidateEntryIndex
                                                                                : firstCandidateEntryIndex))
                : (reverse ? lastCandidateEntryIndex : firstCandidateEntryIndex);
        const UINodeId nextFocus = entries[nextEntryIndex].node;
        const UINodeId previousFocus = defaultActionFocusButton;
        const bool moved = !defaultActionFocusButton.hasValue() || defaultActionFocusButton != nextFocus;
        const UINodeId dirtyPreviousFocus =
            previousFocus.hasValue() && previousFocus != nextFocus && contains(previousFocus) ? previousFocus
                                                                                              : UINodeId{};
        const UINodeId dirtyTextFocus =
            textInputFocus.hasValue() && textInputFocus != nextFocus && contains(textInputFocus) ? textInputFocus
                                                                                                 : UINodeId{};
        const UINodeId dirtyArmedSlider =
            armedSlider.hasValue() && armedSlider != nextFocus && contains(armedSlider) ? armedSlider : UINodeId{};
        if (Core::Status dirty = markPaintDirtyBatch({
                dirtyPreviousFocus,
                dirtyTextFocus,
                dirtyArmedSlider,
                nextFocus,
            });
            !dirty)
        {
            return Core::failure(dirty.error());
        }
        defaultActionPressState.clearAll();
        defaultActionFocusButton = nextFocus;
        // Tab navigation does not keep a live pointer arm.
        clearArmedPrimaryButton();
        clearArmedSlider();
        clearArmedTextEdit();
        capturedPointerNode = {};
        const NodeRecord* nextRecord = nodes.tryGet(nextFocus.storageId());
        if (nextRecord != nullptr && nextRecord->kind == UIWidgetKind::TextEdit)
        {
            if (textInputFocus != nextFocus)
            {
                clearImeFocus();
                textInputFocus = nextFocus;
            }
        } else
        {
            clearImeFocus();
        }
        return UIDefaultFocusStepResult{
            .consumed = true,
            .moved = moved,
            .focus = nextFocus,
        };
    }

    [[nodiscard]] UINodeId defaultActionFocus() const noexcept
    {
        if (!isCommittedKeyboardFocusCandidate(defaultActionFocusButton))
        {
            return {};
        }
        const NodeRecord* record = nodes.tryGet(defaultActionFocusButton.storageId());
        if (record != nullptr && record->kind == UIWidgetKind::TextEdit && textInputFocus != defaultActionFocusButton)
        {
            return {};
        }
        return defaultActionFocusButton;
    }

    [[nodiscard]] UINodeId imeFocus() const noexcept
    {
        if (!isCommittedTextEditFocusCandidate(textInputFocus) || defaultActionFocusButton != textInputFocus)
        {
            return {};
        }
        return textInputFocus;
    }

    [[nodiscard]] bool imeCompositionActive() const noexcept
    {
        return imeComposition.active() && isCommittedTextEditFocusCandidate(textInputFocus) &&
               defaultActionFocusButton == textInputFocus;
    }

    [[nodiscard]] std::string_view imePreeditUtf8() const noexcept
    {
        return imeComposition.preeditUtf8();
    }

    [[nodiscard]] u32 imePreeditCursorCodepoint() const noexcept
    {
        return imeComposition.cursorCodepoint();
    }

    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextComposition(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                         std::string_view preeditUtf8, u32 cursorCodepoint, Platform::TextCompositionStage stage)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!window.hasValue() || window != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI text composition belongs to another owner window");
        }
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI text composition requires a platform frame and sequence");
        }

        using Stage = Platform::TextCompositionStage;
        const bool knownStage =
            stage == Stage::Started || stage == Stage::Updated || stage == Stage::Ended || stage == Stage::Cancelled;
        if (!knownStage)
        {
            return fail(UIErrorCode::InvalidText, "UI text composition stage is not recognized");
        }
        if (!isCommittedTextEditFocusCandidate(textInputFocus))
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }

        if (stage == Stage::Cancelled || stage == Stage::Ended)
        {
            // clearImeComposition marks paint dirty when preedit was active.
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            return UITextInputRouteResult{.consumed = true, .applied = true};
        }
        if (!Core::isStrictUtf8WithoutNul(preeditUtf8))
        {
            return fail(UIErrorCode::InvalidText, "UI IME preedit must be strict UTF-8 without embedded NUL");
        }
        if (containsLineBreak(preeditUtf8))
        {
            return fail(UIErrorCode::InvalidText, "UI TextEdit preedit accepts one logical line without CR or LF");
        }
        if (Core::Status capacityStatus = Detail::UIImeCompositionState::validateCapacity(preeditUtf8);
            !capacityStatus)
        {
            return Core::failure(capacityStatus.error());
        }
        const auto codepoints = Core::countStrictUtf8CodepointsWithoutNul(preeditUtf8);
        if (!codepoints.has_value())
        {
            return fail(UIErrorCode::InvalidText, "UI IME preedit must be strict UTF-8 without embedded NUL");
        }
        if (Core::Status paintStatus = markPaintDirty(textInputFocus); !paintStatus)
        {
            return Core::failure(paintStatus.error());
        }
        imeComposition.assign(preeditUtf8, cursorCodepoint, *codepoints);
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }

    [[nodiscard]] Core::Result<UITextInputRouteResult> routeTextInput(Platform::WindowId window,
                                                                      Platform::PlatformFrameId platformFrame,
                                                                      u64 sourceSequence,
                                                                      std::string_view committedUtf8)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!window.hasValue() || window != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI text input belongs to another owner window");
        }
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI text input requires a platform frame and sequence");
        }
        if (!isCommittedTextEditFocusCandidate(textInputFocus))
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }
        if (!Core::isStrictUtf8WithoutNul(committedUtf8))
        {
            return fail(UIErrorCode::InvalidText, "UI text input must be strict UTF-8 without embedded NUL");
        }
        if (containsLineBreak(committedUtf8))
        {
            return UITextInputRouteResult{.consumed = true, .applied = false};
        }
        if (committedUtf8.empty())
        {
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            return UITextInputRouteResult{.consumed = true, .applied = true};
        }

        const UINodeId focusedTextEdit = textInputFocus;
        const NodeRecord* record = nodes.tryGet(focusedTextEdit.storageId());
        if (record == nullptr)
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }
        const UINodeId rootNode = idForIndex(record->rootIndex);
        if (!rootNode.hasValue())
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }

        const std::string_view current = textViewFor(focusedTextEdit.index());
        const UITextSelection selection = textEditStatesByNodeIndex[focusedTextEdit.index()].selection;
        const u32 selectionBegin = (std::min)(selection.anchorCodepoint, selection.caretCodepoint);
        const u32 selectionEnd = (std::max)(selection.anchorCodepoint, selection.caretCodepoint);
        const usize selectionBeginByte = utf8ByteOffsetForCodepoint(current, selectionBegin);
        const usize selectionEndByte = utf8ByteOffsetForCodepoint(current, selectionEnd);
        const usize retainedBytes = current.size() - (selectionEndByte - selectionBeginByte);
        if (retainedBytes > (std::numeric_limits<usize>::max)() - committedUtf8.size())
        {
            return fail(UIErrorCode::CapacityExceeded, "UI text input would overflow the text byte capacity");
        }
        std::string combined;
        try
        {
            // One-shot commit allocation; not a per-frame hot path.
            combined.reserve(retainedBytes + committedUtf8.size());
            combined.append(current.substr(0, selectionBeginByte));
            combined.append(committedUtf8);
            combined.append(current.substr(selectionEndByte));
        } catch (const std::bad_alloc&)
        {
            return fail(Core::CoreErrorCode::OutOfMemory, "UI text input scratch allocation failed");
        }
        if (Core::Status status = setTextFromUpdater(rootNode, focusedTextEdit, combined); !status)
        {
            return Core::failure(status.error());
        }
        if (Core::Status dirty = markPaintDirty(focusedTextEdit); !dirty)
        {
            return Core::failure(dirty.error());
        }
        const auto insertedCodepoints = Core::countStrictUtf8CodepointsWithoutNul(committedUtf8);
        const u32 nextCaret = selectionBegin + insertedCodepoints.value_or(0U);
        textEditStatesByNodeIndex[focusedTextEdit.index()].selection = {
            .anchorCodepoint = nextCaret,
            .caretCodepoint = nextCaret,
        };
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }

    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextEditCommand(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                         UITextEditCommand command, bool extendSelection)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread)
        {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (!window.hasValue() || window != ownerWindow)
        {
            return fail(UIErrorCode::WrongOwnerWindow, "UI TextEdit command belongs to another owner window");
        }
        if (!platformFrame.hasValue() || sourceSequence == 0)
        {
            return fail(UIErrorCode::InvalidPointerInput, "UI TextEdit command requires a platform frame and sequence");
        }
        if (!isCommittedTextEditFocusCandidate(textInputFocus))
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }

        const UINodeId focusedTextEdit = textInputFocus;
        const NodeRecord* record = nodes.tryGet(focusedTextEdit.storageId());
        if (record == nullptr)
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }
        const UINodeId rootNode = idForIndex(record->rootIndex);
        if (!rootNode.hasValue())
        {
            clearImeFocus();
            return UITextInputRouteResult{};
        }

        TextEditState& editState = textEditStatesByNodeIndex[focusedTextEdit.index()];
        const u32 codepointCount = textStatesByIndex[focusedTextEdit.index()].metrics.codepointCount;
        const UITextSelection currentSelection = editState.selection;
        const u32 selectionBegin = (std::min)(currentSelection.anchorCodepoint, currentSelection.caretCodepoint);
        const u32 selectionEnd = (std::max)(currentSelection.anchorCodepoint, currentSelection.caretCodepoint);

        UITextSelection nextSelection = currentSelection;
        bool deletesText = false;
        u32 deleteBegin = selectionBegin;
        u32 deleteEnd = selectionEnd;
        switch (command)
        {
        case UITextEditCommand::MoveLeft: {
            const u32 nextCaret = !extendSelection && !currentSelection.isCollapsed() ? selectionBegin
                                  : currentSelection.caretCodepoint > 0 ? currentSelection.caretCodepoint - 1U
                                                                        : 0U;
            nextSelection.caretCodepoint = nextCaret;
            if (!extendSelection)
            {
                nextSelection.anchorCodepoint = nextCaret;
            }
            break;
        }
        case UITextEditCommand::MoveRight: {
            const u32 nextCaret = !extendSelection && !currentSelection.isCollapsed()
                                      ? selectionEnd
                                      : (std::min)(currentSelection.caretCodepoint + 1U, codepointCount);
            nextSelection.caretCodepoint = nextCaret;
            if (!extendSelection)
            {
                nextSelection.anchorCodepoint = nextCaret;
            }
            break;
        }
        case UITextEditCommand::MoveHome:
            nextSelection.caretCodepoint = 0;
            if (!extendSelection)
            {
                nextSelection.anchorCodepoint = 0;
            }
            break;
        case UITextEditCommand::MoveEnd:
            nextSelection.caretCodepoint = codepointCount;
            if (!extendSelection)
            {
                nextSelection.anchorCodepoint = codepointCount;
            }
            break;
        case UITextEditCommand::SelectAll:
            nextSelection = {
                .anchorCodepoint = 0,
                .caretCodepoint = codepointCount,
            };
            break;
        case UITextEditCommand::Backspace:
            deletesText = true;
            if (currentSelection.isCollapsed())
            {
                deleteBegin = currentSelection.caretCodepoint > 0 ? currentSelection.caretCodepoint - 1U : 0U;
                deleteEnd = currentSelection.caretCodepoint;
            }
            break;
        case UITextEditCommand::Delete:
            deletesText = true;
            if (currentSelection.isCollapsed())
            {
                deleteBegin = currentSelection.caretCodepoint;
                deleteEnd = (std::min)(currentSelection.caretCodepoint + 1U, codepointCount);
            }
            break;
        default:
            return fail(UIErrorCode::InvalidText, "UI TextEdit command is not recognized");
        }

        if (!deletesText)
        {
            if (nextSelection == currentSelection)
            {
                if (Core::Status status = clearImeComposition(); !status)
                {
                    return Core::failure(status.error());
                }
                return UITextInputRouteResult{.consumed = true, .applied = false};
            }
            if (Core::Status paintStatus = markPaintDirty(focusedTextEdit); !paintStatus)
            {
                return Core::failure(paintStatus.error());
            }
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            editState.selection = nextSelection;
            return UITextInputRouteResult{.consumed = true, .applied = true};
        }

        if (deleteBegin == deleteEnd)
        {
            if (Core::Status status = clearImeComposition(); !status)
            {
                return Core::failure(status.error());
            }
            return UITextInputRouteResult{.consumed = true, .applied = false};
        }
        const std::string_view current = textViewFor(focusedTextEdit.index());
        const usize deleteBeginByte = utf8ByteOffsetForCodepoint(current, deleteBegin);
        const usize deleteEndByte = utf8ByteOffsetForCodepoint(current, deleteEnd);
        std::string combined;
        try
        {
            combined.reserve(current.size() - (deleteEndByte - deleteBeginByte));
            combined.append(current.substr(0, deleteBeginByte));
            combined.append(current.substr(deleteEndByte));
        } catch (const std::bad_alloc&)
        {
            return fail(Core::CoreErrorCode::OutOfMemory, "UI TextEdit command scratch allocation failed");
        }
        if (Core::Status status = setTextFromUpdater(rootNode, focusedTextEdit, combined); !status)
        {
            return Core::failure(status.error());
        }
        editState.selection = {
            .anchorCodepoint = deleteBegin,
            .caretCodepoint = deleteBegin,
        };
        if (Core::Status status = clearImeComposition(); !status)
        {
            return Core::failure(status.error());
        }
        return UITextInputRouteResult{.consumed = true, .applied = true};
    }

    [[nodiscard]] UIContextStatistics statistics() const noexcept
    {
        return UIContextStatistics{
            .nodeCapacity = capacityConfig.nodeCapacity,
            .rootCapacity = capacityConfig.rootCapacity,
            .dirtyQueueCapacity = capacityConfig.dirtyQueueCapacity,
            .layoutSnapshotCapacity = capacityConfig.layoutSnapshotCapacity,
            .hitSnapshotCapacity = capacityConfig.hitSnapshotCapacity,
            .paintSnapshotCapacity = capacityConfig.paintSnapshotCapacity,
            .routePathCapacity = capacityConfig.routePathCapacity,
            .routedPointerListenerCapacity = capacityConfig.routedPointerListenerCapacity,
            .activeRoutedPointerListenerCount = routedPointerListenerRegistry.activeCount(),
            .routedPointerListenerHighWater = routedPointerListenerRegistry.highWater(),
            .buttonActionCapacity = capacityConfig.buttonActionCapacity,
            .activeButtonActionCount = buttonActionRegistry.activeCount(),
            .buttonActionHighWater = buttonActionRegistry.highWater(),
            .textByteCapacity = textStorage.capacity(),
            .textByteUsed = textStorage.used(),
            .textByteHighWater = textStorage.highWater(),
            .liveNodeCount = nodes.activeCount(),
            .liveRootCount = liveRootCount,
            .committedNodeCount = committedBuffers[publishedBufferIndex].size(),
            .committedRevision = committedRevision,
            .committedLayoutNodeCount = committedLayoutBuffers[publishedLayoutBufferIndex].size(),
            .layoutRevision = committedLayoutRevision,
            .committedHitNodeCount = committedHitBuffers[publishedHitBufferIndex].size(),
            .committedHitTargetCount = committedHitTargetCount,
            .hitRevision = committedHitRevision,
            .paintOrderRevision = committedHitPaintOrderRevision,
            .committedPaintNodeCount = committedPaintBuffers[publishedPaintBufferIndex].size(),
            .paintRevision = committedPaintRevision,
            .committedSemanticsNodeCount = committedSemanticsBuffers[publishedSemanticsBufferIndex].size(),
            .semanticsRevision = committedSemanticsRevision,
            .structureDirty = isPhaseDirty(PhaseStructure),
            .layoutDirty = isPhaseDirty(PhaseLayout),
            .hitDirty = isPhaseDirty(PhaseHit),
            .paintDirty = isPhaseDirty(PhasePaint),
            .semanticsDirty = isPhaseDirty(PhaseSemantics),
            .lastLayoutPassCount = lastLayoutPass.passCount,
            .lastLayoutMeasuredNodeCount = lastLayoutPass.measuredNodeCount,
            .lastLayoutArrangedNodeCount = lastLayoutPass.arrangedNodeCount,
            .lastLayoutPercentMeasureFallbackCount = lastLayoutPass.percentMeasureFallbackCount,
            .lastHitRebuildCount = lastHitRebuildCount,
            .lastPaintCacheRebuildCount = lastPaintCacheRebuildCount,
            .lastPaintSnapshotRebuildCount = lastPaintSnapshotRebuildCount,
            .dirtyQueuePendingCount = validDirtyQueueCount(),
            .dirtyQueueHighWater = dirtyQueueHighWater,
        };
    }
};

UIRoutedPointerListenerToken::UIRoutedPointerListenerToken(std::weak_ptr<Detail::UIContextLifetimeControl> lifetime,
                                                           u32 slot, u32 generation) noexcept
    : m_lifetime(std::move(lifetime)), m_slot(slot), m_generation(generation)
{
}

UIRoutedPointerListenerToken::~UIRoutedPointerListenerToken() noexcept
{
    reset();
}

UIRoutedPointerListenerToken::UIRoutedPointerListenerToken(UIRoutedPointerListenerToken&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime)), m_slot(std::exchange(other.m_slot, 0)),
      m_generation(std::exchange(other.m_generation, 0))
{
}

UIRoutedPointerListenerToken& UIRoutedPointerListenerToken::operator=(UIRoutedPointerListenerToken&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_slot = std::exchange(other.m_slot, 0);
    m_generation = std::exchange(other.m_generation, 0);
    return *this;
}

void UIRoutedPointerListenerToken::reset() noexcept
{
    const u32 generation = m_generation;
    if (generation == 0)
    {
        m_lifetime.reset();
        m_slot = 0;
        return;
    }

    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    UIContext* immediateContext =
        lifetime ? lifetime->releaseRoutedPointerListener(m_slot, generation)
                 : nullptr;

    if (immediateContext != nullptr)
    {
        immediateContext->releaseRoutedPointerListenerFromToken(m_slot, generation);
    }
    m_lifetime.reset();
    m_slot = 0;
    m_generation = 0;
}

bool UIRoutedPointerListenerToken::isActive() const noexcept
{
    if (m_generation == 0)
    {
        return false;
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    if (!lifetime)
    {
        return false;
    }
    return lifetime->isRoutedPointerListenerActive(m_slot, m_generation);
}

UIRoutedPointerListenerToken::operator bool() const noexcept
{
    return isActive();
}

UIRootOwner::UIRootOwner(std::weak_ptr<Detail::UIContextLifetimeControl> lifetime, UINodeId root) noexcept
    : m_lifetime(std::move(lifetime)), m_root(root)
{
}

UIRootOwner::~UIRootOwner() noexcept
{
    reset();
}

UIRootOwner::UIRootOwner(UIRootOwner&& other) noexcept : m_lifetime(std::move(other.m_lifetime)), m_root(other.m_root)
{
    other.m_root = {};
}

UIRootOwner& UIRootOwner::operator=(UIRootOwner&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_root = other.m_root;
    other.m_root = {};
    return *this;
}

void UIRootOwner::reset() noexcept
{
    const UINodeId root = m_root;
    if (!root.hasValue())
    {
        m_lifetime.reset();
        return;
    }

    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    if (!lifetime)
    {
        m_root = {};
        m_lifetime.reset();
        return;
    }

    UIContext* context = lifetime->releaseRoot(root);

    if (context != nullptr)
    {
        context->destroyRootFromOwner(root);
    }
    m_root = {};
    m_lifetime.reset();
}

UINodeId UIRootOwner::rootNodeId() const noexcept
{
    return m_root;
}

bool UIRootOwner::hasValue() const noexcept
{
    return m_root.hasValue();
}

UIRootOwner::operator bool() const noexcept
{
    return hasValue();
}

UIRootBuilder::UIRootBuilder(UIContext& context) noexcept : m_context(&context)
{
}

Core::Result<UIRootOwner> UIRootBuilder::createRoot()
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createRoot();
}

Core::Result<UINodeId> UIRootBuilder::createPanel(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Panel);
}

Core::Result<UINodeId> UIRootBuilder::createLabel(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Label);
}

Core::Result<UINodeId> UIRootBuilder::createButton(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Button);
}

Core::Result<UINodeId> UIRootBuilder::createCheckbox(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Checkbox);
}

Core::Result<UINodeId> UIRootBuilder::createSlider(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Slider);
}

Core::Result<UINodeId> UIRootBuilder::createTextEdit(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::TextEdit);
}

Core::Result<UINodeId> UIRootBuilder::createProgressBar(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::ProgressBar);
}

Core::Result<UINodeId> UIRootBuilder::createRadioButton(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::RadioButton);
}

Core::Result<UINodeId> UIRootBuilder::createModal(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Modal);
}

Core::Result<UINodeId> UIRootBuilder::createScrollView(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::ScrollView);
}

Core::Result<UINodeId> UIRootBuilder::createDropdown(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Dropdown);
}

Core::Result<UINodeId> UIRootBuilder::createPopup(UINodeId dropdown)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(dropdown, UIWidgetKind::Popup);
}

Core::Result<UINodeId> UIRootBuilder::createDropdownItem(UINodeId popup)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(popup, UIWidgetKind::DropdownItem);
}

Core::Result<UINodeId> UIRootBuilder::createListView(UINodeId parent, UIListViewCreateConfig config)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createListViewChild(parent, config);
}

Core::Result<UINodeId> UIRootBuilder::createTreeView(UINodeId parent, UITreeViewCreateConfig config)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createTreeViewChild(parent, config);
}

UITreeUpdater::UITreeUpdater(UIContext& context, UINodeId root) noexcept : m_context(&context), m_root(root)
{
}

UITreeUpdater::UITreeUpdater(UITreeUpdater&& other) noexcept
    : m_context(std::exchange(other.m_context, nullptr)), m_root(std::exchange(other.m_root, {}))
{
}

UITreeUpdater& UITreeUpdater::operator=(UITreeUpdater&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    m_context = std::exchange(other.m_context, nullptr);
    m_root = std::exchange(other.m_root, {});
    return *this;
}

Core::Result<UINodeId> UITreeUpdater::createPanel(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Panel);
}

Core::Result<UINodeId> UITreeUpdater::createLabel(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Label);
}

Core::Result<UINodeId> UITreeUpdater::createButton(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Button);
}

Core::Result<UINodeId> UITreeUpdater::createCheckbox(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Checkbox);
}

Core::Result<UINodeId> UITreeUpdater::createSlider(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Slider);
}

Core::Result<UINodeId> UITreeUpdater::createTextEdit(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::TextEdit);
}

Core::Result<UINodeId> UITreeUpdater::createProgressBar(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::ProgressBar);
}

Core::Result<UINodeId> UITreeUpdater::createRadioButton(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::RadioButton);
}

Core::Result<UINodeId> UITreeUpdater::createModal(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Modal);
}

Core::Result<UINodeId> UITreeUpdater::createScrollView(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::ScrollView);
}

Core::Result<UINodeId> UITreeUpdater::createDropdown(UINodeId parent)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, parent, UIWidgetKind::Dropdown);
}

Core::Result<UINodeId> UITreeUpdater::createPopup(UINodeId dropdown)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, dropdown, UIWidgetKind::Popup);
}

Core::Result<UINodeId> UITreeUpdater::createDropdownItem(UINodeId popup)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createChildFromUpdater(m_root, popup, UIWidgetKind::DropdownItem);
}

Core::Result<UINodeId> UITreeUpdater::createListView(UINodeId parent, UIListViewCreateConfig config)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createListViewFromUpdater(m_root, parent, config);
}

Core::Result<UINodeId> UITreeUpdater::createTreeView(UINodeId parent, UITreeViewCreateConfig config)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->createTreeViewFromUpdater(m_root, parent, config);
}

bool UITreeUpdater::isAlive(UINodeId node) const noexcept
{
    return m_context != nullptr && m_context->isAliveInRoot(m_root, node);
}

Core::Status UITreeUpdater::setLayoutStyle(UINodeId node, const UILayoutStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setLayoutStyleFromUpdater(m_root, node, style);
}

Core::Status UITreeUpdater::setPointerHitPolicy(UINodeId node, UIPointerHitPolicy policy)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setPointerHitPolicyFromUpdater(m_root, node, policy);
}

Core::Status UITreeUpdater::setEnabled(UINodeId node, bool enabled)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setEnabledFromUpdater(m_root, node, enabled);
}

Core::Result<bool> UITreeUpdater::isEnabled(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isEnabledFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::setFocusScopeMode(UINodeId node, UIFocusScopeMode mode)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setFocusScopeModeFromUpdater(m_root, node, mode);
}

Core::Result<UIFocusScopeMode> UITreeUpdater::focusScopeMode(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->focusScopeModeFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::requestFocus(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->requestFocusFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::clearFocus()
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearFocusFromUpdater(m_root);
}

Core::Status UITreeUpdater::setBoxPaint(UINodeId node, const UIBoxPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setBoxPaintFromUpdater(m_root, node, paint);
}

Core::Status UITreeUpdater::setButtonPaint(UINodeId button, const UIButtonPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setButtonPaintFromUpdater(m_root, button, paint);
}

Core::Result<UIButtonPaint> UITreeUpdater::buttonPaint(UINodeId button) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->buttonPaintFromUpdater(m_root, button);
}

Core::Status UITreeUpdater::setText(UINodeId node, std::string_view utf8)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTextFromUpdater(m_root, node, utf8);
}

Core::Status UITreeUpdater::setTextStyle(UINodeId node, const UITextStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTextStyleFromUpdater(m_root, node, style);
}

Core::Result<std::string_view> UITreeUpdater::text(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->textFromUpdater(m_root, node);
}

Core::Result<UITextStyle> UITreeUpdater::textStyle(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->textStyleFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::setTextSelection(UINodeId textEdit, UITextSelection selection)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setTextSelectionFromUpdater(m_root, textEdit, selection);
}

Core::Result<UITextSelection> UITreeUpdater::textSelection(UINodeId textEdit) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->textSelectionFromUpdater(m_root, textEdit);
}

Core::Status UITreeUpdater::setButtonAction(UINodeId button, UIButtonActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setButtonActionFromUpdater(m_root, button, std::move(callback));
}

Core::Status UITreeUpdater::clearButtonAction(UINodeId button)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearButtonActionFromUpdater(m_root, button);
}

Core::Result<bool> UITreeUpdater::isButtonPressed(UINodeId button) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isButtonPressedFromUpdater(m_root, button);
}

Core::Status UITreeUpdater::setCheckboxAction(UINodeId checkbox, UIButtonActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setCheckboxActionFromUpdater(m_root, checkbox, std::move(callback));
}

Core::Status UITreeUpdater::clearCheckboxAction(UINodeId checkbox)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearCheckboxActionFromUpdater(m_root, checkbox);
}

Core::Status UITreeUpdater::setCheckboxPaint(UINodeId checkbox, const UICheckboxPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setCheckboxPaintFromUpdater(m_root, checkbox, paint);
}

Core::Result<UICheckboxPaint> UITreeUpdater::checkboxPaint(UINodeId checkbox) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->checkboxPaintFromUpdater(m_root, checkbox);
}

Core::Status UITreeUpdater::setChecked(UINodeId checkbox, bool checked)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setCheckedFromUpdater(m_root, checkbox, checked);
}

Core::Result<bool> UITreeUpdater::isChecked(UINodeId checkbox) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isCheckedFromUpdater(m_root, checkbox);
}

Core::Result<bool> UITreeUpdater::isCheckboxPressed(UINodeId checkbox) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isCheckboxPressedFromUpdater(m_root, checkbox);
}

Core::Status UITreeUpdater::setSliderRange(UINodeId slider, float minValue, float maxValue, float step)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setSliderRangeFromUpdater(m_root, slider, minValue, maxValue, step);
}

Core::Status UITreeUpdater::setSliderValue(UINodeId slider, float value)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setSliderValueFromUpdater(m_root, slider, value);
}

Core::Result<float> UITreeUpdater::sliderValue(UINodeId slider) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->sliderValueFromUpdater(m_root, slider);
}

Core::Status UITreeUpdater::setSliderPaint(UINodeId slider, const UISliderPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setSliderPaintFromUpdater(m_root, slider, paint);
}

Core::Result<UISliderPaint> UITreeUpdater::sliderPaint(UINodeId slider) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->sliderPaintFromUpdater(m_root, slider);
}

Core::Status UITreeUpdater::setSliderChangeCallback(UINodeId slider, UISliderChangeCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setSliderChangeCallbackFromUpdater(m_root, slider, std::move(callback));
}

Core::Status UITreeUpdater::clearSliderChangeCallback(UINodeId slider)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearSliderChangeCallbackFromUpdater(m_root, slider);
}

Core::Result<bool> UITreeUpdater::isSliderDragging(UINodeId slider) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isSliderDraggingFromUpdater(m_root, slider);
}

Core::Status UITreeUpdater::setScrollViewStyle(UINodeId scrollView, const UIScrollViewStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setScrollViewStyleFromUpdater(m_root, scrollView, style);
}

Core::Result<UIScrollViewStyle> UITreeUpdater::scrollViewStyle(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->scrollViewStyleFromUpdater(m_root, scrollView);
}

Core::Status UITreeUpdater::setScrollViewOffset(UINodeId scrollView, UIScrollOffset offset)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setScrollViewOffsetFromUpdater(m_root, scrollView, offset);
}

Core::Result<UIScrollOffset> UITreeUpdater::scrollViewOffset(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->scrollViewOffsetFromUpdater(m_root, scrollView);
}

Core::Result<UIScrollViewMetrics> UITreeUpdater::scrollViewMetrics(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->scrollViewMetricsFromUpdater(m_root, scrollView);
}

Core::Status UITreeUpdater::setScrollViewPaint(UINodeId scrollView, const UIScrollViewPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setScrollViewPaintFromUpdater(m_root, scrollView, paint);
}

Core::Result<UIScrollViewPaint> UITreeUpdater::scrollViewPaint(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->scrollViewPaintFromUpdater(m_root, scrollView);
}

Core::Result<bool> UITreeUpdater::isScrollViewDragging(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isScrollViewDraggingFromUpdater(m_root, scrollView);
}

Core::Status UITreeUpdater::setPopupStyle(UINodeId popup, const UIPopupStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setPopupStyleFromUpdater(m_root, popup, style);
}

Core::Result<UIPopupStyle> UITreeUpdater::popupStyle(UINodeId popup) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->popupStyleFromUpdater(m_root, popup);
}

Core::Status UITreeUpdater::setPopupOpen(UINodeId popup, bool open)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setPopupOpenFromUpdater(m_root, popup, open);
}

Core::Result<bool> UITreeUpdater::isPopupOpen(UINodeId popup) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isPopupOpenFromUpdater(m_root, popup);
}

Core::Result<UIPopupMetrics> UITreeUpdater::popupMetrics(UINodeId popup) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->popupMetricsFromUpdater(m_root, popup);
}

Core::Status UITreeUpdater::setDropdownOpen(UINodeId dropdown, bool open)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setDropdownOpenFromUpdater(m_root, dropdown, open);
}

Core::Result<bool> UITreeUpdater::isDropdownOpen(UINodeId dropdown) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isDropdownOpenFromUpdater(m_root, dropdown);
}

Core::Status UITreeUpdater::setDropdownSelectedItem(UINodeId dropdown, UINodeId item)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setDropdownSelectedItemFromUpdater(m_root, dropdown, item);
}

Core::Result<UINodeId> UITreeUpdater::dropdownSelectedItem(UINodeId dropdown) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->dropdownSelectedItemFromUpdater(m_root, dropdown);
}

Core::Result<bool> UITreeUpdater::isDropdownItemSelected(UINodeId item) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isDropdownItemSelectedFromUpdater(m_root, item);
}

Core::Status UITreeUpdater::setDropdownPaint(UINodeId dropdown, const UIDropdownPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setDropdownPaintFromUpdater(m_root, dropdown, paint);
}

Core::Result<UIDropdownPaint> UITreeUpdater::dropdownPaint(UINodeId dropdown) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->dropdownPaintFromUpdater(m_root, dropdown);
}

Core::Status UITreeUpdater::setListViewDataSource(UINodeId listView, UIListViewDataSource source)
{
    return m_context != nullptr
               ? m_context->setListViewDataSourceFromUpdater(m_root, listView, source)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearListViewDataSource(UINodeId listView)
{
    return m_context != nullptr
               ? m_context->clearListViewDataSourceFromUpdater(m_root, listView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::invalidateListViewItems(UINodeId listView)
{
    return m_context != nullptr
               ? m_context->invalidateListViewItemsFromUpdater(m_root, listView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setListViewStyle(UINodeId listView, const UIListViewStyle& style)
{
    return m_context != nullptr
               ? m_context->setListViewStyleFromUpdater(m_root, listView, style)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIListViewStyle> UITreeUpdater::listViewStyle(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->listViewStyleFromUpdater(m_root, listView);
}

Core::Status UITreeUpdater::setListViewPaint(UINodeId listView, const UIListViewPaint& paint)
{
    return m_context != nullptr
               ? m_context->setListViewPaintFromUpdater(m_root, listView, paint)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIListViewPaint> UITreeUpdater::listViewPaint(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->listViewPaintFromUpdater(m_root, listView);
}

Core::Result<UIListViewMetrics> UITreeUpdater::listViewMetrics(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->listViewMetricsFromUpdater(m_root, listView);
}

Core::Status UITreeUpdater::setListViewSelectedIndex(UINodeId listView, u64 logicalIndex)
{
    return m_context != nullptr
               ? m_context->setListViewSelectedIndexFromUpdater(m_root, listView, logicalIndex)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearListViewSelection(UINodeId listView)
{
    return m_context != nullptr
               ? m_context->clearListViewSelectionFromUpdater(m_root, listView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIListViewSelection> UITreeUpdater::listViewSelection(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->listViewSelectionFromUpdater(m_root, listView);
}

Core::Status UITreeUpdater::scrollListViewToIndex(UINodeId listView, u64 logicalIndex,
                                                 UIListViewScrollAlignment alignment)
{
    return m_context != nullptr ? m_context->scrollListViewToIndexFromUpdater(m_root, listView, logicalIndex, alignment)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setTreeViewDataSource(UINodeId treeView, UITreeViewDataSource source)
{
    return m_context != nullptr ? m_context->setTreeViewDataSourceFromUpdater(m_root, treeView, source)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearTreeViewDataSource(UINodeId treeView)
{
    return m_context != nullptr ? m_context->clearTreeViewDataSourceFromUpdater(m_root, treeView)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::invalidateTreeViewItems(UINodeId treeView)
{
    return m_context != nullptr ? m_context->invalidateTreeViewItemsFromUpdater(m_root, treeView)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setTreeViewStyle(UINodeId treeView, const UITreeViewStyle& style)
{
    return m_context != nullptr ? m_context->setTreeViewStyleFromUpdater(m_root, treeView, style)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UITreeViewStyle> UITreeUpdater::treeViewStyle(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->treeViewStyleFromUpdater(m_root, treeView);
}

Core::Status UITreeUpdater::setTreeViewPaint(UINodeId treeView, const UITreeViewPaint& paint)
{
    return m_context != nullptr ? m_context->setTreeViewPaintFromUpdater(m_root, treeView, paint)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UITreeViewPaint> UITreeUpdater::treeViewPaint(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->treeViewPaintFromUpdater(m_root, treeView);
}

Core::Result<UITreeViewMetrics> UITreeUpdater::treeViewMetrics(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->treeViewMetricsFromUpdater(m_root, treeView);
}

Core::Status UITreeUpdater::setTreeViewSelectedIndex(UINodeId treeView, u64 logicalIndex)
{
    return m_context != nullptr ? m_context->setTreeViewSelectedIndexFromUpdater(m_root, treeView, logicalIndex)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearTreeViewSelection(UINodeId treeView)
{
    return m_context != nullptr ? m_context->clearTreeViewSelectionFromUpdater(m_root, treeView)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UITreeViewSelection> UITreeUpdater::treeViewSelection(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->treeViewSelectionFromUpdater(m_root, treeView);
}

Core::Status UITreeUpdater::setTreeViewItemExpanded(UINodeId treeView, u64 logicalIndex, bool expanded)
{
    return m_context != nullptr
               ? m_context->setTreeViewItemExpandedFromUpdater(m_root, treeView, logicalIndex, expanded)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::scrollTreeViewToIndex(UINodeId treeView, u64 logicalIndex,
                                                  UITreeViewScrollAlignment alignment)
{
    return m_context != nullptr ? m_context->scrollTreeViewToIndexFromUpdater(m_root, treeView, logicalIndex, alignment)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setProgressBarRange(UINodeId progressBar, float minValue, float maxValue)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setProgressBarRangeFromUpdater(m_root, progressBar, minValue, maxValue);
}

Core::Status UITreeUpdater::setProgressBarValue(UINodeId progressBar, float value)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setProgressBarValueFromUpdater(m_root, progressBar, value);
}

Core::Result<float> UITreeUpdater::progressBarValue(UINodeId progressBar) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->progressBarValueFromUpdater(m_root, progressBar);
}

Core::Status UITreeUpdater::setProgressBarPaint(UINodeId progressBar, const UIProgressBarPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setProgressBarPaintFromUpdater(m_root, progressBar, paint);
}

Core::Result<UIProgressBarPaint> UITreeUpdater::progressBarPaint(UINodeId progressBar) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->progressBarPaintFromUpdater(m_root, progressBar);
}

Core::Status UITreeUpdater::setRadioButtonPaint(UINodeId radioButton, const UIRadioButtonPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setRadioButtonPaintFromUpdater(m_root, radioButton, paint);
}

Core::Result<UIRadioButtonPaint> UITreeUpdater::radioButtonPaint(UINodeId radioButton) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->radioButtonPaintFromUpdater(m_root, radioButton);
}

Core::Status UITreeUpdater::setRadioButtonAction(UINodeId radioButton, UIButtonActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setRadioButtonActionFromUpdater(m_root, radioButton, std::move(callback));
}

Core::Status UITreeUpdater::clearRadioButtonAction(UINodeId radioButton)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->clearRadioButtonActionFromUpdater(m_root, radioButton);
}

Core::Status UITreeUpdater::setRadioButtonSelected(UINodeId radioButton, bool selected)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->setRadioButtonSelectedFromUpdater(m_root, radioButton, selected);
}

Core::Result<bool> UITreeUpdater::isRadioButtonSelected(UINodeId radioButton) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isRadioButtonSelectedFromUpdater(m_root, radioButton);
}

Core::Result<bool> UITreeUpdater::isRadioButtonPressed(UINodeId radioButton) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->isRadioButtonPressedFromUpdater(m_root, radioButton);
}

Core::Result<UIRoutedPointerListenerToken>
UITreeUpdater::addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor, UIRoutedPointerCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->addRoutedPointerListenerFromUpdater(m_root, descriptor, std::move(callback));
}

Core::Status UITreeUpdater::destroy(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->destroyNodeFromUpdater(m_root, node);
}

Core::Result<std::unique_ptr<UIContext>> UIContext::Create(Platform::WindowId ownerWindow,
                                                           UIContextCapacityConfig capacityConfig,
                                                           std::pmr::memory_resource& resource)
{
    // Validate before any allocation against the caller's resource so failed
    // Create remains allocation-free for invalid window/capacity probes.
    if (!ownerWindow.hasValue())
    {
        return fail(UIErrorCode::InvalidOwnerWindow, "UI context owner window id is empty");
    }
    if (Core::Status status = validateUIContextCapacityConfig(capacityConfig); !status)
    {
        return Core::failure(status.error());
    }

    // Placeholder rasterizer construction allocates against the caller's PMR and
    // must surface OOM as Result, not an uncaught bad_alloc (M10-A39 gate).
    try
    {
        auto rasterizer = createPlaceholderTextRasterizer({}, resource);
        if (!rasterizer)
        {
            return Core::failure(rasterizer.error());
        }
        return Create(ownerWindow, capacityConfig, std::move(*rasterizer), resource);
    } catch (const std::bad_alloc&)
    {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI context allocation failed");
    } catch (const std::exception& exception)
    {
        return fail(Core::CoreErrorCode::Internal, std::string_view(exception.what()));
    } catch (...)
    {
        return fail(Core::CoreErrorCode::Internal, "UI context allocation failed");
    }
}

Core::Result<std::unique_ptr<UIContext>> UIContext::Create(Platform::WindowId ownerWindow,
                                                           UIContextCapacityConfig capacityConfig,
                                                           std::unique_ptr<IUITextRasterizer> textRasterizer,
                                                           std::pmr::memory_resource& resource)
{
    if (!ownerWindow.hasValue())
    {
        return fail(UIErrorCode::InvalidOwnerWindow, "UI context owner window id is empty");
    }
    if (!textRasterizer)
    {
        return fail(UIErrorCode::InvalidFont, "UI context text rasterizer is null");
    }

    auto normalizedResult = normalizeCapacity(capacityConfig);
    if (!normalizedResult)
    {
        return Core::failure(normalizedResult.error());
    }

    try
    {
        const std::thread::id ownerThreadId = std::this_thread::get_id();
        auto lifetime = std::make_shared<Detail::UIContextLifetimeControl>(
            ownerThreadId, normalizedResult->rootCapacity, normalizedResult->routedPointerListenerCapacity);
        auto implResult = Impl::Create(ownerWindow, *normalizedResult, lifetime, resource);
        if (!implResult)
        {
            return Core::failure(implResult.error());
        }

        // Best-effort open of the built-in/empty face. Placeholder accepts {}.
        // FreeType adapters reject empty bytes; they remain without a face until
        // a later font-binding slice opens real bytes.
        auto faceResult = textRasterizer->openFace({});
        if (faceResult)
        {
            (*implResult)->textFace = *faceResult;
        }
        (*implResult)->textRasterizer = std::move(textRasterizer);

        auto atlasResult = UIGlyphAtlas::Create(
            UIGlyphAtlasCapacity{
                .width = 512,
                .height = 512,
                .maxGlyphs = 1024,
            },
            resource);
        if (atlasResult)
        {
            (*implResult)->glyphAtlas = std::move(*atlasResult);
        }

        auto context = std::unique_ptr<UIContext>(new UIContext(std::move(*implResult)));
        lifetime->attach(*context);
        return context;
    } catch (const std::bad_alloc&)
    {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI context allocation failed");
    } catch (const std::exception& exception)
    {
        return fail(Core::CoreErrorCode::Internal, std::string_view(exception.what()));
    } catch (...)
    {
        return fail(Core::CoreErrorCode::Internal, "UI context allocation failed");
    }
}

UIContext::UIContext(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl))
{
}

UIContext::~UIContext() noexcept
{
    if (m_impl)
    {
        if (!m_impl->isOwnerThread() || m_impl->routeDispatchDepth != 0 ||
            m_impl->routedPointerListenerRegistry.operationInProgress() ||
            m_impl->buttonActionRegistry.operationInProgress() ||
            m_impl->sliderChangeCallbackRegistry.operationInProgress())
        {
            std::terminate();
        }
        m_impl->detachLifetime(this);
    }
}

Platform::WindowId UIContext::ownerWindow() const noexcept
{
    return m_impl->ownerWindow;
}

bool UIContext::contains(UINodeId node) const noexcept
{
    return m_impl->isOwnerThread() && m_impl->contains(node);
}

const UITheme& UIContext::productTheme() const noexcept
{
    return m_impl->productTheme;
}

Core::Status UIContext::setProductTheme(const UITheme& theme)
{
    return m_impl->setProductTheme(theme);
}

Core::Status UIContext::openTextFont(std::span<const std::byte> fontBytes, i32 faceIndex)
{
    return m_impl->openTextFont(fontBytes, faceIndex);
}

UIRootBuilder UIContext::rootBuilder() noexcept
{
    return UIRootBuilder(*this);
}

Core::Result<UITreeUpdater> UIContext::treeUpdater(UIRootOwner& rootOwner)
{
    if (Core::Status ownerThread = m_impl->ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    m_impl->drainDeferredRootDestroys();
    if (!rootOwner.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a root owner");
    }
    if (rootOwner.rootNodeId().ownerWindow() != m_impl->ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI root owner belongs to another owner window");
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = rootOwner.m_lifetime.lock();
    UIContext* attachedContext = lifetime ? lifetime->attachedContext() : nullptr;
    if (attachedContext == nullptr)
    {
        return fail(UIErrorCode::RootRequired, "UI root owner is detached");
    }
    if (attachedContext != this)
    {
        return fail(UIErrorCode::WrongContext, "UI root owner belongs to another context");
    }
    if (!m_impl->contains(rootOwner.rootNodeId()))
    {
        return fail(UIErrorCode::RootRequired, "UI root owner is no longer alive");
    }
    return UITreeUpdater(*this, rootOwner.rootNodeId());
}

Core::Status UIContext::commitStructure()
{
    return m_impl->commitStructure();
}

UICommittedStructureView UIContext::committedStructure() const noexcept
{
    return m_impl->committedStructure();
}

Core::Status UIContext::commitLayout(UILogicalSize viewportSize)
{
    return m_impl->commitLayout(viewportSize);
}

UICommittedLayoutView UIContext::committedLayout() const noexcept
{
    return m_impl->committedLayout();
}

UICommittedHitView UIContext::committedHit() const noexcept
{
    return m_impl->committedHit();
}

UICommittedPaintView UIContext::committedPaint() const noexcept
{
    return m_impl->committedPaint();
}

UICommittedSemanticsView UIContext::committedSemantics() const noexcept
{
    return m_impl->committedSemantics();
}

std::span<const u8> UIContext::glyphAtlasPixels() const noexcept
{
    return m_impl->glyphAtlasPixels();
}

u32 UIContext::glyphAtlasWidth() const noexcept
{
    return m_impl->glyphAtlasWidth();
}

u32 UIContext::glyphAtlasHeight() const noexcept
{
    return m_impl->glyphAtlasHeight();
}

UIPointerHitQueryResult UIContext::queryPointerHit(UILogicalPoint point) const noexcept
{
    return m_impl->queryPointerHit(point);
}

Core::Result<UIRoutedPointerListenerToken> UIContext::addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor,
                                                                               UIRoutedPointerCallback callback)
{
    auto registration = m_impl->addRoutedPointerListener(descriptor, std::move(callback), {});
    if (!registration)
    {
        return Core::failure(registration.error());
    }
    return UIRoutedPointerListenerToken{
        m_impl->lifetime,
        registration->first,
        registration->second,
    };
}

Core::Result<UIPointerRouteResult> UIContext::routePointerInput(const UIPointerInputEvent& input)
{
    return m_impl->routePointerInput(input);
}

Core::Status UIContext::cancelPointerInteraction(Platform::WindowId routedWindow)
{
    return m_impl->cancelPointerInteraction(routedWindow);
}

Core::Status UIContext::cancelDefaultActionInteraction(Platform::WindowId routedWindow,
                                                       std::optional<Platform::GamepadId> gamepad)
{
    return m_impl->cancelDefaultActionInteraction(routedWindow, gamepad);
}

Core::Result<UIContext::UIDefaultActionResult>
UIContext::routeDefaultActionActivate(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                      UIButtonActivationSource source,
                                      std::optional<Platform::DigitalControlIdentity> control)
{
    return m_impl->routeDefaultActionActivate(platformFrame, sourceSequence, source, std::move(control));
}

Core::Result<UIContext::UIDefaultActionResult>
UIContext::routeDefaultActionRelease(Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                     UIButtonActivationSource source, const Platform::DigitalControlIdentity& control)
{
    return m_impl->routeDefaultActionRelease(platformFrame, sourceSequence, source, control);
}

Core::Result<UIContext::UIDefaultFocusStepResult> UIContext::routeDefaultActionFocusStep(bool reverse)
{
    return m_impl->routeDefaultActionFocusStep(reverse);
}

Core::Result<UIDropdownCommandResult> UIContext::routeDropdownCommand(UIDropdownCommand command, bool pressed)
{
    return m_impl->routeDropdownCommand(command, pressed);
}

Core::Result<UIListViewCommandResult> UIContext::routeListViewCommand(UIListViewCommand command, bool pressed)
{
    return m_impl->routeListViewCommand(command, pressed);
}

Core::Result<UITreeViewCommandResult> UIContext::routeTreeViewCommand(UITreeViewCommand command, bool pressed)
{
    return m_impl->routeTreeViewCommand(command, pressed);
}

UINodeId UIContext::defaultActionFocus() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->defaultActionFocus();
}

UINodeId UIContext::activeFocusScope() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->activeFocusScope();
}

UINodeId UIContext::activeModal() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->activeModal();
}

UINodeId UIContext::pointerCapture() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->pointerCapture();
}

UINodeId UIContext::activePopup() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->activePopup();
}

Core::Status UIContext::requestFocus(UINodeId node)
{
    return m_impl->requestFocus(node);
}

Core::Status UIContext::clearFocus()
{
    return m_impl->clearFocus();
}

Core::Status UIContext::performAccessibilityAction(const UIAccessibilityAction& action)
{
    return m_impl->performAccessibilityAction(action);
}

UINodeId UIContext::imeFocus() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->imeFocus();
}

bool UIContext::imeCompositionActive() const noexcept
{
    return m_impl->isOwnerThread() && m_impl->imeCompositionActive();
}

std::string_view UIContext::imePreeditUtf8() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return {};
    }
    return m_impl->imePreeditUtf8();
}

u32 UIContext::imePreeditCursorCodepoint() const noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return 0;
    }
    return m_impl->imePreeditCursorCodepoint();
}

Core::Result<UIContext::UITextInputRouteResult>
UIContext::routeTextComposition(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                std::string_view preeditUtf8, u32 cursorCodepoint, Platform::TextCompositionStage stage)
{
    return m_impl->routeTextComposition(window, platformFrame, sourceSequence, preeditUtf8, cursorCodepoint, stage);
}

Core::Result<UIContext::UITextInputRouteResult> UIContext::routeTextInput(Platform::WindowId window,
                                                                          Platform::PlatformFrameId platformFrame,
                                                                          u64 sourceSequence,
                                                                          std::string_view committedUtf8)
{
    return m_impl->routeTextInput(window, platformFrame, sourceSequence, committedUtf8);
}

Core::Result<UIContext::UITextInputRouteResult>
UIContext::routeTextEditCommand(Platform::WindowId window, Platform::PlatformFrameId platformFrame, u64 sourceSequence,
                                UITextEditCommand command, bool extendSelection)
{
    return m_impl->routeTextEditCommand(window, platformFrame, sourceSequence, command, extendSelection);
}

UIContextStatistics UIContext::statistics() const noexcept
{
    return m_impl->statistics();
}

usize UIContext::liveNodeCount() const noexcept
{
    return m_impl->nodes.activeCount();
}

usize UIContext::liveRootCount() const noexcept
{
    return m_impl->liveRootCount;
}

Core::Result<UIRootOwner> UIContext::createRoot()
{
    return m_impl->createRoot(*this);
}

Core::Result<UINodeId> UIContext::createChild(UINodeId parent, UIWidgetKind kind)
{
    return m_impl->createChild(parent, kind);
}

Core::Result<UINodeId> UIContext::createListViewChild(UINodeId parent, UIListViewCreateConfig config)
{
    return m_impl->createListViewComposite(parent, config);
}

Core::Result<UINodeId> UIContext::createTreeViewChild(UINodeId parent, UITreeViewCreateConfig config)
{
    return m_impl->createTreeViewComposite(parent, config);
}

Core::Result<UINodeId> UIContext::createChildFromUpdater(UINodeId updaterRoot, UINodeId parent, UIWidgetKind kind)
{
    return m_impl->createChildFromUpdater(updaterRoot, parent, kind);
}

Core::Result<UINodeId> UIContext::createListViewFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                                           UIListViewCreateConfig config)
{
    return m_impl->createListViewFromUpdater(updaterRoot, parent, config);
}

Core::Result<UINodeId> UIContext::createTreeViewFromUpdater(UINodeId updaterRoot, UINodeId parent,
                                                            UITreeViewCreateConfig config)
{
    return m_impl->createTreeViewFromUpdater(updaterRoot, parent, config);
}

Core::Status UIContext::setLayoutStyleFromUpdater(UINodeId updaterRoot, UINodeId node, const UILayoutStyle& style)
{
    return m_impl->setLayoutStyleFromUpdater(updaterRoot, node, style);
}

Core::Status UIContext::setPointerHitPolicyFromUpdater(UINodeId updaterRoot, UINodeId node, UIPointerHitPolicy policy)
{
    return m_impl->setPointerHitPolicyFromUpdater(updaterRoot, node, policy);
}

Core::Status UIContext::setEnabledFromUpdater(UINodeId updaterRoot, UINodeId node, bool enabled)
{
    return m_impl->setEnabledFromUpdater(updaterRoot, node, enabled);
}

Core::Status UIContext::setFocusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node, UIFocusScopeMode mode)
{
    return m_impl->setFocusScopeModeFromUpdater(updaterRoot, node, mode);
}

Core::Result<UIFocusScopeMode> UIContext::focusScopeModeFromUpdater(UINodeId updaterRoot, UINodeId node) const
{
    return m_impl->focusScopeModeFromUpdater(updaterRoot, node);
}

Core::Status UIContext::requestFocusFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    return m_impl->requestFocusFromUpdater(updaterRoot, node);
}

Core::Status UIContext::clearFocusFromUpdater(UINodeId updaterRoot)
{
    return m_impl->clearFocusFromUpdater(updaterRoot);
}

Core::Result<bool> UIContext::isEnabledFromUpdater(UINodeId updaterRoot, UINodeId node) const
{
    return m_impl->isEnabledFromUpdater(updaterRoot, node);
}

Core::Status UIContext::setBoxPaintFromUpdater(UINodeId updaterRoot, UINodeId node, const UIBoxPaint& paint)
{
    return m_impl->setBoxPaintFromUpdater(updaterRoot, node, paint);
}

Core::Status UIContext::setButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId button, const UIButtonPaint& paint)
{
    return m_impl->setButtonPaintFromUpdater(updaterRoot, button, paint);
}

Core::Result<UIButtonPaint> UIContext::buttonPaintFromUpdater(UINodeId updaterRoot, UINodeId button) const
{
    return m_impl->buttonPaintFromUpdater(updaterRoot, button);
}

Core::Status UIContext::setTextFromUpdater(UINodeId updaterRoot, UINodeId node, std::string_view utf8)
{
    return m_impl->setTextFromUpdater(updaterRoot, node, utf8);
}

Core::Status UIContext::setTextStyleFromUpdater(UINodeId updaterRoot, UINodeId node, const UITextStyle& style)
{
    return m_impl->setTextStyleFromUpdater(updaterRoot, node, style);
}

Core::Result<std::string_view> UIContext::textFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    return m_impl->textFromUpdater(updaterRoot, node);
}

Core::Result<UITextStyle> UIContext::textStyleFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    return m_impl->textStyleFromUpdater(updaterRoot, node);
}

Core::Status UIContext::setTextSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit, UITextSelection selection)
{
    return m_impl->setTextSelectionFromUpdater(updaterRoot, textEdit, selection);
}

Core::Result<UITextSelection> UIContext::textSelectionFromUpdater(UINodeId updaterRoot, UINodeId textEdit) const
{
    return m_impl->textSelectionFromUpdater(updaterRoot, textEdit);
}

Core::Status UIContext::setButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button,
                                                   UIButtonActionCallback&& callback)
{
    return m_impl->setButtonActionFromUpdater(updaterRoot, button, std::move(callback));
}

Core::Status UIContext::clearButtonActionFromUpdater(UINodeId updaterRoot, UINodeId button)
{
    return m_impl->clearButtonActionFromUpdater(updaterRoot, button);
}

Core::Result<bool> UIContext::isButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId button)
{
    return m_impl->isButtonPressedFromUpdater(updaterRoot, button);
}

Core::Status UIContext::setCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                     UIButtonActionCallback&& callback)
{
    return m_impl->setCheckboxActionFromUpdater(updaterRoot, checkbox, std::move(callback));
}

Core::Status UIContext::clearCheckboxActionFromUpdater(UINodeId updaterRoot, UINodeId checkbox)
{
    return m_impl->clearCheckboxActionFromUpdater(updaterRoot, checkbox);
}

Core::Status UIContext::setCheckboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox,
                                                    const UICheckboxPaint& paint)
{
    return m_impl->setCheckboxPaintFromUpdater(updaterRoot, checkbox, paint);
}

Core::Result<UICheckboxPaint> UIContext::checkboxPaintFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const
{
    return m_impl->checkboxPaintFromUpdater(updaterRoot, checkbox);
}

Core::Status UIContext::setCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox, bool checked)
{
    return m_impl->setCheckedFromUpdater(updaterRoot, checkbox, checked);
}

Core::Result<bool> UIContext::isCheckedFromUpdater(UINodeId updaterRoot, UINodeId checkbox) const
{
    return m_impl->isCheckedFromUpdater(updaterRoot, checkbox);
}

Core::Result<bool> UIContext::isCheckboxPressedFromUpdater(UINodeId updaterRoot, UINodeId checkbox)
{
    return m_impl->isCheckboxPressedFromUpdater(updaterRoot, checkbox);
}

Core::Status UIContext::setSliderRangeFromUpdater(UINodeId updaterRoot, UINodeId slider, float minValue, float maxValue,
                                                  float step)
{
    return m_impl->setSliderRangeFromUpdater(updaterRoot, slider, minValue, maxValue, step);
}

Core::Status UIContext::setSliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider, float value)
{
    return m_impl->setSliderValueFromUpdater(updaterRoot, slider, value);
}

Core::Result<float> UIContext::sliderValueFromUpdater(UINodeId updaterRoot, UINodeId slider) const
{
    return m_impl->sliderValueFromUpdater(updaterRoot, slider);
}

Core::Status UIContext::setSliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider, const UISliderPaint& paint)
{
    return m_impl->setSliderPaintFromUpdater(updaterRoot, slider, paint);
}

Core::Result<UISliderPaint> UIContext::sliderPaintFromUpdater(UINodeId updaterRoot, UINodeId slider) const
{
    return m_impl->sliderPaintFromUpdater(updaterRoot, slider);
}

Core::Status UIContext::setSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider,
                                                           UISliderChangeCallback&& callback)
{
    return m_impl->setSliderChangeCallbackFromUpdater(updaterRoot, slider, std::move(callback));
}

Core::Status UIContext::clearSliderChangeCallbackFromUpdater(UINodeId updaterRoot, UINodeId slider)
{
    return m_impl->clearSliderChangeCallbackFromUpdater(updaterRoot, slider);
}

Core::Result<bool> UIContext::isSliderDraggingFromUpdater(UINodeId updaterRoot, UINodeId slider) const
{
    return m_impl->isSliderDraggingFromUpdater(updaterRoot, slider);
}

Core::Status UIContext::setScrollViewStyleFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                      const UIScrollViewStyle& style)
{
    return m_impl->setScrollViewStyleFromUpdater(updaterRoot, scrollView, style);
}

Core::Result<UIScrollViewStyle> UIContext::scrollViewStyleFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId scrollView) const
{
    return m_impl->scrollViewStyleFromUpdater(updaterRoot, scrollView);
}

Core::Status UIContext::setScrollViewOffsetFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                       UIScrollOffset offset)
{
    return m_impl->setScrollViewOffsetFromUpdater(updaterRoot, scrollView, offset);
}

Core::Result<UIScrollOffset> UIContext::scrollViewOffsetFromUpdater(UINodeId updaterRoot,
                                                                   UINodeId scrollView) const
{
    return m_impl->scrollViewOffsetFromUpdater(updaterRoot, scrollView);
}

Core::Result<UIScrollViewMetrics> UIContext::scrollViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId scrollView) const
{
    return m_impl->scrollViewMetricsFromUpdater(updaterRoot, scrollView);
}

Core::Status UIContext::setScrollViewPaintFromUpdater(UINodeId updaterRoot, UINodeId scrollView,
                                                      const UIScrollViewPaint& paint)
{
    return m_impl->setScrollViewPaintFromUpdater(updaterRoot, scrollView, paint);
}

Core::Result<UIScrollViewPaint> UIContext::scrollViewPaintFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId scrollView) const
{
    return m_impl->scrollViewPaintFromUpdater(updaterRoot, scrollView);
}

Core::Result<bool> UIContext::isScrollViewDraggingFromUpdater(UINodeId updaterRoot,
                                                             UINodeId scrollView) const
{
    return m_impl->isScrollViewDraggingFromUpdater(updaterRoot, scrollView);
}

Core::Status UIContext::setPopupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup,
                                                 const UIPopupStyle& style)
{
    return m_impl->setPopupStyleFromUpdater(updaterRoot, popup, style);
}

Core::Result<UIPopupStyle> UIContext::popupStyleFromUpdater(UINodeId updaterRoot, UINodeId popup) const
{
    return m_impl->popupStyleFromUpdater(updaterRoot, popup);
}

Core::Status UIContext::setPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup, bool open)
{
    return m_impl->setPopupOpenFromUpdater(updaterRoot, popup, open);
}

Core::Result<bool> UIContext::isPopupOpenFromUpdater(UINodeId updaterRoot, UINodeId popup) const
{
    return m_impl->isPopupOpenFromUpdater(updaterRoot, popup);
}

Core::Result<UIPopupMetrics> UIContext::popupMetricsFromUpdater(UINodeId updaterRoot, UINodeId popup) const
{
    return m_impl->popupMetricsFromUpdater(updaterRoot, popup);
}

Core::Status UIContext::setDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown, bool open)
{
    return m_impl->setDropdownOpenFromUpdater(updaterRoot, dropdown, open);
}

Core::Result<bool> UIContext::isDropdownOpenFromUpdater(UINodeId updaterRoot, UINodeId dropdown) const
{
    return m_impl->isDropdownOpenFromUpdater(updaterRoot, dropdown);
}

Core::Status UIContext::setDropdownSelectedItemFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                           UINodeId item)
{
    return m_impl->setDropdownSelectedItemFromUpdater(updaterRoot, dropdown, item);
}

Core::Result<UINodeId> UIContext::dropdownSelectedItemFromUpdater(UINodeId updaterRoot,
                                                                  UINodeId dropdown) const
{
    return m_impl->dropdownSelectedItemFromUpdater(updaterRoot, dropdown);
}

Core::Result<bool> UIContext::isDropdownItemSelectedFromUpdater(UINodeId updaterRoot, UINodeId item) const
{
    return m_impl->isDropdownItemSelectedFromUpdater(updaterRoot, item);
}

Core::Status UIContext::setDropdownPaintFromUpdater(UINodeId updaterRoot, UINodeId dropdown,
                                                    const UIDropdownPaint& paint)
{
    return m_impl->setDropdownPaintFromUpdater(updaterRoot, dropdown, paint);
}

Core::Result<UIDropdownPaint> UIContext::dropdownPaintFromUpdater(UINodeId updaterRoot,
                                                                  UINodeId dropdown) const
{
    return m_impl->dropdownPaintFromUpdater(updaterRoot, dropdown);
}

Core::Status UIContext::setListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                        UIListViewDataSource source)
{
    return m_impl->setListViewDataSourceFromUpdater(updaterRoot, listView, source);
}

Core::Status UIContext::clearListViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId listView)
{
    return m_impl->clearListViewDataSourceFromUpdater(updaterRoot, listView);
}

Core::Status UIContext::invalidateListViewItemsFromUpdater(UINodeId updaterRoot, UINodeId listView)
{
    return m_impl->invalidateListViewItemsFromUpdater(updaterRoot, listView);
}

Core::Status UIContext::setListViewStyleFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                   const UIListViewStyle& style)
{
    return m_impl->setListViewStyleFromUpdater(updaterRoot, listView, style);
}

Core::Result<UIListViewStyle> UIContext::listViewStyleFromUpdater(UINodeId updaterRoot,
                                                                 UINodeId listView) const
{
    return m_impl->listViewStyleFromUpdater(updaterRoot, listView);
}

Core::Status UIContext::setListViewPaintFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                   const UIListViewPaint& paint)
{
    return m_impl->setListViewPaintFromUpdater(updaterRoot, listView, paint);
}

Core::Result<UIListViewPaint> UIContext::listViewPaintFromUpdater(UINodeId updaterRoot,
                                                                 UINodeId listView) const
{
    return m_impl->listViewPaintFromUpdater(updaterRoot, listView);
}

Core::Result<UIListViewMetrics> UIContext::listViewMetricsFromUpdater(UINodeId updaterRoot,
                                                                     UINodeId listView) const
{
    return m_impl->listViewMetricsFromUpdater(updaterRoot, listView);
}

Core::Status UIContext::setListViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                           u64 logicalIndex)
{
    return m_impl->setListViewSelectedIndexFromUpdater(updaterRoot, listView, logicalIndex);
}

Core::Status UIContext::clearListViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId listView)
{
    return m_impl->clearListViewSelectionFromUpdater(updaterRoot, listView);
}

Core::Result<UIListViewSelection> UIContext::listViewSelectionFromUpdater(UINodeId updaterRoot,
                                                                         UINodeId listView) const
{
    return m_impl->listViewSelectionFromUpdater(updaterRoot, listView);
}

Core::Status UIContext::scrollListViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId listView,
                                                        u64 logicalIndex, UIListViewScrollAlignment alignment)
{
    return m_impl->scrollListViewToIndexFromUpdater(updaterRoot, listView, logicalIndex, alignment);
}

Core::Status UIContext::setTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                         UITreeViewDataSource source)
{
    return m_impl->setTreeViewDataSourceFromUpdater(updaterRoot, treeView, source);
}

Core::Status UIContext::clearTreeViewDataSourceFromUpdater(UINodeId updaterRoot, UINodeId treeView)
{
    return m_impl->clearTreeViewDataSourceFromUpdater(updaterRoot, treeView);
}

Core::Status UIContext::invalidateTreeViewItemsFromUpdater(UINodeId updaterRoot, UINodeId treeView)
{
    return m_impl->invalidateTreeViewItemsFromUpdater(updaterRoot, treeView);
}

Core::Status UIContext::setTreeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                    const UITreeViewStyle& style)
{
    return m_impl->setTreeViewStyleFromUpdater(updaterRoot, treeView, style);
}

Core::Result<UITreeViewStyle> UIContext::treeViewStyleFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    return m_impl->treeViewStyleFromUpdater(updaterRoot, treeView);
}

Core::Status UIContext::setTreeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView,
                                                    const UITreeViewPaint& paint)
{
    return m_impl->setTreeViewPaintFromUpdater(updaterRoot, treeView, paint);
}

Core::Result<UITreeViewPaint> UIContext::treeViewPaintFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    return m_impl->treeViewPaintFromUpdater(updaterRoot, treeView);
}

Core::Result<UITreeViewMetrics> UIContext::treeViewMetricsFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    return m_impl->treeViewMetricsFromUpdater(updaterRoot, treeView);
}

Core::Status UIContext::setTreeViewSelectedIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView, u64 logicalIndex)
{
    return m_impl->setTreeViewSelectedIndexFromUpdater(updaterRoot, treeView, logicalIndex);
}

Core::Status UIContext::clearTreeViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId treeView)
{
    return m_impl->clearTreeViewSelectionFromUpdater(updaterRoot, treeView);
}

Core::Result<UITreeViewSelection> UIContext::treeViewSelectionFromUpdater(UINodeId updaterRoot, UINodeId treeView) const
{
    return m_impl->treeViewSelectionFromUpdater(updaterRoot, treeView);
}

Core::Status UIContext::setTreeViewItemExpandedFromUpdater(UINodeId updaterRoot, UINodeId treeView, u64 logicalIndex,
                                                           bool expanded)
{
    return m_impl->setTreeViewItemExpandedFromUpdater(updaterRoot, treeView, logicalIndex, expanded);
}

Core::Status UIContext::scrollTreeViewToIndexFromUpdater(UINodeId updaterRoot, UINodeId treeView, u64 logicalIndex,
                                                         UITreeViewScrollAlignment alignment)
{
    return m_impl->scrollTreeViewToIndexFromUpdater(updaterRoot, treeView, logicalIndex, alignment);
}

Core::Status UIContext::setProgressBarRangeFromUpdater(UINodeId updaterRoot, UINodeId progressBar, float minValue,
                                                       float maxValue)
{
    return m_impl->setProgressBarRangeFromUpdater(updaterRoot, progressBar, minValue, maxValue);
}

Core::Status UIContext::setProgressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar, float value)
{
    return m_impl->setProgressBarValueFromUpdater(updaterRoot, progressBar, value);
}

Core::Result<float> UIContext::progressBarValueFromUpdater(UINodeId updaterRoot, UINodeId progressBar) const
{
    return m_impl->progressBarValueFromUpdater(updaterRoot, progressBar);
}

Core::Status UIContext::setProgressBarPaintFromUpdater(UINodeId updaterRoot, UINodeId progressBar,
                                                       const UIProgressBarPaint& paint)
{
    return m_impl->setProgressBarPaintFromUpdater(updaterRoot, progressBar, paint);
}

Core::Result<UIProgressBarPaint> UIContext::progressBarPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId progressBar) const
{
    return m_impl->progressBarPaintFromUpdater(updaterRoot, progressBar);
}

Core::Status UIContext::setRadioButtonPaintFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                       const UIRadioButtonPaint& paint)
{
    return m_impl->setRadioButtonPaintFromUpdater(updaterRoot, radioButton, paint);
}

Core::Result<UIRadioButtonPaint> UIContext::radioButtonPaintFromUpdater(UINodeId updaterRoot,
                                                                        UINodeId radioButton) const
{
    return m_impl->radioButtonPaintFromUpdater(updaterRoot, radioButton);
}

Core::Status UIContext::setRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton,
                                                        UIButtonActionCallback&& callback)
{
    return m_impl->setRadioButtonActionFromUpdater(updaterRoot, radioButton, std::move(callback));
}

Core::Status UIContext::clearRadioButtonActionFromUpdater(UINodeId updaterRoot, UINodeId radioButton)
{
    return m_impl->clearRadioButtonActionFromUpdater(updaterRoot, radioButton);
}

Core::Status UIContext::setRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton, bool selected)
{
    return m_impl->setRadioButtonSelectedFromUpdater(updaterRoot, radioButton, selected);
}

Core::Result<bool> UIContext::isRadioButtonSelectedFromUpdater(UINodeId updaterRoot, UINodeId radioButton) const
{
    return m_impl->isRadioButtonSelectedFromUpdater(updaterRoot, radioButton);
}

Core::Result<bool> UIContext::isRadioButtonPressedFromUpdater(UINodeId updaterRoot, UINodeId radioButton) const
{
    return m_impl->isRadioButtonPressedFromUpdater(updaterRoot, radioButton);
}

Core::Result<UIRoutedPointerListenerToken>
UIContext::addRoutedPointerListenerFromUpdater(UINodeId updaterRoot, UIRoutedPointerListenerDesc descriptor,
                                               UIRoutedPointerCallback&& callback)
{
    auto registration = m_impl->addRoutedPointerListenerFromUpdater(updaterRoot, descriptor, std::move(callback));
    if (!registration)
    {
        return Core::failure(registration.error());
    }
    return UIRoutedPointerListenerToken{
        m_impl->lifetime,
        registration->first,
        registration->second,
    };
}

Core::Status UIContext::destroyNodeFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    return m_impl->destroyFromUpdater(updaterRoot, node);
}

void UIContext::destroyRootFromOwner(UINodeId root) noexcept
{
    if (!m_impl->isOwnerThread())
    {
        return;
    }
    m_impl->drainDeferredRootDestroys();
    m_impl->destroyRootImmediately(root);
}

bool UIContext::isAliveInRoot(UINodeId updaterRoot, UINodeId node) const noexcept
{
    if (!m_impl->isOwnerThread() || !updaterRoot.hasValue())
    {
        return false;
    }
    return m_impl->isNodeWithinRoot(updaterRoot, node);
}

void UIContext::releaseRoutedPointerListenerFromToken(u32 slot, u32 generation) noexcept
{
    if (m_impl && m_impl->isOwnerThread())
    {
        m_impl->deactivateRoutedPointerListener(slot, generation, false);
    }
}

} // namespace Tina::UI
