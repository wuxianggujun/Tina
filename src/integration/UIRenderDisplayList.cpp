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
    double pixelViewportEnd)
{
    const double logicalStart = static_cast<double>(logicalOrigin);
    const double logicalEnd = logicalStart + static_cast<double>(logicalExtent);
    const double projectedStart = pixelViewportStart + logicalStart * scale;
    const double projectedEnd = pixelViewportStart + logicalEnd * scale;
    if (!std::isfinite(projectedStart) || !std::isfinite(projectedEnd))
    {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "A committed UI rectangle cannot be represented in framebuffer coordinates");
    }

    double roundedStart = std::floor(projectedStart);
    double roundedEnd = logicalExtent == 0.0F
                            ? roundedStart
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
    const PixelProjection& projection)
{
    auto horizontal = projectAxis(
        logicalRect.x,
        logicalRect.width,
        projection.scaleX,
        projection.viewportLeft,
        projection.viewportRight);
    if (!horizontal)
    {
        return Core::failure(std::move(horizontal.error()));
    }
    auto vertical = projectAxis(
        logicalRect.y,
        logicalRect.height,
        projection.scaleY,
        projection.viewportTop,
        projection.viewportBottom);
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

} // namespace

Core::Result<UIRenderDisplayListBuild> buildUIDisplayList(
    Render::UIDisplayListBuilder& builder,
    UI::UICommittedPaintView paintView,
    UIRenderViewportMapping mapping)
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
        auto bounds = projectRect(entry.worldRect, *projection);
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

        Core::Status addStatus = builder.addSolidQuad({
            .paintOrdinal = entry.paintOrdinal,
            .bounds = *bounds,
            .color = {
                .red = entry.solidFill.red,
                .green = entry.solidFill.green,
                .blue = entry.solidFill.blue,
                .alpha = entry.solidFill.alpha,
            },
            .effectiveClip = submittedClip,
        });
        if (!addStatus)
        {
            return Core::failure(std::move(addStatus.error()));
        }
        ++statistics.submittedSolidQuadCount;
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
