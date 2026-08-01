#include <tina/integration/UIRenderDisplayList.hpp>

#include <tina/core/base/ScopeExit.hpp>

#include <algorithm>
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
                               entry.kind == UI::UICommittedPaintKind::Glyph ||
                               entry.kind == UI::UICommittedPaintKind::Image;
        if (!validKind || !std::isfinite(entry.cornerRadius) || entry.cornerRadius < 0.0F ||
            (entry.kind != UI::UICommittedPaintKind::SolidQuad && entry.cornerRadius != 0.0F))
        {
            return invalidInput(
                "Committed UI paint kind and corner radius must be valid");
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

[[nodiscard]] Core::Result<ProjectedAxis> projectAxis(
    float logicalOrigin,
    float logicalExtent,
    double scale,
    double pixelViewportStart,
    double pixelViewportEnd,
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
    roundedStart = std::clamp(
        roundedStart, pixelViewportStart, pixelViewportEnd);
    roundedEnd = std::clamp(
        roundedEnd, pixelViewportStart, pixelViewportEnd);
    if (roundedEnd < roundedStart)
    {
        roundedEnd = roundedStart;
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
    std::optional<UI::UILogicalPoint> sharedLogicalEnd = std::nullopt)
{
    auto horizontal = projectAxis(
        logicalRect.x,
        logicalRect.width,
        projection.scaleX,
        projection.viewportLeft,
        projection.viewportRight,
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

} // namespace

Core::Result<UIRenderDisplayListBuild> buildUIDisplayList(
    Render::UIDisplayListBuilder& builder,
    UI::UICommittedPaintView paintView,
    UIRenderViewportMapping mapping,
    UIRenderImageBuildContext imageContext)
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

    UIRenderDisplayListBuildStatistics statistics{
        .sourcePaintEntryCount = paintView.size(),
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

    for (const UI::UICommittedPaintEntry& entry : paintView.entries())
    {
        const std::optional<UI::UILogicalPoint> sharedBoundsEnd =
            entry.kind == UI::UICommittedPaintKind::Image &&
                    entry.imageBoundsProjection == UI::UICommittedImageBoundsProjection::SharedBoundary
                ? std::optional<UI::UILogicalPoint>{entry.imageProjectionEnd}
                : std::nullopt;
        auto bounds = projectRect(entry.worldRect, *projection, sharedBoundsEnd);
        if (!bounds)
        {
            return Core::failure(std::move(bounds.error()));
        }
        auto clip = projectRect(entry.effectiveClip, *projection);
        if (!clip)
        {
            return Core::failure(std::move(clip.error()));
        }

        std::optional<Render::UIPixelRect> submittedClip;
        if (!bounds->empty() && covers(*clip, *bounds))
        {
            ++statistics.redundantClipElisionCount;
        } else if (!bounds->empty())
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
                .bounds = *bounds,
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
                .bounds = *bounds,
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
        else
        {
            Core::Status addStatus = builder.addSolidQuad({
                .paintOrdinal = entry.paintOrdinal,
                .bounds = *bounds,
                .color = color,
                .cornerRadius = projectCornerRadius(entry.cornerRadius, *projection, *bounds),
                .effectiveClip = submittedClip,
            });
            if (!addStatus)
            {
                return Core::failure(std::move(addStatus.error()));
            }
            ++statistics.submittedSolidQuadCount;
        }
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
