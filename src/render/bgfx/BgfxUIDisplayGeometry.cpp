#include "BgfxUIDisplayGeometry.hpp"

#include <limits>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

inline constexpr usize VerticesPerSolidQuad = 4;
inline constexpr usize IndicesPerSolidQuad = 6;

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

} // namespace

Core::Result<BgfxUIDisplayGeometryRequirements>
checkedGeometryRequirements(UIDisplayListView displayList)
{
    const usize commandCount = displayList.commands().size();
    constexpr usize MaxUsize = (std::numeric_limits<usize>::max)();
    if (commandCount > MaxUsize / VerticesPerSolidQuad ||
        commandCount > MaxUsize / IndicesPerSolidQuad)
    {
        return geometryCapacityFailure(
            "UI DisplayList geometry requirements exceed addressable output counts");
    }

    const usize vertexCount = commandCount * VerticesPerSolidQuad;
    const usize indexCount = commandCount * IndicesPerSolidQuad;
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
        if (command.kind != UIDrawCommandKind::SolidQuad)
        {
            return Core::failure(Core::CoreErrorCode::Unsupported,
                                 "The UI DisplayList contains an unsupported draw command kind");
        }
    }

    return BgfxUIDisplayGeometryRequirements{
        .vertexCount = vertexCount,
        .indexCount = indexCount,
    };
}

Core::Result<BgfxUIDisplayGeometryRequirements>
writeGeometry(UIDisplayListView displayList, std::span<BgfxUIDisplayVertex> vertices,
              std::span<u32> indices)
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

    for (usize commandIndex = 0; commandIndex < displayList.commands().size(); ++commandIndex)
    {
        const UIDrawCommand& command = displayList.commands()[commandIndex];
        const i64 left = static_cast<i64>(command.bounds.x);
        const i64 top = static_cast<i64>(command.bounds.y);
        const i64 right = left + static_cast<i64>(command.bounds.width);
        const i64 bottom = top + static_cast<i64>(command.bounds.height);
        const u32 color = packAbgr(command.color);

        const usize vertexOffset = commandIndex * VerticesPerSolidQuad;
        vertices[vertexOffset + 0U] = {
            .x = static_cast<float>(left),
            .y = static_cast<float>(top),
            .abgr = color,
        };
        vertices[vertexOffset + 1U] = {
            .x = static_cast<float>(right),
            .y = static_cast<float>(top),
            .abgr = color,
        };
        vertices[vertexOffset + 2U] = {
            .x = static_cast<float>(right),
            .y = static_cast<float>(bottom),
            .abgr = color,
        };
        vertices[vertexOffset + 3U] = {
            .x = static_cast<float>(left),
            .y = static_cast<float>(bottom),
            .abgr = color,
        };

        const u32 absoluteVertex = static_cast<u32>(vertexOffset);
        const usize indexOffset = commandIndex * IndicesPerSolidQuad;
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
