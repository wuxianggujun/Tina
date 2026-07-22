#include "BgfxOpaque3DGeometry.hpp"

#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

constexpr std::array<BgfxOpaque3DVertex, 24> CubeVertices{{
    // +Z
    {-1.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F},
    {1.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F},
    {1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F},
    {-1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F},
    // -Z
    {1.0F, -1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F},
    {-1.0F, -1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 1.0F, 1.0F},
    {-1.0F, 1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F},
    {1.0F, 1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F},
    // +X
    {1.0F, -1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F},
    {1.0F, -1.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F},
    {1.0F, 1.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 0.0F},
    {1.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F},
    // -X
    {-1.0F, -1.0F, -1.0F, -1.0F, 0.0F, 0.0F, 0.0F, 1.0F},
    {-1.0F, -1.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 1.0F},
    {-1.0F, 1.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F},
    {-1.0F, 1.0F, -1.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F},
    // +Y
    {-1.0F, 1.0F, 1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F},
    {1.0F, 1.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F},
    {1.0F, 1.0F, -1.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F},
    {-1.0F, 1.0F, -1.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F},
    // -Y
    {-1.0F, -1.0F, -1.0F, 0.0F, -1.0F, 0.0F, 0.0F, 1.0F},
    {1.0F, -1.0F, -1.0F, 0.0F, -1.0F, 0.0F, 1.0F, 1.0F},
    {1.0F, -1.0F, 1.0F, 0.0F, -1.0F, 0.0F, 1.0F, 0.0F},
    {-1.0F, -1.0F, 1.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F},
}};

constexpr std::array<u16, 36> CubeIndices{{
    0, 1, 2, 0, 2, 3,
    4, 5, 6, 4, 6, 7,
    8, 9, 10, 8, 10, 11,
    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19,
    20, 21, 22, 20, 22, 23,
}};

static_assert(std::is_standard_layout_v<BgfxOpaque3DVertex>);
static_assert(sizeof(BgfxOpaque3DVertex) == sizeof(float) * 8U);
static_assert(std::is_standard_layout_v<BgfxOpaque3DInstanceData>);
static_assert(sizeof(BgfxOpaque3DInstanceData) == sizeof(float) * 20U);

[[nodiscard]] bool finiteInstance(const RenderMesh3DItem& item) noexcept
{
    return std::ranges::all_of(item.columnMajorWorldTransform,
                               [](float value) noexcept { return std::isfinite(value); }) &&
           std::isfinite(item.baseColorFactor.red) && std::isfinite(item.baseColorFactor.green) &&
           std::isfinite(item.baseColorFactor.blue) && std::isfinite(item.baseColorFactor.alpha);
}

[[nodiscard]] Core::Result<BgfxOpaque3DFrameRequirements>
invalidFrame(const char* message)
{
    return Core::failure(RenderErrorCode::InvalidRenderSceneInput, message);
}

} // namespace

std::span<const BgfxOpaque3DVertex> canonicalCubeVertices() noexcept
{
    return CubeVertices;
}

std::span<const u16> canonicalCubeIndices() noexcept
{
    return CubeIndices;
}

Core::Result<BgfxOpaque3DFrameRequirements>
checkedOpaque3DFrame(RenderSceneView scene)
{
    const std::span<const RenderMesh3DItem> meshes = scene.meshes3D();
    const std::span<const RenderMesh3DBatch> batches = scene.mesh3DBatches();
    if (meshes.size() > (std::numeric_limits<u32>::max)() ||
        batches.size() > (std::numeric_limits<u32>::max)())
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Opaque3D frame exceeds backend count limits");
    }
    if (meshes.empty())
    {
        if (!batches.empty())
        {
            return invalidFrame("An empty Opaque3D frame must not contain instance batches");
        }
        return BgfxOpaque3DFrameRequirements{};
    }
    if (!scene.perspectiveCamera().has_value() || batches.empty())
    {
        return invalidFrame("Opaque3D meshes require a perspective camera and instance batches");
    }

    usize nextItem = 0;
    for (const RenderMesh3DBatch& batch : batches)
    {
        if (batch.itemCount == 0 || static_cast<usize>(batch.firstItem) != nextItem ||
            static_cast<usize>(batch.itemCount) > meshes.size() - nextItem)
        {
            return invalidFrame("Opaque3D batches must cover mesh items contiguously");
        }
        // meshKey: 1 = built-in cube fixture; other non-zero keys require GPU mesh binding.
        // materialKey/submeshIndex remain fixture-restricted until Material product path lands.
        if (batch.meshKey == 0 || batch.materialKey != Opaque3DFixtureMaterialKey ||
            batch.submeshIndex != Opaque3DFixtureSubmeshIndex)
        {
            return Core::failure(Core::CoreErrorCode::Unsupported,
                                 "Opaque3D received an unsupported material/submesh fixture key");
        }

        const usize batchEnd = nextItem + static_cast<usize>(batch.itemCount);
        for (; nextItem < batchEnd; ++nextItem)
        {
            const RenderMesh3DItem& item = meshes[nextItem];
            if (item.meshKey != batch.meshKey || item.materialKey != batch.materialKey ||
                item.submeshIndex != batch.submeshIndex || item.doubleSided != batch.doubleSided ||
                !finiteInstance(item))
            {
                return invalidFrame("Opaque3D batch metadata does not match its mesh items");
            }
        }
    }
    if (nextItem != meshes.size())
    {
        return invalidFrame("Opaque3D batches do not cover every mesh item");
    }

    return BgfxOpaque3DFrameRequirements{
        .instanceCount = static_cast<u32>(meshes.size()),
        .batchCount = static_cast<u32>(batches.size()),
    };
}

Core::Result<BgfxOpaque3DFrameRequirements>
writeOpaque3DInstanceData(RenderSceneView scene,
                          std::span<BgfxOpaque3DInstanceData> instances)
{
    auto requirements = checkedOpaque3DFrame(scene);
    if (!requirements)
    {
        return Core::failure(std::move(requirements.error()));
    }
    if (instances.size() < requirements->instanceCount)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Opaque3D instance output does not have enough capacity");
    }

    for (usize index = 0; index < scene.meshes3D().size(); ++index)
    {
        const RenderMesh3DItem& item = scene.meshes3D()[index];
        instances[index] = BgfxOpaque3DInstanceData{
            .columnMajorWorldTransform = item.columnMajorWorldTransform,
            .baseColorFactor = {
                item.baseColorFactor.red,
                item.baseColorFactor.green,
                item.baseColorFactor.blue,
                item.baseColorFactor.alpha,
            },
        };
    }
    return *requirements;
}

} // namespace Tina::Render::Bgfx
