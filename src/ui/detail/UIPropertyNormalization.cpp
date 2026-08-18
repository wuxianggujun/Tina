#include "UIPropertyNormalization.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] float normalizeFloat(float value) noexcept
{
    return value == 0.0F ? 0.0F : value;
}

[[nodiscard]] bool isFiniteNonNegative(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] Core::Status normalizeLayoutLength(
    UILayoutLength& length, bool allowAuto)
{
    switch (length.unit)
    {
    case UILayoutLengthUnit::Auto:
        if (!allowAuto)
        {
            return Core::failure(UIErrorCode::InvalidLayout,
                                 "UI layout length cannot be Auto here");
        }
        length.value = 0.0F;
        return Core::success();
    case UILayoutLengthUnit::Px:
        if (!isFiniteNonNegative(length.value))
        {
            return Core::failure(
                UIErrorCode::InvalidLayout,
                "UI layout length must be finite and non-negative");
        }
        length.value = normalizeFloat(length.value);
        return Core::success();
    case UILayoutLengthUnit::Percent:
        if (!isFiniteNonNegative(length.value) || length.value > 100.0F)
        {
            return Core::failure(
                UIErrorCode::InvalidLayout,
                "UI layout percent must be finite and within 0..100");
        }
        length.value = normalizeFloat(length.value);
        return Core::success();
    }

    return Core::failure(UIErrorCode::InvalidLayout,
                         "UI layout length unit is invalid");
}

[[nodiscard]] Core::Status normalizeOverlayOffsetLength(
    UILayoutLength& length)
{
    switch (length.unit)
    {
    case UILayoutLengthUnit::Auto:
        return Core::failure(UIErrorCode::InvalidLayout,
                             "UI overlay offset cannot be Auto");
    case UILayoutLengthUnit::Px:
        if (!std::isfinite(length.value))
        {
            return Core::failure(
                UIErrorCode::InvalidLayout,
                "UI overlay pixel offset must be finite");
        }
        length.value = normalizeFloat(length.value);
        return Core::success();
    case UILayoutLengthUnit::Percent:
        if (!std::isfinite(length.value) ||
            length.value < -100.0F || length.value > 100.0F)
        {
            return Core::failure(
                UIErrorCode::InvalidLayout,
                "UI overlay percent offset must be finite and within -100..100");
        }
        length.value = normalizeFloat(length.value);
        return Core::success();
    }

    return Core::failure(UIErrorCode::InvalidLayout,
                         "UI overlay offset unit is invalid");
}

[[nodiscard]] Core::Status normalizeSpacing(float& value)
{
    if (!isFiniteNonNegative(value))
    {
        return Core::failure(
            UIErrorCode::InvalidLayout,
            "UI layout spacing must be finite and non-negative");
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
    return value == UIFlexDirection::Row ||
           value == UIFlexDirection::Column;
}

[[nodiscard]] bool isValidJustifyContent(UIJustifyContent value) noexcept
{
    return value == UIJustifyContent::Start ||
           value == UIJustifyContent::Center ||
           value == UIJustifyContent::End ||
           value == UIJustifyContent::SpaceBetween;
}

[[nodiscard]] bool isValidAxisAlignment(UIAxisAlignment value) noexcept
{
    return value == UIAxisAlignment::Start || value == UIAxisAlignment::Center ||
           value == UIAxisAlignment::End || value == UIAxisAlignment::Stretch;
}

[[nodiscard]] bool isValidAlignSelf(UIAlignSelf value) noexcept
{
    return value == UIAlignSelf::Auto || value == UIAlignSelf::Start ||
           value == UIAlignSelf::Center || value == UIAlignSelf::End ||
           value == UIAlignSelf::Stretch;
}

[[nodiscard]] bool isValidPlacement(UILayoutPlacement value) noexcept
{
    return value == UILayoutPlacement::Flow || value == UILayoutPlacement::Overlay;
}

[[nodiscard]] bool isValidVisibility(UIVisibility value) noexcept
{
    return value == UIVisibility::Visible || value == UIVisibility::Hidden ||
           value == UIVisibility::Collapsed;
}

[[nodiscard]] bool isValidScrollAxes(UIScrollAxes axes) noexcept
{
    return axes == UIScrollAxes::None || axes == UIScrollAxes::Horizontal ||
           axes == UIScrollAxes::Vertical || axes == UIScrollAxes::Both;
}

[[nodiscard]] bool isValidScrollBarVisibility(
    UIScrollBarVisibility visibility) noexcept
{
    return visibility == UIScrollBarVisibility::Auto ||
           visibility == UIScrollBarVisibility::Always ||
           visibility == UIScrollBarVisibility::Hidden;
}

} // namespace

Core::Result<NormalizedUIContextCapacityConfig>
normalizeUIContextCapacityConfig(UIContextCapacityConfig config)
{
    if (Core::Status status = validateUIContextCapacityConfig(config); !status)
    {
        return Core::failure(status.error());
    }

    const auto deriveFromNodeCapacity = [nodeCapacity = config.nodeCapacity](
                                            usize configuredCapacity) noexcept {
        return configuredCapacity == 0 ? nodeCapacity : configuredCapacity;
    };
    return NormalizedUIContextCapacityConfig{
        .nodeCapacity = config.nodeCapacity,
        .rootCapacity = config.rootCapacity,
        .dirtyQueueCapacity = deriveFromNodeCapacity(config.dirtyQueueCapacity),
        .layoutSnapshotCapacity = deriveFromNodeCapacity(config.layoutSnapshotCapacity),
        .hitSnapshotCapacity = deriveFromNodeCapacity(config.hitSnapshotCapacity),
        .paintSnapshotCapacity = deriveFromNodeCapacity(config.paintSnapshotCapacity),
        .canvasCommandCapacity = deriveFromNodeCapacity(config.canvasCommandCapacity),
        .imageContentCapacity = deriveFromNodeCapacity(config.imageContentCapacity),
        .routePathCapacity = deriveFromNodeCapacity(config.routePathCapacity),
        .routedPointerListenerCapacity =
            deriveFromNodeCapacity(config.routedPointerListenerCapacity),
        .buttonActionCapacity = deriveFromNodeCapacity(config.buttonActionCapacity),
        .textByteCapacity = config.textByteCapacity == 0
                                ? UIContextCapacityConfig::DefaultTextByteCapacity
                                : config.textByteCapacity,
        .textEditVisualLineCapacity = config.textEditVisualLineCapacity == 0
                                          ? UIContextCapacityConfig::DefaultTextEditVisualLineCapacity
                                          : config.textEditVisualLineCapacity,
        .styleClassCapacity = config.styleClassCapacity,
        .styleTokenCapacity = config.styleTokenCapacity,
        .styleRuleCapacity = config.styleRuleCapacity,
        .styleBucketCapacity = config.styleBucketCapacity,
        .styleRulesPerBucketCapacity = config.styleRulesPerBucketCapacity,
        .nodeStyleClassLinkCapacity =
            config.nodeStyleClassLinkCapacity == 0
                ? config.nodeCapacity * 4U
                : config.nodeStyleClassLinkCapacity,
        .motionTrackCapacity = config.motionTrackCapacity == 0
                                   ? UIContextCapacityConfig::DefaultMotionTrackCapacity
                                   : config.motionTrackCapacity,
        .timelineCapacity = config.timelineCapacity == 0
                                ? UIContextCapacityConfig::DefaultTimelineCapacity
                                : config.timelineCapacity,
        .timelineTrackCapacity = config.timelineTrackCapacity == 0
                                     ? UIContextCapacityConfig::DefaultTimelineTrackCapacity
                                     : config.timelineTrackCapacity,
        .timelineKeyframeCapacity = config.timelineKeyframeCapacity == 0
                                        ? UIContextCapacityConfig::DefaultTimelineKeyframeCapacity
                                        : config.timelineKeyframeCapacity,
        .activeTimelineCapacity = config.activeTimelineCapacity == 0
                                      ? (std::min)(
                                            UIContextCapacityConfig::DefaultActiveTimelineCapacity,
                                            config.timelineCapacity == 0
                                                ? UIContextCapacityConfig::DefaultTimelineCapacity
                                                : config.timelineCapacity)
                                      : config.activeTimelineCapacity,
        .flowLayerCapacity = config.flowLayerCapacity == 0
                                 ? (std::min)(UIContextCapacityConfig::DefaultFlowLayerCapacity,
                                              config.nodeCapacity)
                                 : config.flowLayerCapacity,
        .flowScreenCapacity = config.flowScreenCapacity == 0
                                  ? (std::min)(UIContextCapacityConfig::DefaultFlowScreenCapacity,
                                               config.nodeCapacity)
                                  : config.flowScreenCapacity,
        .applyDefaultProductChrome = config.applyDefaultProductChrome,
    };
}

bool isValidImageSource(const UIImageSource& source) noexcept
{
    const bool validExtent = source.texturePixelExtent.width != 0 &&
                             source.texturePixelExtent.height != 0;
    const bool validSourceRect = source.sourcePixels.width != 0 &&
                                 source.sourcePixels.height != 0 &&
                                 source.sourcePixels.x <= source.texturePixelExtent.width &&
                                 source.sourcePixels.y <= source.texturePixelExtent.height &&
                                 source.sourcePixels.width <=
                                     source.texturePixelExtent.width - source.sourcePixels.x &&
                                 source.sourcePixels.height <=
                                     source.texturePixelExtent.height - source.sourcePixels.y;
    const bool validIntrinsic = std::isfinite(source.intrinsicLogicalSize.width) &&
                                std::isfinite(source.intrinsicLogicalSize.height) &&
                                source.intrinsicLogicalSize.width > 0.0F &&
                                source.intrinsicLogicalSize.height > 0.0F;
    return source.texture.hasValue() && validExtent && validSourceRect && validIntrinsic;
}

bool isValidImageSampling(UIImageSampling sampling) noexcept
{
    return sampling >= UIImageSampling::Linear && sampling <= UIImageSampling::Nearest;
}

Core::Result<UIImageContent> normalizeImageContent(UIImageContent content)
{
    const bool validFit = content.fit >= UIImageFit::Fill && content.fit <= UIImageFit::None;
    if (!isValidImageSource(content.source) || !validFit || !isValidImageSampling(content.sampling) ||
        !isValidContentAlignment(content.alignment))
    {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UI image source, geometry, fit, sampling, and alignment must be valid");
    }
    content.source.intrinsicLogicalSize.width = normalizeFloat(content.source.intrinsicLogicalSize.width);
    content.source.intrinsicLogicalSize.height = normalizeFloat(content.source.intrinsicLogicalSize.height);
    return content;
}

bool isValidLogicalCornerRadii(const UILogicalCornerRadii& radii) noexcept
{
    return std::isfinite(radii.topLeft) && radii.topLeft >= 0.0F &&
           std::isfinite(radii.topRight) && radii.topRight >= 0.0F &&
           std::isfinite(radii.bottomRight) && radii.bottomRight >= 0.0F &&
           std::isfinite(radii.bottomLeft) && radii.bottomLeft >= 0.0F;
}

UIBoxPaint normalizeBoxPaint(UIBoxPaint paint) noexcept
{
    const auto clearNonRectChrome = [&paint]() noexcept {
        paint.borderWidth = 0.0F;
        paint.borderLight = {};
        paint.borderDark = {};
        paint.shadow = {};
        paint.shadowOffsetX = 0.0F;
        paint.shadowOffsetY = 0.0F;
        paint.cornerRadii = {};
    };
    const auto clearPrimitiveGeometry = [&paint]() noexcept {
        paint.primitive = UIBoxPrimitiveKind::Rectangle;
        paint.line = {};
        paint.ellipseStrokeWidth = 0.0F;
    };
    switch (paint.primitive)
    {
    case UIBoxPrimitiveKind::Rectangle:
        paint.line = {};
        paint.ellipseStrokeWidth = 0.0F;
        break;
    case UIBoxPrimitiveKind::Ellipse:
        if (!std::isfinite(paint.ellipseStrokeWidth) || paint.ellipseStrokeWidth < 0.0F)
        {
            paint.solidFill.reset();
            clearPrimitiveGeometry();
            clearNonRectChrome();
        }
        else
        {
            paint.ellipseStrokeWidth = normalizeFloat(paint.ellipseStrokeWidth);
            paint.line = {};
            clearNonRectChrome();
        }
        break;
    case UIBoxPrimitiveKind::Line:
    {
        const float lineLength = std::hypot(
            paint.line.end.x - paint.line.start.x,
            paint.line.end.y - paint.line.start.y);
        if (!std::isfinite(paint.line.start.x) || !std::isfinite(paint.line.start.y) ||
            !std::isfinite(paint.line.end.x) || !std::isfinite(paint.line.end.y) ||
            !std::isfinite(paint.line.thickness) || paint.line.thickness <= 0.0F)
        {
            // Invalid geometry is deliberately blank rather than silently
            // becoming a rectangle over the entire layout envelope.
            paint.solidFill.reset();
            clearPrimitiveGeometry();
            clearNonRectChrome();
        }
        else if (!std::isfinite(lineLength) || lineLength <= 0.0F)
        {
            paint.solidFill.reset();
            clearPrimitiveGeometry();
            clearNonRectChrome();
        }
        else
        {
            paint.line.start.x = normalizeFloat(paint.line.start.x);
            paint.line.start.y = normalizeFloat(paint.line.start.y);
            paint.line.end.x = normalizeFloat(paint.line.end.x);
            paint.line.end.y = normalizeFloat(paint.line.end.y);
            paint.line.thickness = normalizeFloat(paint.line.thickness);
            paint.ellipseStrokeWidth = 0.0F;
            clearNonRectChrome();
        }
        break;
    }
    default:
        paint.solidFill.reset();
        clearPrimitiveGeometry();
        clearNonRectChrome();
        break;
    }
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
    if (!(std::isfinite(paint.shadowOffsetX) &&
          std::isfinite(paint.shadowOffsetY)) ||
        paint.shadow.alpha == 0)
    {
        paint.shadow = {};
        paint.shadowOffsetX = 0.0F;
        paint.shadowOffsetY = 0.0F;
    }
    const auto normalizeCornerRadius = [](float radius) noexcept {
        return std::isfinite(radius) && radius > 0.0F
                   ? normalizeFloat(radius)
                   : 0.0F;
    };
    paint.cornerRadii = {
        .topLeft = normalizeCornerRadius(paint.cornerRadii.topLeft),
        .topRight = normalizeCornerRadius(paint.cornerRadii.topRight),
        .bottomRight = normalizeCornerRadius(paint.cornerRadii.bottomRight),
        .bottomLeft = normalizeCornerRadius(paint.cornerRadii.bottomLeft),
    };
    return paint;
}

Core::Result<UIScrollViewStyle>
normalizeScrollViewStyle(UIScrollViewStyle style)
{
    if (!isValidScrollAxes(style.axes) ||
        !isValidScrollBarVisibility(style.scrollBarVisibility) ||
        !(std::isfinite(style.wheelStep) && style.wheelStep > 0.0F))
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UI ScrollView axes/visibility must be valid and wheel step must be finite and positive");
    }
    style.wheelStep = normalizeFloat(style.wheelStep);
    return style;
}

Core::Result<UIScrollViewPaint>
normalizeScrollViewPaint(UIScrollViewPaint paint)
{
    if (!(std::isfinite(paint.thickness) && paint.thickness > 0.0F) ||
        !(std::isfinite(paint.minThumbExtent) &&
          paint.minThumbExtent > 0.0F))
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UI ScrollView scrollbar thickness and minimum thumb extent must be finite and positive");
    }
    paint.thickness = normalizeFloat(paint.thickness);
    paint.minThumbExtent = normalizeFloat(paint.minThumbExtent);
    return paint;
}

Core::Result<UIScrollOffset> normalizeScrollOffset(UIScrollOffset offset)
{
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) ||
        offset.x < 0.0F || offset.y < 0.0F)
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UI ScrollView offset must contain finite non-negative coordinates");
    }
    offset.x = normalizeFloat(offset.x);
    offset.y = normalizeFloat(offset.y);
    return offset;
}

Core::Result<UIListViewCreateConfig>
normalizeListViewCreateConfig(UIListViewCreateConfig config)
{
    if (config.materializedItemCapacity == 0 ||
        config.materializedItemCapacity >
            UIListViewCreateConfig::MaximumMaterializedItemCapacity)
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UI ListView materialized item capacity must be within the supported range");
    }
    return config;
}

Core::Result<UIListViewStyle>
normalizeListViewStyle(UIListViewStyle style)
{
    if (!(std::isfinite(style.rowHeight) && style.rowHeight > 0.0F) ||
        !isValidScrollBarVisibility(style.scrollBarVisibility) ||
        !(std::isfinite(style.wheelStep) && style.wheelStep > 0.0F) ||
        (style.rowTextOverflow != UITextOverflow::Clip &&
         style.rowTextOverflow != UITextOverflow::Ellipsis))
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UI ListView row height/wheel step must be finite and positive, visibility must be valid, and row text overflow must be Clip or Ellipsis");
    }
    style.rowHeight = normalizeFloat(style.rowHeight);
    style.wheelStep = normalizeFloat(style.wheelStep);
    return style;
}

Core::Result<UIListViewPaint>
normalizeListViewPaint(UIListViewPaint paint)
{
    auto scrollBar = normalizeScrollViewPaint(paint.scrollBar);
    if (!scrollBar)
    {
        return Core::failure(scrollBar.error());
    }
    paint.scrollBar = *scrollBar;
    return paint;
}

bool isValidListViewScrollAlignment(
    UIListViewScrollAlignment alignment) noexcept
{
    return alignment == UIListViewScrollAlignment::Nearest ||
           alignment == UIListViewScrollAlignment::Start ||
           alignment == UIListViewScrollAlignment::Center ||
           alignment == UIListViewScrollAlignment::End;
}

Core::Result<UITreeViewCreateConfig>
normalizeTreeViewCreateConfig(UITreeViewCreateConfig config)
{
    if (config.materializedItemCapacity == 0 ||
        config.materializedItemCapacity >
            UITreeViewCreateConfig::MaximumMaterializedItemCapacity)
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UI TreeView materialized item capacity must be within the supported range");
    }
    return config;
}

Core::Result<UITreeViewStyle>
normalizeTreeViewStyle(UITreeViewStyle style)
{
    if (!(std::isfinite(style.rowHeight) && style.rowHeight > 0.0F) ||
        !isValidScrollBarVisibility(style.scrollBarVisibility) ||
        !(std::isfinite(style.wheelStep) && style.wheelStep > 0.0F) ||
        !(std::isfinite(style.indentation) && style.indentation >= 0.0F) ||
        !(std::isfinite(style.disclosureExtent) &&
          style.disclosureExtent > 0.0F) ||
        !(std::isfinite(style.disclosureGap) && style.disclosureGap >= 0.0F))
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UI TreeView row, scroll, indentation, and disclosure metrics must be finite and valid");
    }
    style.rowHeight = normalizeFloat(style.rowHeight);
    style.wheelStep = normalizeFloat(style.wheelStep);
    style.indentation = normalizeFloat(style.indentation);
    style.disclosureExtent = normalizeFloat(style.disclosureExtent);
    style.disclosureGap = normalizeFloat(style.disclosureGap);
    return style;
}

Core::Result<UITreeViewPaint>
normalizeTreeViewPaint(UITreeViewPaint paint)
{
    auto scrollBar = normalizeScrollViewPaint(paint.scrollBar);
    if (!scrollBar)
    {
        return Core::failure(scrollBar.error());
    }
    paint.scrollBar = *scrollBar;
    return paint;
}

bool isValidTreeViewScrollAlignment(
    UITreeViewScrollAlignment alignment) noexcept
{
    return alignment == UITreeViewScrollAlignment::Nearest ||
           alignment == UITreeViewScrollAlignment::Start ||
           alignment == UITreeViewScrollAlignment::Center ||
           alignment == UITreeViewScrollAlignment::End;
}

Core::Result<UIPopupStyle> normalizePopupStyle(UIPopupStyle style)
{
    if ((style.placement != UIPopupPlacement::Auto &&
         style.placement != UIPopupPlacement::Below &&
         style.placement != UIPopupPlacement::Above) ||
        !std::isfinite(style.anchorGap) || style.anchorGap < 0.0F)
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UI Popup placement must be valid and anchor gap must be finite and non-negative");
    }
    style.anchorGap = normalizeFloat(style.anchorGap);
    return style;
}

Core::Result<UIDropdownPaint>
normalizeDropdownPaint(UIDropdownPaint paint)
{
    if (!(std::isfinite(paint.indicatorWidth) &&
          paint.indicatorWidth > 0.0F) ||
        !(std::isfinite(paint.indicatorHeight) &&
          paint.indicatorHeight > 0.0F) ||
        !std::isfinite(paint.indicatorInset) || paint.indicatorInset < 0.0F)
    {
        return Core::failure(
            UIErrorCode::InvalidControlValue,
            "UI Dropdown indicator size must be finite and positive and inset must be non-negative");
    }
    paint.indicatorWidth = normalizeFloat(paint.indicatorWidth);
    paint.indicatorHeight = normalizeFloat(paint.indicatorHeight);
    paint.indicatorInset = normalizeFloat(paint.indicatorInset);
    return paint;
}

Core::Status validateProductTheme(const UITheme& theme)
{
    if (!isFiniteNonNegative(theme.panelBorderWidth) ||
        !isFiniteNonNegative(theme.panelCornerRadius) ||
        !isFiniteNonNegative(theme.controlCornerRadius) ||
        !std::isfinite(theme.panelShadowOffsetX) ||
        !std::isfinite(theme.panelShadowOffsetY) ||
        !isFiniteNonNegative(theme.checkboxIndicatorInset) ||
        !isFiniteNonNegative(theme.radioSelectedInset) ||
        !isFiniteNonNegative(theme.radioLabelGap) ||
        !isFiniteNonNegative(theme.sliderContentInset) ||
        !isFiniteNonNegative(theme.sliderThumbWidth) ||
        !(std::isfinite(theme.typography.display) && theme.typography.display > 0.0F) ||
        !(std::isfinite(theme.typography.title) && theme.typography.title > 0.0F) ||
        !(std::isfinite(theme.typography.section) && theme.typography.section > 0.0F) ||
        !(std::isfinite(theme.typography.body) && theme.typography.body > 0.0F) ||
        !(std::isfinite(theme.typography.control) && theme.typography.control > 0.0F) ||
        !(std::isfinite(theme.typography.caption) && theme.typography.caption > 0.0F))
    {
        return Core::failure(
            UIErrorCode::InvalidTheme,
            "UI Theme metrics must be finite; sizes must be positive and insets non-negative");
    }
    return Core::success();
}

Core::Result<UILayoutStyle> normalizeLayoutStyle(UILayoutStyle style)
{
    if (Core::Status status = normalizeLayoutLength(style.size.width, true);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.size.height, true);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(
            style.minMax.minWidth, true);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(
            style.minMax.minHeight, true);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(
            style.minMax.maxWidth, true);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(
            style.minMax.maxHeight, true);
        !status)
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
    if (Core::Status status = normalizeSpacing(style.flexItem.grow); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeSpacing(style.flexItem.shrink); !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeLayoutLength(style.flexItem.basis, true);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeSpacing(style.flexContainer.gap.row);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeSpacing(style.flexContainer.gap.column);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeOverlayOffsetLength(style.overlay.offset.x);
        !status)
    {
        return Core::failure(status.error());
    }
    if (Core::Status status = normalizeOverlayOffsetLength(style.overlay.offset.y);
        !status)
    {
        return Core::failure(status.error());
    }
    if (!isValidFlexDirection(style.flexContainer.direction) ||
        !isValidJustifyContent(style.flexContainer.justifyContent) ||
        !isValidAxisAlignment(style.flexContainer.alignItems) ||
        !isValidAlignSelf(style.flexItem.alignSelf) ||
        !isValidAxisAlignment(style.overlay.horizontal) ||
        !isValidAxisAlignment(style.overlay.vertical) ||
        !isValidPlacement(style.placement) ||
        !isValidVisibility(style.visibility))
    {
        return Core::failure(UIErrorCode::InvalidLayout,
                             "UI layout enum value is invalid");
    }

    return style;
}

bool isValidContentAlignment(UIContentAlignment alignment) noexcept
{
    const auto validAxis = [](UIAxisAlignment axis) noexcept {
        return axis == UIAxisAlignment::Start ||
               axis == UIAxisAlignment::Center ||
               axis == UIAxisAlignment::End;
    };
    return validAxis(alignment.horizontal) && validAxis(alignment.vertical);
}

} // namespace Tina::UI::Detail
