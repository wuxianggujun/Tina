#include <tina/integration/UIRenderDisplayList.hpp>

#include <tina/core/base/ScopeExit.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace Tina::Integration {
namespace {

struct PixelProjection final {
    double scaleX = 1.0;
    double scaleY = 1.0;
    double viewportLeft = 0.0;
    double viewportTop = 0.0;
    double viewportRight = 0.0;
    double viewportBottom = 0.0;
};

[[nodiscard]] Core::Status invalidInput(const char* message)
{
    return Core::failure(Core::CoreErrorCode::InvalidArgument, message);
}

[[nodiscard]] bool validLogicalRect(const UI::UILogicalRect& rect) noexcept
{
    return std::isfinite(rect.x) && std::isfinite(rect.y) &&
           std::isfinite(rect.width) && std::isfinite(rect.height) &&
           rect.width >= 0.0F && rect.height >= 0.0F;
}

[[nodiscard]] bool validPremultipliedColor(
    const UI::UIPremultipliedRgba8Color& color) noexcept
{
    return color.red <= color.alpha && color.green <= color.alpha &&
           color.blue <= color.alpha;
}

[[nodiscard]] bool zeroPoint(const UI::UILogicalPoint& point) noexcept
{
    return point.x == 0.0F && point.y == 0.0F;
}

[[nodiscard]] bool validLogicalCornerRadii(
    const UI::UILogicalCornerRadii& radii) noexcept
{
    return std::isfinite(radii.topLeft) && radii.topLeft >= 0.0F &&
           std::isfinite(radii.topRight) && radii.topRight >= 0.0F &&
           std::isfinite(radii.bottomRight) && radii.bottomRight >= 0.0F &&
           std::isfinite(radii.bottomLeft) && radii.bottomLeft >= 0.0F;
}

[[nodiscard]] bool validLinePaint(const UI::UICommittedPaintEntry& entry) noexcept
{
    if (entry.kind != UI::UICommittedPaintKind::SolidLine)
    {
        return zeroPoint(entry.lineStart) && zeroPoint(entry.lineEnd) &&
               entry.lineThickness == 0.0F;
    }
    if (!entry.cornerRadii.isZero() || entry.ellipseStrokeWidth != 0.0F ||
        !std::isfinite(entry.lineStart.x) || !std::isfinite(entry.lineStart.y) ||
        !std::isfinite(entry.lineEnd.x) || !std::isfinite(entry.lineEnd.y) ||
        !std::isfinite(entry.lineThickness) || entry.lineThickness <= 0.0F ||
        (entry.lineStart.x == entry.lineEnd.x && entry.lineStart.y == entry.lineEnd.y) ||
        entry.worldRect.width <= 0.0F || entry.worldRect.height <= 0.0F)
    {
        return false;
    }

    const double deltaX = static_cast<double>(entry.lineEnd.x) - entry.lineStart.x;
    const double deltaY = static_cast<double>(entry.lineEnd.y) - entry.lineStart.y;
    const double length = std::hypot(deltaX, deltaY);
    if (!std::isfinite(length) || length <= 0.0)
    {
        return false;
    }
    const double halfThickness = static_cast<double>(entry.lineThickness) * 0.5;
    const double extentX = std::abs(deltaY / length) * halfThickness;
    const double extentY = std::abs(deltaX / length) * halfThickness;
    const double requiredLeft =
        (std::min)(static_cast<double>(entry.lineStart.x),
                   static_cast<double>(entry.lineEnd.x)) - extentX;
    const double requiredTop =
        (std::min)(static_cast<double>(entry.lineStart.y),
                   static_cast<double>(entry.lineEnd.y)) - extentY;
    const double requiredRight =
        (std::max)(static_cast<double>(entry.lineStart.x),
                   static_cast<double>(entry.lineEnd.x)) + extentX;
    const double requiredBottom =
        (std::max)(static_cast<double>(entry.lineStart.y),
                   static_cast<double>(entry.lineEnd.y)) + extentY;
    const double envelopeRight = static_cast<double>(entry.worldRect.x) +
                                 static_cast<double>(entry.worldRect.width);
    const double envelopeBottom = static_cast<double>(entry.worldRect.y) +
                                  static_cast<double>(entry.worldRect.height);
    const double tolerance = (std::max)(
        1.0,
        (std::max)(static_cast<double>(entry.worldRect.width),
                   static_cast<double>(entry.worldRect.height))) * 1.0e-5;
    return std::isfinite(requiredLeft) && std::isfinite(requiredTop) &&
           std::isfinite(requiredRight) && std::isfinite(requiredBottom) &&
           requiredLeft >= static_cast<double>(entry.worldRect.x) - tolerance &&
           requiredTop >= static_cast<double>(entry.worldRect.y) - tolerance &&
           requiredRight <= envelopeRight + tolerance &&
           requiredBottom <= envelopeBottom + tolerance;
}

[[nodiscard]] bool validEllipsePaint(
    const UI::UICommittedPaintEntry& entry) noexcept
{
    if (entry.kind != UI::UICommittedPaintKind::SolidEllipse)
    {
        return entry.ellipseStrokeWidth == 0.0F;
    }
    return entry.cornerRadii.isZero() &&
           std::isfinite(entry.ellipseStrokeWidth) &&
           entry.ellipseStrokeWidth >= 0.0F;
}

[[nodiscard]] bool validImagePaint(const UI::UICommittedPaintEntry& entry) noexcept
{
    const UI::UIImageSource& source = entry.imageSource;
    const UI::UIImagePixelRect& sourcePixels = source.sourcePixels;
    const UI::UIImagePixelExtent& textureExtent = source.texturePixelExtent;
    const bool validSourceRect = textureExtent.width != 0 && textureExtent.height != 0 &&
                                 sourcePixels.width != 0 && sourcePixels.height != 0 &&
                                 sourcePixels.x <= textureExtent.width &&
                                 sourcePixels.y <= textureExtent.height &&
                                 sourcePixels.width <= textureExtent.width - sourcePixels.x &&
                                 sourcePixels.height <= textureExtent.height - sourcePixels.y;
    const bool validIntrinsicSize = std::isfinite(source.intrinsicLogicalSize.width) &&
                                    std::isfinite(source.intrinsicLogicalSize.height) &&
                                    source.intrinsicLogicalSize.width > 0.0F &&
                                    source.intrinsicLogicalSize.height > 0.0F;
    const bool validSampling = entry.imageSampling == UI::UIImageSampling::Linear ||
                               entry.imageSampling == UI::UIImageSampling::Nearest;
    const bool validProjection = [&entry]() noexcept {
        if (entry.imageBoundsProjection == UI::UICommittedImageBoundsProjection::Cover)
        {
            return entry.imageProjectionEnd.x == 0.0F && entry.imageProjectionEnd.y == 0.0F;
        }
        if (entry.imageBoundsProjection != UI::UICommittedImageBoundsProjection::SharedBoundary ||
            !std::isfinite(entry.imageProjectionEnd.x) || !std::isfinite(entry.imageProjectionEnd.y) ||
            entry.imageProjectionEnd.x < entry.worldRect.x || entry.imageProjectionEnd.y < entry.worldRect.y)
        {
            return false;
        }
        return entry.worldRect.width == entry.imageProjectionEnd.x - entry.worldRect.x &&
               entry.worldRect.height == entry.imageProjectionEnd.y - entry.worldRect.y;
    }();
    return entry.root.hasValue() && source.texture.hasValue() && validSourceRect &&
           validIntrinsicSize && validSampling && validProjection;
}

[[nodiscard]] Core::Status validatePaintView(UI::UICommittedPaintView paintView)
{
    const UI::UILogicalSize viewport = paintView.viewportSize();
    if (!std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
        viewport.width < 0.0F || viewport.height < 0.0F)
    {
        return invalidInput(
            "The committed UI logical viewport must be finite and non-negative");
    }

    std::optional<u32> previousOrdinal;
    for (const UI::UICommittedPaintEntry& entry : paintView.entries())
    {
        if (previousOrdinal.has_value() && entry.paintOrdinal <= *previousOrdinal)
        {
            return invalidInput(
                "Committed UI paint ordinals must be strictly increasing");
        }
        previousOrdinal = entry.paintOrdinal;

        if (!validLogicalRect(entry.worldRect) ||
            !validLogicalRect(entry.effectiveClip))
        {
            return invalidInput(
                "Committed UI paint rectangles must be finite with non-negative extents");
        }
        if (!validPremultipliedColor(entry.solidFill))
        {
            return invalidInput(
                "Committed UI paint colors must use premultiplied RGBA8 channels");
        }
        const bool validKind = entry.kind == UI::UICommittedPaintKind::SolidQuad ||
                               entry.kind == UI::UICommittedPaintKind::SolidEllipse ||
                               entry.kind == UI::UICommittedPaintKind::SolidLine ||
                               entry.kind == UI::UICommittedPaintKind::Glyph ||
                               entry.kind == UI::UICommittedPaintKind::Image;
        if (!validKind || !validLogicalCornerRadii(entry.cornerRadii) ||
            (entry.kind != UI::UICommittedPaintKind::SolidQuad && !entry.cornerRadii.isZero()))
        {
            return invalidInput(
                "Committed UI paint kind and corner radii must be valid");
        }
        if (!validLinePaint(entry))
        {
            return invalidInput(
                "Committed UI line geometry must be finite, non-degenerate, positive-width, and covered by its logical envelope");
        }
        if (!validEllipsePaint(entry))
        {
            return invalidInput(
                "Committed UI ellipse geometry and stroke width must be finite, non-negative, and exclusive to SolidEllipse paint");
        }
        if (entry.kind == UI::UICommittedPaintKind::Image && !validImagePaint(entry))
        {
            return invalidInput(
                "Committed UI image paint requires valid root, source geometry, intrinsic size, sampling, and bounds projection");
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateFramebufferViewport(
    const Render::UIPixelRect& viewport)
{
    if (viewport.x < 0 || viewport.y < 0)
    {
        return invalidInput(
            "The UI framebuffer viewport origin must be non-negative");
    }

    constexpr i64 MaxSignedExclusive =
        static_cast<i64>((std::numeric_limits<i32>::max)()) + 1;
    const i64 right = static_cast<i64>(viewport.x) + viewport.width;
    const i64 bottom = static_cast<i64>(viewport.y) + viewport.height;
    if (right > MaxSignedExclusive || bottom > MaxSignedExclusive)
    {
        return invalidInput(
            "The UI framebuffer viewport exceeds representable pixel coordinates");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<PixelProjection> makeProjection(
    UI::UILogicalSize logicalViewport,
    const Render::UIPixelRect& framebufferViewport)
{
    PixelProjection projection{
        .scaleX = static_cast<double>(framebufferViewport.width) /
                  static_cast<double>(logicalViewport.width),
        .scaleY = static_cast<double>(framebufferViewport.height) /
                  static_cast<double>(logicalViewport.height),
        .viewportLeft = static_cast<double>(framebufferViewport.x),
        .viewportTop = static_cast<double>(framebufferViewport.y),
        .viewportRight = static_cast<double>(framebufferViewport.x) +
                         static_cast<double>(framebufferViewport.width),
        .viewportBottom = static_cast<double>(framebufferViewport.y) +
                          static_cast<double>(framebufferViewport.height),
    };
    if (!std::isfinite(projection.scaleX) ||
        !std::isfinite(projection.scaleY))
    {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "The UI logical-to-framebuffer scale is not finite");
    }
    return projection;
}

struct ProjectedAxis final {
    i32 origin = 0;
    u32 extent = 0;
};

struct ProjectedQuad final {
    Render::UIPixelRect bounds{};
    Render::UISolidQuadVertices vertices{};
};

[[nodiscard]] Core::Result<ProjectedAxis> projectAxis(
    float logicalOrigin,
    float logicalExtent,
    double scale,
    double pixelViewportStart,
    double pixelViewportEnd,
    bool clampToViewport,
    std::optional<float> sharedLogicalEnd = std::nullopt)
{
    const double logicalStart = static_cast<double>(logicalOrigin);
    // NineSlice patches carry their authored half-open end explicitly because
    // float(end - start) cannot always be reversed exactly by start + extent.
    const double logicalEnd = sharedLogicalEnd.has_value()
                                ? static_cast<double>(*sharedLogicalEnd)
                                : logicalStart + static_cast<double>(logicalExtent);
    const double projectedStart = pixelViewportStart + logicalStart * scale;
    const double projectedEnd = pixelViewportStart + logicalEnd * scale;
    if (!std::isfinite(projectedStart) || !std::isfinite(projectedEnd))
    {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "A committed UI rectangle cannot be represented in framebuffer coordinates");
    }

    double roundedStart = sharedLogicalEnd.has_value()
                            ? std::round(projectedStart)
                            : std::floor(projectedStart);
    double roundedEnd = logicalExtent == 0.0F
                            ? roundedStart
                            : sharedLogicalEnd.has_value()
                                ? std::round(projectedEnd)
                                : std::ceil(projectedEnd);
    if (clampToViewport)
    {
        roundedStart = std::clamp(
            roundedStart, pixelViewportStart, pixelViewportEnd);
        roundedEnd = std::clamp(
            roundedEnd, pixelViewportStart, pixelViewportEnd);
    }
    if (roundedEnd < roundedStart)
    {
        roundedEnd = roundedStart;
    }

    constexpr double MinimumCoordinate =
        static_cast<double>((std::numeric_limits<i32>::min)());
    constexpr double MaximumCoordinateExclusive =
        static_cast<double>((std::numeric_limits<i32>::max)()) + 1.0;
    if (roundedStart < MinimumCoordinate || roundedStart > MaximumCoordinateExclusive ||
        roundedEnd < MinimumCoordinate || roundedEnd > MaximumCoordinateExclusive)
    {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "A projected UI rectangle exceeds representable pixel coordinates");
    }
    const i64 integerStart = static_cast<i64>(roundedStart);
    const i64 integerEnd = static_cast<i64>(roundedEnd);
    const i64 integerExtent = integerEnd - integerStart;
    if (integerExtent < 0 ||
        integerExtent > static_cast<i64>((std::numeric_limits<u32>::max)()))
    {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "A projected UI rectangle extent exceeds the pixel representation");
    }

    // The half-open viewport endpoint may be INT32_MAX + 1. It can only be the
    // origin of an empty interval, whose coordinate does not affect rendering.
    const i64 storedStart =
        integerStart > static_cast<i64>((std::numeric_limits<i32>::max)())
            ? static_cast<i64>((std::numeric_limits<i32>::max)())
            : integerStart;
    if (storedStart < static_cast<i64>((std::numeric_limits<i32>::min)()))
    {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "A projected UI rectangle origin exceeds the pixel representation");
    }

    return ProjectedAxis{
        .origin = static_cast<i32>(storedStart),
        .extent = static_cast<u32>(integerExtent),
    };
}

[[nodiscard]] Core::Result<Render::UIPixelRect> projectRect(
    const UI::UILogicalRect& logicalRect,
    const PixelProjection& projection,
    std::optional<UI::UILogicalPoint> sharedLogicalEnd = std::nullopt,
    bool clampToViewport = true)
{
    auto horizontal = projectAxis(
        logicalRect.x,
        logicalRect.width,
        projection.scaleX,
        projection.viewportLeft,
        projection.viewportRight,
        clampToViewport,
        sharedLogicalEnd.has_value() ? std::optional<float>{sharedLogicalEnd->x} : std::nullopt);
    if (!horizontal)
    {
        return Core::failure(std::move(horizontal.error()));
    }
    auto vertical = projectAxis(
        logicalRect.y,
        logicalRect.height,
        projection.scaleY,
        projection.viewportTop,
        projection.viewportBottom,
        clampToViewport,
        sharedLogicalEnd.has_value() ? std::optional<float>{sharedLogicalEnd->y} : std::nullopt);
    if (!vertical)
    {
        return Core::failure(std::move(vertical.error()));
    }
    return Render::UIPixelRect{
        .x = horizontal->origin,
        .y = vertical->origin,
        .width = horizontal->extent,
        .height = vertical->extent,
    };
}

[[nodiscard]] Core::Result<ProjectedQuad> projectLine(
    const UI::UICommittedPaintEntry& entry,
    const PixelProjection& projection)
{
    struct DoublePoint final {
        double x = 0.0;
        double y = 0.0;
    };

    const double startX = static_cast<double>(entry.lineStart.x);
    const double startY = static_cast<double>(entry.lineStart.y);
    const double endX = static_cast<double>(entry.lineEnd.x);
    const double endY = static_cast<double>(entry.lineEnd.y);
    const double deltaX = endX - startX;
    const double deltaY = endY - startY;
    const double length = std::hypot(deltaX, deltaY);
    if (!std::isfinite(length) || length <= 0.0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "A committed UI line is degenerate");
    }
    const double halfThickness = static_cast<double>(entry.lineThickness) * 0.5;
    const double normalX = -deltaY / length * halfThickness;
    const double normalY = deltaX / length * halfThickness;
    const auto projectCorner = [&](double logicalX, double logicalY) noexcept {
        return DoublePoint{
            .x = projection.viewportLeft + logicalX * projection.scaleX,
            .y = projection.viewportTop + logicalY * projection.scaleY,
        };
    };
    const std::array projected{
        projectCorner(startX - normalX, startY - normalY),
        projectCorner(endX - normalX, endY - normalY),
        projectCorner(endX + normalX, endY + normalY),
        projectCorner(startX + normalX, startY + normalY),
    };

    constexpr double MinimumCoordinate =
        static_cast<double>((std::numeric_limits<i32>::min)());
    constexpr double MaximumCoordinateExclusive =
        static_cast<double>((std::numeric_limits<i32>::max)()) + 1.0;
    std::array<Render::UISubpixelPoint, 4> points{};
    for (usize index = 0; index < projected.size(); ++index)
    {
        if (!std::isfinite(projected[index].x) ||
            !std::isfinite(projected[index].y) ||
            projected[index].x < MinimumCoordinate ||
            projected[index].x > MaximumCoordinateExclusive ||
            projected[index].y < MinimumCoordinate ||
            projected[index].y > MaximumCoordinateExclusive)
        {
            return Core::failure(
                Core::CoreErrorCode::InvalidArgument,
                "A UI line vertex exceeds representable framebuffer coordinates");
        }
        points[index] = Render::UISubpixelPoint{
            .x = projected[index].x == 0.0
                     ? 0.0F
                     : static_cast<float>(projected[index].x),
            .y = projected[index].y == 0.0
                     ? 0.0F
                     : static_cast<float>(projected[index].y),
        };
    }

    float minimumX = points[0].x;
    float minimumY = points[0].y;
    float maximumX = points[0].x;
    float maximumY = points[0].y;
    for (usize index = 1; index < points.size(); ++index)
    {
        minimumX = (std::min)(minimumX, points[index].x);
        minimumY = (std::min)(minimumY, points[index].y);
        maximumX = (std::max)(maximumX, points[index].x);
        maximumY = (std::max)(maximumY, points[index].y);
    }
    const double roundedLeft = std::floor(static_cast<double>(minimumX));
    const double roundedTop = std::floor(static_cast<double>(minimumY));
    const double roundedRight = std::ceil(static_cast<double>(maximumX));
    const double roundedBottom = std::ceil(static_cast<double>(maximumY));
    if (roundedLeft < MinimumCoordinate ||
        roundedTop < MinimumCoordinate ||
        roundedLeft > static_cast<double>((std::numeric_limits<i32>::max)()) ||
        roundedTop > static_cast<double>((std::numeric_limits<i32>::max)()) ||
        roundedRight > MaximumCoordinateExclusive ||
        roundedBottom > MaximumCoordinateExclusive ||
        roundedRight <= roundedLeft || roundedBottom <= roundedTop)
    {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "A UI line AABB exceeds the pixel representation");
    }

    const i64 integerLeft = static_cast<i64>(roundedLeft);
    const i64 integerTop = static_cast<i64>(roundedTop);
    const i64 integerRight = static_cast<i64>(roundedRight);
    const i64 integerBottom = static_cast<i64>(roundedBottom);
    const i64 integerWidth = integerRight - integerLeft;
    const i64 integerHeight = integerBottom - integerTop;
    if (integerWidth > static_cast<i64>((std::numeric_limits<u32>::max)()) ||
        integerHeight > static_cast<i64>((std::numeric_limits<u32>::max)()))
    {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "A UI line extent exceeds the pixel representation");
    }

    return ProjectedQuad{
        .bounds =
            Render::UIPixelRect{
                .x = static_cast<i32>(integerLeft),
                .y = static_cast<i32>(integerTop),
                .width = static_cast<u32>(integerWidth),
                .height = static_cast<u32>(integerHeight),
            },
        .vertices =
            Render::UISolidQuadVertices{
                .topLeft = points[0],
                .topRight = points[1],
                .bottomRight = points[2],
                .bottomLeft = points[3],
            },
    };
}

[[nodiscard]] bool covers(
    const Render::UIPixelRect& outer,
    const Render::UIPixelRect& inner) noexcept
{
    const i64 outerRight = static_cast<i64>(outer.x) + outer.width;
    const i64 outerBottom = static_cast<i64>(outer.y) + outer.height;
    const i64 innerRight = static_cast<i64>(inner.x) + inner.width;
    const i64 innerBottom = static_cast<i64>(inner.y) + inner.height;
    return outer.x <= inner.x && outer.y <= inner.y &&
           outerRight >= innerRight && outerBottom >= innerBottom;
}

[[nodiscard]] u64 imageCacheHash(UI::UINodeId root, Core::AssetId asset) noexcept
{
    u64 hash = 14695981039346656037ULL;
    const auto append = [&hash](u8 byte) noexcept {
        hash ^= byte;
        hash *= 1099511628211ULL;
    };
    const auto appendU32 = [&append](u32 value) noexcept {
        for (u32 shift = 0; shift < 32; shift += 8)
        {
            append(static_cast<u8>((value >> shift) & 0xFFU));
        }
    };
    appendU32(root.ownerWindow().index());
    appendU32(root.ownerWindow().generation());
    appendU32(root.index());
    appendU32(root.generation());
    for (std::byte byte : asset.bytes())
    {
        append(std::to_integer<u8>(byte));
    }
    return hash;
}

[[nodiscard]] Core::Result<UIRenderImageResolutionCacheEntry*> resolveImageResource(
    const UI::UICommittedPaintEntry& entry, UIRenderImageBuildContext& context,
    u32& nextResourceOrdinal, UIRenderDisplayListBuildStatistics& statistics)
{
    if (!entry.root.hasValue() || !entry.imageSource.texture.hasValue())
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Committed UI image paint requires valid root and asset identities");
    }
    if (context.cache.empty())
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "UI image resolution cache capacity has been exhausted");
    }

    const usize start = static_cast<usize>(imageCacheHash(entry.root, entry.imageSource.texture) % context.cache.size());
    UIRenderImageResolutionCacheEntry* cacheEntry = nullptr;
    for (usize probe = 0; probe < context.cache.size(); ++probe)
    {
        UIRenderImageResolutionCacheEntry& candidate = context.cache[(start + probe) % context.cache.size()];
        if (candidate.state == UIRenderImageResolutionState::Empty)
        {
            cacheEntry = &candidate;
            candidate.root = entry.root;
            candidate.asset = entry.imageSource.texture;
            candidate.resourceOrdinal = nextResourceOrdinal++;
            break;
        }
        if (candidate.root == entry.root && candidate.asset == entry.imageSource.texture)
        {
            cacheEntry = &candidate;
            break;
        }
    }
    if (cacheEntry == nullptr)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "UI image resolution cache capacity has been exhausted");
    }
    if (cacheEntry->state != UIRenderImageResolutionState::Empty)
    {
        return cacheEntry;
    }

    const Render::Texture2DFrameResourceResolver* resolver =
        context.resolverLookup.find != nullptr
            ? context.resolverLookup.find(context.resolverLookup.userData, entry.root)
            : nullptr;
    if (resolver == nullptr || !resolver->hasValue() || context.resourceSink == nullptr)
    {
        cacheEntry->state = UIRenderImageResolutionState::MissingResolver;
        ++statistics.skippedImageMissingResolverCount;
        return cacheEntry;
    }
    auto resolution = resolver->resolve(resolver->userData, entry.imageSource.texture, *context.resourceSink);
    if (!resolution)
    {
        return Core::failure(std::move(resolution.error()));
    }
    if (!resolution->has_value())
    {
        cacheEntry->state = UIRenderImageResolutionState::Unavailable;
        ++statistics.skippedImageUnavailableCount;
        return cacheEntry;
    }
    if (!(**resolution).resource.hasValue() || (**resolution).pixelWidth == 0 || (**resolution).pixelHeight == 0)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "UI image resolver returned an invalid Texture2D frame resource");
    }
    cacheEntry->resolution = **resolution;
    cacheEntry->state = UIRenderImageResolutionState::Ready;
    ++statistics.resolvedImageResourceCount;
    return cacheEntry;
}

[[nodiscard]] float projectCornerRadius(
    float logicalRadius,
    const PixelProjection& projection,
    const Render::UIPixelRect& bounds) noexcept
{
    if (!(logicalRadius > 0.0F) || bounds.empty())
    {
        return 0.0F;
    }
    const double projected = static_cast<double>(logicalRadius) *
                             (std::min)(projection.scaleX, projection.scaleY);
    const float maximum = static_cast<float>((std::min)(bounds.width, bounds.height)) * 0.5F;
    return (std::min)(static_cast<float>(projected), maximum);
}

[[nodiscard]] Render::UIPixelCornerRadii projectCornerRadii(
    const UI::UILogicalCornerRadii& logicalRadii,
    const PixelProjection& projection,
    const Render::UIPixelRect& bounds) noexcept
{
    return {
        .topLeft = projectCornerRadius(logicalRadii.topLeft, projection, bounds),
        .topRight = projectCornerRadius(logicalRadii.topRight, projection, bounds),
        .bottomRight = projectCornerRadius(logicalRadii.bottomRight, projection, bounds),
        .bottomLeft = projectCornerRadius(logicalRadii.bottomLeft, projection, bounds),
    };
}

[[nodiscard]] float projectEllipseStrokeWidth(
    float logicalStrokeWidth,
    const PixelProjection& projection,
    const Render::UIPixelRect& bounds) noexcept
{
    if (!(logicalStrokeWidth > 0.0F) || bounds.empty())
    {
        return 0.0F;
    }
    const double projected = static_cast<double>(logicalStrokeWidth) *
                             (std::min)(projection.scaleX, projection.scaleY);
    const float maximum =
        static_cast<float>((std::min)(bounds.width, bounds.height)) * 0.5F;
    return (std::min)(static_cast<float>(projected), maximum);
}

[[nodiscard]] Core::Status addDebugOutline(
    Render::UIDisplayListBuilder& builder,
    const UI::UILogicalRect& logicalRect,
    const PixelProjection& projection,
    Render::UIPremultipliedRgba8 color,
    float logicalThickness,
    u64& nextPaintOrdinal,
    const std::optional<Render::UIPixelRect>& exclusion,
    UIRenderDisplayListBuildStatistics& statistics)
{
    if (!validLogicalRect(logicalRect) || logicalRect.width <= 0.0F || logicalRect.height <= 0.0F)
    {
        return Core::success();
    }
    auto projected = projectRect(logicalRect, projection);
    if (!projected || projected->empty())
    {
        return projected ? Core::success() : Core::failure(std::move(projected.error()));
    }

    const Render::UIPixelRect outer = *projected;
    const auto submitPart = [&](const Render::UIPixelRect& part) -> Core::Status {
        if (part.empty())
        {
            return Core::success();
        }
        if (nextPaintOrdinal > (std::numeric_limits<u32>::max)())
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                 "UI layout debug overlay paint ordinal capacity was exceeded");
        }
        Core::Status status = builder.addSolidQuad({
            .paintOrdinal = static_cast<u32>(nextPaintOrdinal++),
            .bounds = part,
            .color = color,
        });
        if (status)
        {
            ++statistics.submittedLayoutDebugQuadCount;
        }
        return status;
    };
    // Overlay quads carry the highest paint ordinals and no clip, so without an
    // explicit exclusion they would paint across the debugger window that owns
    // them. Split along the part's longer axis, which yields at most two pieces
    // and stays exact for outline strips: a strip is 1-2px on its short axis
    // while the excluded window is hundreds of px, so the excluded band always
    // spans the strip completely on that axis.
    const auto addPart = [&](const Render::UIPixelRect& part) -> Core::Status {
        if (part.empty())
        {
            return Core::success();
        }
        if (!exclusion.has_value() || exclusion->empty())
        {
            return submitPart(part);
        }
        const i64 partLeft = part.x;
        const i64 partTop = part.y;
        const i64 partRight = partLeft + part.width;
        const i64 partBottom = partTop + part.height;
        const i64 blockLeft = exclusion->x;
        const i64 blockTop = exclusion->y;
        const i64 blockRight = blockLeft + exclusion->width;
        const i64 blockBottom = blockTop + exclusion->height;
        if (partRight <= blockLeft || partLeft >= blockRight ||
            partBottom <= blockTop || partTop >= blockBottom)
        {
            return submitPart(part);
        }
        if (part.width >= part.height)
        {
            const Render::UIPixelRect before{
                .x = part.x,
                .y = part.y,
                .width = static_cast<u32>((std::max)(i64{0}, blockLeft - partLeft)),
                .height = part.height,
            };
            const Render::UIPixelRect after{
                .x = static_cast<i32>((std::max)(partLeft, blockRight)),
                .y = part.y,
                .width = static_cast<u32>((std::max)(i64{0}, partRight - (std::max)(partLeft, blockRight))),
                .height = part.height,
            };
            if (Core::Status status = submitPart(before); !status)
            {
                return status;
            }
            return submitPart(after);
        }
        const Render::UIPixelRect above{
            .x = part.x,
            .y = part.y,
            .width = part.width,
            .height = static_cast<u32>((std::max)(i64{0}, blockTop - partTop)),
        };
        const Render::UIPixelRect below{
            .x = part.x,
            .y = static_cast<i32>((std::max)(partTop, blockBottom)),
            .width = part.width,
            .height = static_cast<u32>((std::max)(i64{0}, partBottom - (std::max)(partTop, blockBottom))),
        };
        if (Core::Status status = submitPart(above); !status)
        {
            return status;
        }
        return submitPart(below);
    };
    const double projectedThickness = static_cast<double>(logicalThickness) *
                                      (std::min)(projection.scaleX, projection.scaleY);
    const double maximumThickness = static_cast<double>((std::min)(outer.width, outer.height));
    const u32 thickness = static_cast<u32>(
        (std::min)(maximumThickness, (std::max)(1.0, std::ceil(projectedThickness))));
    if (static_cast<u64>(outer.width) <= static_cast<u64>(thickness) * 2U ||
        static_cast<u64>(outer.height) <= static_cast<u64>(thickness) * 2U)
    {
        return addPart(outer);
    }

    const Render::UIPixelRect innerRect{
        .x = static_cast<i32>(static_cast<i64>(outer.x) + thickness),
        .y = static_cast<i32>(static_cast<i64>(outer.y) + thickness),
        .width = outer.width - thickness * 2U,
        .height = outer.height - thickness * 2U,
    };
    const i64 outerRight = static_cast<i64>(outer.x) + outer.width;
    const i64 outerBottom = static_cast<i64>(outer.y) + outer.height;
    const i64 innerRight = static_cast<i64>(innerRect.x) + innerRect.width;
    const i64 innerBottom = static_cast<i64>(innerRect.y) + innerRect.height;
    const std::array<Render::UIPixelRect, 4> parts{
        Render::UIPixelRect{.x = outer.x, .y = outer.y, .width = outer.width,
                            .height = static_cast<u32>((std::max)(i64{0},
                                static_cast<i64>(innerRect.y) - static_cast<i64>(outer.y)))},
        Render::UIPixelRect{.x = outer.x, .y = static_cast<i32>(innerBottom),
                            .width = outer.width,
                            .height = static_cast<u32>((std::max)(i64{0}, outerBottom - innerBottom))},
        Render::UIPixelRect{.x = outer.x, .y = innerRect.y,
                            .width = static_cast<u32>((std::max)(i64{0},
                                static_cast<i64>(innerRect.x) - static_cast<i64>(outer.x))),
                            .height = innerRect.height},
        Render::UIPixelRect{.x = static_cast<i32>(innerRight), .y = innerRect.y,
                            .width = static_cast<u32>((std::max)(i64{0}, outerRight - innerRight)),
                            .height = innerRect.height},
    };
    for (const Render::UIPixelRect& part : parts)
    {
        Core::Status status = addPart(part);
        if (!status)
        {
            return status;
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateLayoutDebugOverlay(
    UI::UICommittedPaintView paintView,
    const UIRenderLayoutDebugOverlay& overlay)
{
    if (!overlay.options.enabled)
    {
        return Core::success();
    }
    if (overlay.snapshot.viewportSize() != paintView.viewportSize() ||
        overlay.snapshot.structureRevision() != paintView.structureRevision() ||
        overlay.snapshot.layoutRevision() != paintView.layoutRevision())
    {
        return invalidInput(
            "The UI layout debug snapshot must match the committed paint revision and viewport");
    }
    for (const UI::UILayoutDebugEntry& entry : overlay.snapshot.entries())
    {
        if (!entry.node.hasValue() || !validLogicalRect(entry.localRect) ||
            !validLogicalRect(entry.worldRect) || !validLogicalRect(entry.effectiveClip) ||
            !validLogicalRect(entry.contentPlacement.contentBox))
        {
            return invalidInput(
                "UI layout debug entries require valid node identities and finite non-negative geometry");
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status appendLayoutDebugOverlay(
    Render::UIDisplayListBuilder& builder,
    const PixelProjection& projection,
    const UIRenderLayoutDebugOverlay& overlay,
    u64& nextPaintOrdinal,
    UIRenderDisplayListBuildStatistics& statistics)
{
    if (!overlay.options.enabled || overlay.snapshot.empty())
    {
        return Core::success();
    }

    const UI::UILayoutDebugEntry* selected = nullptr;
    const UI::UILayoutDebugEntry* excludedRoot = nullptr;
    for (const UI::UILayoutDebugEntry& entry : overlay.snapshot.entries())
    {
        if (overlay.options.excludedSubtreeRoot.hasValue() &&
            entry.node == overlay.options.excludedSubtreeRoot)
        {
            excludedRoot = &entry;
            break;
        }
    }
    u32 excludedEndPreorder = (std::numeric_limits<u32>::max)();
    if (excludedRoot != nullptr)
    {
        for (const UI::UILayoutDebugEntry& entry : overlay.snapshot.entries())
        {
            if (entry.preorder > excludedRoot->preorder && entry.depth <= excludedRoot->depth)
            {
                excludedEndPreorder = (std::min)(excludedEndPreorder, entry.preorder);
            }
        }
    }
    // Keep every outline out of the excluded window's screen area. Subtree
    // exclusion alone only suppresses that subtree's own outlines; other nodes
    // would still draw across it because overlay quads are unclipped.
    std::optional<Render::UIPixelRect> exclusionArea{};
    if (excludedRoot != nullptr &&
        excludedRoot->effectiveVisibility == UI::UIVisibility::Visible &&
        validLogicalRect(excludedRoot->worldRect) && excludedRoot->worldRect.width > 0.0F &&
        excludedRoot->worldRect.height > 0.0F)
    {
        auto projectedExclusion = projectRect(excludedRoot->worldRect, projection);
        if (!projectedExclusion)
        {
            return Core::failure(std::move(projectedExclusion.error()));
        }
        if (!projectedExclusion->empty())
        {
            exclusionArea = *projectedExclusion;
        }
    }
    for (const UI::UILayoutDebugEntry& entry : overlay.snapshot.entries())
    {
        if (excludedRoot != nullptr && entry.preorder >= excludedRoot->preorder &&
            entry.preorder < excludedEndPreorder)
        {
            // Snapshot preorder/depth makes subtree exclusion frame-local and
            // avoids retaining a second tree traversal or parent map here.
            continue;
        }
        if (overlay.options.selectedNode.hasValue() && entry.node == overlay.options.selectedNode)
        {
            selected = &entry;
        }
        if (entry.effectiveVisibility != UI::UIVisibility::Visible)
        {
            continue;
        }
        if (overlay.options.showAllVisibleBounds)
        {
            Core::Status status = addDebugOutline(
                builder, entry.worldRect, projection,
                Render::UIPremultipliedRgba8{.red = 29, .green = 54, .blue = 92, .alpha = 92},
                1.0F, nextPaintOrdinal, exclusionArea, statistics);
            if (!status)
            {
                return status;
            }
        }
    }
    if (selected == nullptr)
    {
        return Core::success();
    }
    ++statistics.layoutDebugSelectedEntryCount;

    Core::Status status = addDebugOutline(
        builder, selected->worldRect, projection,
        Render::UIPremultipliedRgba8{.red = 40, .green = 235, .blue = 255, .alpha = 255},
        2.0F, nextPaintOrdinal, exclusionArea, statistics);
    if (!status)
    {
        return status;
    }
    status = addDebugOutline(
        builder, selected->contentPlacement.contentBox, projection,
        Render::UIPremultipliedRgba8{.red = 63, .green = 216, .blue = 108, .alpha = 230},
        1.0F, nextPaintOrdinal, exclusionArea, statistics);
    if (!status)
    {
        return status;
    }
    return addDebugOutline(
        builder, selected->effectiveClip, projection,
        Render::UIPremultipliedRgba8{.red = 230, .green = 171, .blue = 45, .alpha = 230},
        1.0F, nextPaintOrdinal, exclusionArea, statistics);
}

} // namespace

Core::Result<UIRenderDisplayListBuild> buildUIDisplayList(
    Render::UIDisplayListBuilder& builder,
    UI::UICommittedPaintView paintView,
    UIRenderViewportMapping mapping,
    UIRenderImageBuildContext imageContext,
    UIRenderLayoutDebugOverlay layoutDebugOverlay)
{
    Core::Status beginStatus = builder.beginFrame();
    if (!beginStatus)
    {
        return Core::failure(std::move(beginStatus.error()));
    }
    auto rollback = Core::makeScopeExit(
        [&builder]() noexcept { builder.rollback(); });

    Core::Status paintStatus = validatePaintView(paintView);
    if (!paintStatus)
    {
        return Core::failure(std::move(paintStatus.error()));
    }
    Core::Status framebufferStatus =
        validateFramebufferViewport(mapping.framebufferViewport);
    if (!framebufferStatus)
    {
        return Core::failure(std::move(framebufferStatus.error()));
    }
    Core::Status layoutDebugStatus = validateLayoutDebugOverlay(paintView, layoutDebugOverlay);
    if (!layoutDebugStatus)
    {
        return Core::failure(std::move(layoutDebugStatus.error()));
    }

    UIRenderDisplayListBuildStatistics statistics{
        .sourcePaintEntryCount = paintView.size(),
        .sourceLayoutDebugEntryCount = layoutDebugOverlay.snapshot.size(),
    };
    for (UIRenderImageResolutionCacheEntry& entry : imageContext.cache)
    {
        entry = {};
    }
    u32 nextImageResourceOrdinal = 0;
    const UI::UILogicalSize logicalViewport = paintView.viewportSize();
    if (logicalViewport.width == 0.0F || logicalViewport.height == 0.0F ||
        mapping.framebufferViewport.empty())
    {
        auto committed = builder.commit();
        if (!committed)
        {
            return Core::failure(std::move(committed.error()));
        }
        rollback.release();
        return UIRenderDisplayListBuild{
            .displayList = *committed,
            .statistics = statistics,
        };
    }

    auto projection = makeProjection(logicalViewport, mapping.framebufferViewport);
    if (!projection)
    {
        return Core::failure(std::move(projection.error()));
    }

    u64 nextDebugPaintOrdinal = 0;
    for (const UI::UICommittedPaintEntry& entry : paintView.entries())
    {
        if (entry.paintOrdinal == (std::numeric_limits<u32>::max)())
        {
            nextDebugPaintOrdinal = static_cast<u64>((std::numeric_limits<u32>::max)()) + 1U;
        }
        else
        {
            nextDebugPaintOrdinal =
                (std::max)(nextDebugPaintOrdinal, static_cast<u64>(entry.paintOrdinal) + 1U);
        }
        const std::optional<UI::UILogicalPoint> sharedBoundsEnd =
            entry.kind == UI::UICommittedPaintKind::Image &&
                    entry.imageBoundsProjection == UI::UICommittedImageBoundsProjection::SharedBoundary
                ? std::optional<UI::UILogicalPoint>{entry.imageProjectionEnd}
                : std::nullopt;
        const bool solidLine = entry.kind == UI::UICommittedPaintKind::SolidLine;
        Render::UIPixelRect boundsValue{};
        std::optional<Render::UISolidQuadVertices> explicitVertices;
        if (solidLine)
        {
            auto projected = projectLine(entry, *projection);
            if (!projected)
            {
                return Core::failure(std::move(projected.error()));
            }
            boundsValue = projected->bounds;
            explicitVertices = projected->vertices;
        }
        else
        {
            auto bounds = projectRect(entry.worldRect, *projection, sharedBoundsEnd, false);
            if (!bounds)
            {
                return Core::failure(std::move(bounds.error()));
            }
            boundsValue = *bounds;
        }
        auto clip = projectRect(entry.effectiveClip, *projection);
        if (!clip)
        {
            return Core::failure(std::move(clip.error()));
        }

        std::optional<Render::UIPixelRect> submittedClip;
        if (!boundsValue.empty() && covers(*clip, boundsValue))
        {
            ++statistics.redundantClipElisionCount;
        } else if (!boundsValue.empty())
        {
            submittedClip = *clip;
        }

        const Render::UIPremultipliedRgba8 color{
            .red = entry.solidFill.red,
            .green = entry.solidFill.green,
            .blue = entry.solidFill.blue,
            .alpha = entry.solidFill.alpha,
        };
        if (entry.kind == UI::UICommittedPaintKind::Glyph && entry.atlasWidth > 0 && entry.atlasHeight > 0)
        {
            Core::Status addStatus = builder.addGlyphQuad({
                .paintOrdinal = entry.paintOrdinal,
                .bounds = boundsValue,
                .color = color,
                .atlasUv =
                    Render::UIPixelRect{
                        .x = static_cast<i32>(entry.atlasX),
                        .y = static_cast<i32>(entry.atlasY),
                        .width = entry.atlasWidth,
                        .height = entry.atlasHeight,
                    },
                .atlasPage = entry.atlasPage,
                .effectiveClip = submittedClip,
            });
            if (!addStatus)
            {
                return Core::failure(std::move(addStatus.error()));
            }
            ++statistics.submittedGlyphCount;
        }
        else if (entry.kind == UI::UICommittedPaintKind::Image)
        {
            auto cached = resolveImageResource(entry, imageContext, nextImageResourceOrdinal, statistics);
            if (!cached)
            {
                return Core::failure(std::move(cached.error()));
            }
            if ((*cached)->state != UIRenderImageResolutionState::Ready)
            {
                continue;
            }
            const auto& resolution = (*cached)->resolution;
            if (resolution.pixelWidth != entry.imageSource.texturePixelExtent.width ||
                resolution.pixelHeight != entry.imageSource.texturePixelExtent.height)
            {
                ++statistics.skippedImageExtentMismatchCount;
                continue;
            }
            const float inverseWidth = 1.0F / static_cast<float>(resolution.pixelWidth);
            const float inverseHeight = 1.0F / static_cast<float>(resolution.pixelHeight);
            const UI::UIImagePixelRect source = entry.imageSource.sourcePixels;
            Core::Status addStatus = builder.addImageQuad({
                .paintOrdinal = entry.paintOrdinal,
                .bounds = boundsValue,
                .color = color,
                .texture = resolution.resource,
                .resourceOrdinal = (*cached)->resourceOrdinal,
                .uv = {
                    .u0 = static_cast<float>(source.x) * inverseWidth,
                    .v0 = static_cast<float>(source.y) * inverseHeight,
                    .u1 = static_cast<float>(source.x + source.width) * inverseWidth,
                    .v1 = static_cast<float>(source.y + source.height) * inverseHeight,
                },
                .sampling = entry.imageSampling == UI::UIImageSampling::Nearest
                                ? Render::UITextureSampling::Nearest
                                : Render::UITextureSampling::Linear,
                .effectiveClip = submittedClip,
            });
            if (!addStatus)
            {
                return Core::failure(std::move(addStatus.error()));
            }
            ++statistics.submittedImageQuadCount;
        }
        else if (entry.kind == UI::UICommittedPaintKind::SolidEllipse)
        {
            Core::Status addStatus = builder.addSolidEllipse({
                .paintOrdinal = entry.paintOrdinal,
                .bounds = boundsValue,
                .color = color,
                .strokeWidth = projectEllipseStrokeWidth(
                    entry.ellipseStrokeWidth, *projection, boundsValue),
                .effectiveClip = submittedClip,
            });
            if (!addStatus)
            {
                return Core::failure(std::move(addStatus.error()));
            }
            ++statistics.submittedSolidEllipseCount;
        }
        else
        {
            Core::Status addStatus = builder.addSolidQuad({
                .paintOrdinal = entry.paintOrdinal,
                .bounds = boundsValue,
                .color = color,
                .cornerRadii = projectCornerRadii(
                    entry.cornerRadii, *projection, boundsValue),
                .vertices = explicitVertices,
                .effectiveClip = submittedClip,
            });
            if (!addStatus)
            {
                return Core::failure(std::move(addStatus.error()));
            }
            if (solidLine)
            {
                ++statistics.submittedSolidLineCount;
            }
            else
            {
                ++statistics.submittedSolidQuadCount;
            }
        }
    }

    Core::Status debugStatus = appendLayoutDebugOverlay(
        builder, *projection, layoutDebugOverlay, nextDebugPaintOrdinal, statistics);
    if (!debugStatus)
    {
        return Core::failure(std::move(debugStatus.error()));
    }

    auto committed = builder.commit();
    if (!committed)
    {
        return Core::failure(std::move(committed.error()));
    }
    rollback.release();
    return UIRenderDisplayListBuild{
        .displayList = *committed,
        .statistics = statistics,
    };
}

} // namespace Tina::Integration
