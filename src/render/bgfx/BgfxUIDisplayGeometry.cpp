#include "BgfxUIDisplayGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

inline constexpr usize VerticesPerQuad = 4;
inline constexpr usize IndicesPerQuad = 6;

[[nodiscard]] Core::Result<BgfxUIDisplayGeometryRequirements>
geometryCapacityFailure(const char* message)
{
    return Core::failure(Core::CoreErrorCode::CapacityExceeded, message);
}

[[nodiscard]] constexpr u32 packAbgr(UIPremultipliedRgba8 color) noexcept
{
    return (static_cast<u32>(color.alpha) << 24U) |
           (static_cast<u32>(color.blue) << 16U) |
           (static_cast<u32>(color.green) << 8U) |
           static_cast<u32>(color.red);
}

[[nodiscard]] Core::Result<UIAtlasPageSize> resolvePage(
    const BgfxUIAtlasPageTable& pages,
    u32 atlasPage) noexcept
{
    if (atlasPage >= pages.pageCount || atlasPage >= BgfxUIAtlasPageTable::MaxPages)
    {
        return Core::failure(Core::CoreErrorCode::Unsupported,
                             "UI Glyph command references an unknown atlas page");
    }
    const UIAtlasPageSize size = pages.pages[atlasPage];
    if (size.width == 0 || size.height == 0)
    {
        return Core::failure(Core::CoreErrorCode::Unsupported,
                             "UI Glyph atlas page size is zero");
    }
    return size;
}

[[nodiscard]] bool isSupportedCommandKind(UIDrawCommandKind kind) noexcept
{
    return kind == UIDrawCommandKind::SolidQuad || kind == UIDrawCommandKind::SolidEllipse ||
           kind == UIDrawCommandKind::Glyph ||
           kind == UIDrawCommandKind::ImageQuad;
}

[[nodiscard]] constexpr std::array<UISubpixelPoint, VerticesPerQuad> quadPoints(
    const UISolidQuadVertices& vertices) noexcept
{
    return {
        vertices.topLeft,
        vertices.topRight,
        vertices.bottomRight,
        vertices.bottomLeft,
    };
}

[[nodiscard]] bool validSolidQuadVertices(
    const UISolidQuadVertices& vertices,
    const UIPixelRect& bounds) noexcept
{
    const std::array points = quadPoints(vertices);
    const double left = static_cast<double>(bounds.x);
    const double top = static_cast<double>(bounds.y);
    const double right = left + static_cast<double>(bounds.width);
    const double bottom = top + static_cast<double>(bounds.height);
    for (const UISubpixelPoint point : points)
    {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            static_cast<double>(point.x) < left ||
            static_cast<double>(point.x) > right ||
            static_cast<double>(point.y) < top ||
            static_cast<double>(point.y) > bottom)
        {
            return false;
        }
    }

    int winding = 0;
    for (usize index = 0; index < points.size(); ++index)
    {
        const UISubpixelPoint current = points[index];
        const UISubpixelPoint next = points[(index + 1U) % points.size()];
        const UISubpixelPoint following = points[(index + 2U) % points.size()];
        const double firstX = static_cast<double>(next.x) - current.x;
        const double firstY = static_cast<double>(next.y) - current.y;
        const double secondX = static_cast<double>(following.x) - next.x;
        const double secondY = static_cast<double>(following.y) - next.y;
        const double cross = firstX * secondY - firstY * secondX;
        if (!std::isfinite(cross) || cross == 0.0)
        {
            return false;
        }
        const int edgeWinding = cross > 0.0 ? 1 : -1;
        if (winding == 0)
        {
            winding = edgeWinding;
        }
        else if (winding != edgeWinding)
        {
            return false;
        }
    }
    return true;
}

} // namespace

Core::Result<BgfxUIDisplayGeometryRequirements>
checkedGeometryRequirements(UIDisplayListView displayList)
{
    const usize commandCount = displayList.commands().size();
    constexpr usize MaxUsize = (std::numeric_limits<usize>::max)();
    if (commandCount > MaxUsize / VerticesPerQuad ||
        commandCount > MaxUsize / IndicesPerQuad)
    {
        return geometryCapacityFailure(
            "UI DisplayList geometry requirements exceed addressable output counts");
    }

    const usize vertexCount = commandCount * VerticesPerQuad;
    const usize indexCount = commandCount * IndicesPerQuad;
    if constexpr ((std::numeric_limits<usize>::max)() >
                  static_cast<usize>((std::numeric_limits<u32>::max)()))
    {
        constexpr usize MaximumAbsoluteIndexVertexCount =
            static_cast<usize>((std::numeric_limits<u32>::max)()) + usize{1};
        if (vertexCount > MaximumAbsoluteIndexVertexCount)
        {
            return geometryCapacityFailure(
                "UI DisplayList geometry exceeds absolute u32 index limits");
        }
    }

    for (const UIDrawCommand& command : displayList.commands())
    {
        if (!isSupportedCommandKind(command.kind))
        {
            return Core::failure(Core::CoreErrorCode::Unsupported,
                                 "The UI DisplayList contains an unsupported draw command kind");
        }
        const float maximumCornerRadius =
            static_cast<float>((std::min)(command.bounds.width, command.bounds.height)) * 0.5F;
        if (!std::isfinite(command.cornerRadius) || command.cornerRadius < 0.0F ||
            command.cornerRadius > maximumCornerRadius ||
            (command.kind != UIDrawCommandKind::SolidQuad && command.cornerRadius != 0.0F))
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The UI DisplayList contains an invalid corner radius");
        }
        const float maximumStrokeWidth = maximumCornerRadius;
        if (!std::isfinite(command.strokeWidth) || command.strokeWidth < 0.0F ||
            command.strokeWidth > maximumStrokeWidth ||
            (command.kind != UIDrawCommandKind::SolidEllipse && command.strokeWidth != 0.0F))
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The UI DisplayList contains an invalid ellipse stroke width");
        }
        if (command.vertices.has_value() &&
            (command.kind != UIDrawCommandKind::SolidQuad ||
             command.cornerRadius != 0.0F ||
             command.strokeWidth != 0.0F ||
             !validSolidQuadVertices(*command.vertices, command.bounds)))
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "The UI DisplayList contains invalid explicit SolidQuad vertices");
        }
    }

    return BgfxUIDisplayGeometryRequirements{
        .vertexCount = vertexCount,
        .indexCount = indexCount,
    };
}

Core::Result<BgfxUIDisplayGeometryRequirements>
writeGeometry(UIDisplayListView displayList, std::span<BgfxUIDisplayVertex> vertices,
              std::span<u32> indices, BgfxUIAtlasPageTable atlasPages)
{
    auto requirements = checkedGeometryRequirements(displayList);
    if (!requirements)
    {
        return Core::failure(std::move(requirements.error()));
    }
    if (vertices.size() < requirements->vertexCount ||
        indices.size() < requirements->indexCount)
    {
        return geometryCapacityFailure(
            "UI DisplayList geometry output spans do not have enough capacity");
    }

    // Preflight Glyph page lookups before writing so failures leave outputs
    // unmodified (callers may pass sentinel-filled buffers).
    for (const UIDrawCommand& command : displayList.commands())
    {
        if (command.kind != UIDrawCommandKind::Glyph)
        {
            if (command.kind == UIDrawCommandKind::ImageQuad)
            {
                const UINormalizedUvRect uv = command.uv;
                if (!command.texture.hasValue() || !std::isfinite(uv.u0) || !std::isfinite(uv.v0) ||
                    !std::isfinite(uv.u1) || !std::isfinite(uv.v1) || uv.u0 < 0.0F || uv.v0 < 0.0F ||
                    uv.u1 > 1.0F || uv.v1 > 1.0F || uv.u0 >= uv.u1 || uv.v0 >= uv.v1)
                {
                    return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                         "UI ImageQuad command has an invalid texture or normalized UV");
                }
            }
            continue;
        }
        auto page = resolvePage(atlasPages, command.atlasPage);
        if (!page)
        {
            return Core::failure(std::move(page.error()));
        }
        if (command.atlasUv.width == 0 || command.atlasUv.height == 0)
        {
            return Core::failure(Core::CoreErrorCode::Unsupported,
                                 "UI Glyph command has empty atlas UV");
        }
    }

    for (usize commandIndex = 0; commandIndex < displayList.commands().size(); ++commandIndex)
    {
        const UIDrawCommand& command = displayList.commands()[commandIndex];
        const i64 left = static_cast<i64>(command.bounds.x);
        const i64 top = static_cast<i64>(command.bounds.y);
        const i64 right = left + static_cast<i64>(command.bounds.width);
        const i64 bottom = top + static_cast<i64>(command.bounds.height);
        const u32 color = packAbgr(command.color);
        float shapeWidth = static_cast<float>(command.bounds.width);
        float shapeHeight = static_cast<float>(command.bounds.height);
        const float shapeParameter =
            command.kind == UIDrawCommandKind::SolidEllipse
                ? -(command.strokeWidth + 1.0F)
                : command.cornerRadius;

        float u0 = 0.0F;
        float v0 = 0.0F;
        float u1 = 1.0F;
        float v1 = 1.0F;
        if (command.kind == UIDrawCommandKind::Glyph)
        {
            auto page = resolvePage(atlasPages, command.atlasPage);
            // Preflighted above.
            const float pageW = static_cast<float>(page->width);
            const float pageH = static_cast<float>(page->height);
            u0 = static_cast<float>(command.atlasUv.x) / pageW;
            v0 = static_cast<float>(command.atlasUv.y) / pageH;
            u1 = static_cast<float>(command.atlasUv.x + static_cast<i32>(command.atlasUv.width))
                / pageW;
            v1 = static_cast<float>(command.atlasUv.y + static_cast<i32>(command.atlasUv.height))
                / pageH;
        }
        else if (command.kind == UIDrawCommandKind::ImageQuad)
        {
            u0 = command.uv.u0;
            v0 = command.uv.v0;
            u1 = command.uv.u1;
            v1 = command.uv.v1;
        }

        std::array<std::array<float, 2>, VerticesPerQuad> positions{};
        if (command.vertices.has_value())
        {
            const std::array points = quadPoints(*command.vertices);
            for (usize pointIndex = 0; pointIndex < points.size(); ++pointIndex)
            {
                positions[pointIndex] = {points[pointIndex].x, points[pointIndex].y};
            }
            shapeWidth = std::hypot(
                points[1U].x - points[0U].x,
                points[1U].y - points[0U].y);
            shapeHeight = std::hypot(
                points[3U].x - points[0U].x,
                points[3U].y - points[0U].y);
        }
        else
        {
            positions = {
                std::array<float, 2>{static_cast<float>(left), static_cast<float>(top)},
                std::array<float, 2>{static_cast<float>(right), static_cast<float>(top)},
                std::array<float, 2>{static_cast<float>(right), static_cast<float>(bottom)},
                std::array<float, 2>{static_cast<float>(left), static_cast<float>(bottom)},
            };
        }

        const usize vertexOffset = commandIndex * VerticesPerQuad;
        vertices[vertexOffset + 0U] = {
            .x = positions[0U][0],
            .y = positions[0U][1],
            .abgr = color,
            .u = u0,
            .v = v0,
            .shapeWidth = shapeWidth,
            .shapeHeight = shapeHeight,
            .shapeParameter = shapeParameter,
        };
        vertices[vertexOffset + 1U] = {
            .x = positions[1U][0],
            .y = positions[1U][1],
            .abgr = color,
            .u = u1,
            .v = v0,
            .shapeWidth = shapeWidth,
            .shapeHeight = shapeHeight,
            .shapeParameter = shapeParameter,
        };
        vertices[vertexOffset + 2U] = {
            .x = positions[2U][0],
            .y = positions[2U][1],
            .abgr = color,
            .u = u1,
            .v = v1,
            .shapeWidth = shapeWidth,
            .shapeHeight = shapeHeight,
            .shapeParameter = shapeParameter,
        };
        vertices[vertexOffset + 3U] = {
            .x = positions[3U][0],
            .y = positions[3U][1],
            .abgr = color,
            .u = u0,
            .v = v1,
            .shapeWidth = shapeWidth,
            .shapeHeight = shapeHeight,
            .shapeParameter = shapeParameter,
        };

        const u32 absoluteVertex = static_cast<u32>(vertexOffset);
        const usize indexOffset = commandIndex * IndicesPerQuad;
        indices[indexOffset + 0U] = absoluteVertex + 0U;
        indices[indexOffset + 1U] = absoluteVertex + 1U;
        indices[indexOffset + 2U] = absoluteVertex + 2U;
        indices[indexOffset + 3U] = absoluteVertex + 0U;
        indices[indexOffset + 4U] = absoluteVertex + 2U;
        indices[indexOffset + 5U] = absoluteVertex + 3U;
    }

    return *requirements;
}

} // namespace Tina::Render::Bgfx
