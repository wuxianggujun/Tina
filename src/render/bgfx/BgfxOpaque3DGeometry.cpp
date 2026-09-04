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
    {-1.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F},
    {1.0F, -1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, -1.0F, 1.0F, 1.0F},
    {1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F},
    {-1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F},
    // -Z
    {1.0F, -1.0F, -1.0F, 0.0F, 0.0F, -1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F},
    {-1.0F, -1.0F, -1.0F, 0.0F, 0.0F, -1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 1.0F, 1.0F},
    {-1.0F, 1.0F, -1.0F, 0.0F, 0.0F, -1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F},
    {1.0F, 1.0F, -1.0F, 0.0F, 0.0F, -1.0F, -1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F},
    // +X
    {1.0F, -1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, -1.0F, 0.0F, 1.0F},
    {1.0F, -1.0F, -1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, -1.0F, 1.0F, 1.0F},
    {1.0F, 1.0F, -1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, -1.0F, 1.0F, 0.0F},
    {1.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, -1.0F, 0.0F, 0.0F},
    // -X
    {-1.0F, -1.0F, -1.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, -1.0F, 0.0F, 1.0F},
    {-1.0F, -1.0F, 1.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, -1.0F, 1.0F, 1.0F},
    {-1.0F, 1.0F, 1.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, -1.0F, 1.0F, 0.0F},
    {-1.0F, 1.0F, -1.0F, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F},
    // +Y
    {-1.0F, 1.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F},
    {1.0F, 1.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 1.0F, 1.0F},
    {1.0F, 1.0F, -1.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F},
    {-1.0F, 1.0F, -1.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F},
    // -Y
    {-1.0F, -1.0F, -1.0F, 0.0F, -1.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F},
    {1.0F, -1.0F, -1.0F, 0.0F, -1.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 1.0F, 1.0F},
    {1.0F, -1.0F, 1.0F, 0.0F, -1.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 1.0F, 0.0F},
    {-1.0F, -1.0F, 1.0F, 0.0F, -1.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F},
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
static_assert(sizeof(BgfxOpaque3DVertex) == sizeof(float) * 12U);
static_assert(std::is_standard_layout_v<BgfxOpaque3DInstanceData>);
static_assert(sizeof(BgfxOpaque3DInstanceData) == sizeof(float) * 20U);

[[nodiscard]] bool finiteInstance(const RenderMesh3DItem& item) noexcept
{
    return std::ranges::all_of(item.columnMajorWorldTransform,
                               [](float value) noexcept { return std::isfinite(value); }) &&
           std::isfinite(opaque3DModelLinearDeterminant(item.columnMajorWorldTransform)) &&
           std::isfinite(item.baseColorFactor.red) && std::isfinite(item.baseColorFactor.green) &&
           std::isfinite(item.baseColorFactor.blue) && std::isfinite(item.baseColorFactor.alpha);
}

[[nodiscard]] bool finiteInstance(const RenderSkinnedMesh3DItem& item) noexcept
{
    return std::ranges::all_of(item.columnMajorWorldTransform,
                               [](float value) noexcept { return std::isfinite(value); }) &&
           std::isfinite(opaque3DModelLinearDeterminant(item.columnMajorWorldTransform)) &&
           std::isfinite(item.baseColorFactor.red) && std::isfinite(item.baseColorFactor.green) &&
           std::isfinite(item.baseColorFactor.blue) && std::isfinite(item.baseColorFactor.alpha);
}

[[nodiscard]] double cameraDistanceSquared(const RenderPerspectiveCamera& camera,
                                           float x, float y, float z) noexcept
{
    const double deltaX = static_cast<double>(x) - camera.positionX;
    const double deltaY = static_cast<double>(y) - camera.positionY;
    const double deltaZ = static_cast<double>(z) - camera.positionZ;
    return deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
}

[[nodiscard]] bool transparentDrawLess(const RenderTransparent3DDraw& left,
                                       const RenderTransparent3DDraw& right) noexcept
{
    if (left.cameraDistanceSquared != right.cameraDistanceSquared)
    {
        return left.cameraDistanceSquared > right.cameraDistanceSquared;
    }
    if (left.stableEntityKey != right.stableEntityKey)
    {
        return left.stableEntityKey < right.stableEntityKey;
    }
    if (left.kind != right.kind)
    {
        return left.kind < right.kind;
    }
    return left.itemIndex < right.itemIndex;
}

[[nodiscard]] Core::Status invalidScene(const char* message)
{
    return Core::failure(RenderErrorCode::InvalidRenderSceneInput, message);
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

float opaque3DModelLinearDeterminant(
    const std::array<float, 16>& columnMajorWorldTransform) noexcept
{
    const float crossX = columnMajorWorldTransform[1] * columnMajorWorldTransform[6] -
                         columnMajorWorldTransform[2] * columnMajorWorldTransform[5];
    const float crossY = columnMajorWorldTransform[2] * columnMajorWorldTransform[4] -
                         columnMajorWorldTransform[0] * columnMajorWorldTransform[6];
    const float crossZ = columnMajorWorldTransform[0] * columnMajorWorldTransform[5] -
                         columnMajorWorldTransform[1] * columnMajorWorldTransform[4];
    return crossX * columnMajorWorldTransform[8] + crossY * columnMajorWorldTransform[9] +
           crossZ * columnMajorWorldTransform[10];
}

Core::Status validateOpaque3DFrameResources(
    RenderSceneView scene, FrameResourceTableView resources) noexcept
{
    const std::span<const RenderMesh3DItem> staticItems = scene.meshes3D();
    const std::span<const RenderMesh3DItem> opaqueStaticItems = scene.opaqueMeshes3D();
    const std::span<const RenderSkinnedMesh3DItem> skinnedItems = scene.skinnedMeshes3D();
    const std::span<const RenderSkinnedMesh3DItem> opaqueSkinnedItems =
        scene.opaqueSkinnedMeshes3D();
    if ((!staticItems.empty() || !skinnedItems.empty()) &&
        !scene.perspectiveCamera().has_value())
    {
        return invalidScene("Mesh3D items require a perspective camera");
    }

    for (usize itemIndex = 0; itemIndex < staticItems.size(); ++itemIndex)
    {
        const RenderMesh3DItem& item = staticItems[itemIndex];
        const FrameResourceDescriptor* mesh =
            resources.resolve(item.mesh, FrameResourceKind::Mesh3DGeometry);
        const FrameResourceDescriptor* material =
            resources.resolve(item.material, FrameResourceKind::Mesh3DMaterial);
        if (mesh == nullptr || material == nullptr ||
            mesh->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()) ||
            material->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
        {
            return Core::failure(
                RenderErrorCode::InvalidFrameResource,
                "Mesh3D refs are stale, cross-packet, wrong-kind, or out of binding range");
        }
        const Mesh3DAlphaMode expectedAlphaMode =
            itemIndex < opaqueStaticItems.size() ? Mesh3DAlphaMode::Opaque
                                                 : Mesh3DAlphaMode::Blend;
        if (item.alphaMode != expectedAlphaMode || !finiteInstance(item))
        {
            return invalidScene(
                "Mesh3D alpha partition, transform, or color is invalid");
        }
        if (item.submeshIndex != Opaque3DFixtureSubmeshIndex)
        {
            return Core::failure(Core::CoreErrorCode::Unsupported,
                                 "Mesh3D received an unsupported submesh index");
        }
        if (item.shader)
        {
            const FrameResourceDescriptor* shader =
                resources.resolve(item.shader, FrameResourceKind::Shader);
            if (shader == nullptr ||
                shader->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "Mesh3D shader ref is stale, cross-packet, wrong-kind, or out of binding range");
            }
        }
        if (item.shaderUniforms)
        {
            if (!item.shader)
            {
                return Core::failure(RenderErrorCode::InvalidFrameResource,
                                     "Mesh3D shader uniform values require a custom shader ref");
            }
            const FrameResourceDescriptor* uniforms =
                resources.resolve(item.shaderUniforms, FrameResourceKind::ShaderUniforms);
            if (uniforms == nullptr ||
                uniforms->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "Mesh3D shader uniform ref is stale, cross-packet, wrong-kind, or out of "
                    "binding range");
            }
        }
    }

    for (usize itemIndex = 0; itemIndex < skinnedItems.size(); ++itemIndex)
    {
        const RenderSkinnedMesh3DItem& item = skinnedItems[itemIndex];
        const FrameResourceDescriptor* mesh =
            resources.resolve(item.mesh, FrameResourceKind::SkinnedMesh3DGeometry);
        const FrameResourceDescriptor* material =
            resources.resolve(item.material, FrameResourceKind::Mesh3DMaterial);
        if (mesh == nullptr || material == nullptr ||
            mesh->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()) ||
            material->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
        {
            return Core::failure(
                RenderErrorCode::InvalidFrameResource,
                "Skinned Mesh3D refs are stale, cross-packet, wrong-kind, or out of binding range");
        }
        const Mesh3DAlphaMode expectedAlphaMode =
            itemIndex < opaqueSkinnedItems.size() ? Mesh3DAlphaMode::Opaque
                                                  : Mesh3DAlphaMode::Blend;
        if (item.alphaMode != expectedAlphaMode)
        {
            return invalidScene("Skinned Mesh3D alpha partition is invalid");
        }
        if (item.shader)
        {
            const FrameResourceDescriptor* shader =
                resources.resolve(item.shader, FrameResourceKind::Shader);
            if (shader == nullptr ||
                shader->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "Skinned Mesh3D shader ref is stale, cross-packet, wrong-kind, or out of "
                    "binding range");
            }
        }
        if (item.shaderUniforms)
        {
            if (!item.shader)
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "Skinned Mesh3D shader uniform values require a custom shader ref");
            }
            const FrameResourceDescriptor* uniforms =
                resources.resolve(item.shaderUniforms, FrameResourceKind::ShaderUniforms);
            if (uniforms == nullptr ||
                uniforms->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "Skinned Mesh3D shader uniform ref is stale, cross-packet, wrong-kind, or out "
                    "of binding range");
            }
        }
    }

    const std::span<const RenderTransparent3DDraw> transparentDraws =
        scene.transparent3DDraws();
    const usize expectedTransparentCount =
        staticItems.size() - opaqueStaticItems.size() +
        skinnedItems.size() - opaqueSkinnedItems.size();
    if (transparentDraws.size() != expectedTransparentCount)
    {
        return invalidScene(
            "Transparent3D draws must completely cover the transparent item suffixes");
    }

    const RenderPerspectiveCamera* camera = scene.perspectiveCamera().has_value()
                                                ? &*scene.perspectiveCamera()
                                                : nullptr;
    for (usize drawIndex = 0; drawIndex < transparentDraws.size(); ++drawIndex)
    {
        const RenderTransparent3DDraw& draw = transparentDraws[drawIndex];
        if (camera == nullptr || !std::isfinite(draw.cameraDistanceSquared) ||
            (drawIndex != 0 && transparentDrawLess(draw, transparentDraws[drawIndex - 1U])))
        {
            return invalidScene("Transparent3D draws are not in deterministic back-to-front order");
        }

        double expectedDistanceSquared = 0.0;
        u64 expectedStableEntityKey = 0;
        switch (draw.kind)
        {
        case RenderTransparent3DDrawKind::StaticMesh:
            if (draw.itemIndex < opaqueStaticItems.size() || draw.itemIndex >= staticItems.size())
            {
                return invalidScene("Transparent3D static draw index escapes the transparent suffix");
            }
            expectedStableEntityKey = staticItems[draw.itemIndex].stableEntityKey;
            expectedDistanceSquared = cameraDistanceSquared(
                *camera, staticItems[draw.itemIndex].worldBoundsCenterX,
                staticItems[draw.itemIndex].worldBoundsCenterY,
                staticItems[draw.itemIndex].worldBoundsCenterZ);
            break;
        case RenderTransparent3DDrawKind::SkinnedMesh:
            if (draw.itemIndex < opaqueSkinnedItems.size() || draw.itemIndex >= skinnedItems.size())
            {
                return invalidScene("Transparent3D skinned draw index escapes the transparent suffix");
            }
            expectedStableEntityKey = skinnedItems[draw.itemIndex].stableEntityKey;
            expectedDistanceSquared = cameraDistanceSquared(
                *camera, skinnedItems[draw.itemIndex].worldBoundsCenterX,
                skinnedItems[draw.itemIndex].worldBoundsCenterY,
                skinnedItems[draw.itemIndex].worldBoundsCenterZ);
            break;
        default:
            return invalidScene("Transparent3D draw kind is invalid");
        }
        if (draw.stableEntityKey != expectedStableEntityKey ||
            draw.cameraDistanceSquared != expectedDistanceSquared)
        {
            return invalidScene("Transparent3D draw identity or camera distance is invalid");
        }
        if (drawIndex != 0)
        {
            const RenderTransparent3DDraw& previous = transparentDraws[drawIndex - 1U];
            if (draw.kind == previous.kind && draw.itemIndex == previous.itemIndex)
            {
                return invalidScene("Transparent3D draws contain a duplicate item");
            }
        }
    }
    return Core::success();
}

Core::Status validateSkinnedOpaque3DFrame(
    RenderSceneView scene, FrameResourceTableView resources) noexcept
{
    const std::span<const RenderSkinnedMesh3DItem> items = scene.skinnedMeshes3D();
    if (items.empty())
    {
        return Core::success();
    }
    if (!scene.perspectiveCamera().has_value())
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "Skinned Opaque3D meshes require a perspective camera");
    }
    const std::span<const float> palette = scene.skinnedMesh3DPalette();
    const u64 paletteJointCount =
        static_cast<u64>(palette.size() / SkinnedMesh3DPaletteFloatsPerJoint);
    for (const RenderSkinnedMesh3DItem& item : items)
    {
        const FrameResourceDescriptor* mesh =
            resources.resolve(item.mesh, FrameResourceKind::SkinnedMesh3DGeometry);
        const FrameResourceDescriptor* material =
            resources.resolve(item.material, FrameResourceKind::Mesh3DMaterial);
        if (mesh == nullptr || material == nullptr ||
            mesh->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()) ||
            material->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
        {
            return Core::failure(
                RenderErrorCode::InvalidFrameResource,
                "Skinned Opaque3D refs are stale, cross-packet, wrong-kind, or out of binding range");
        }
        if (item.paletteJointCount == 0 ||
            item.paletteJointCount > MaxSkinnedMesh3DPaletteJointCount ||
            static_cast<u64>(item.paletteJointOffset) + item.paletteJointCount > paletteJointCount)
        {
            return Core::failure(
                RenderErrorCode::InvalidFrameResource,
                "Skinned Opaque3D palette range escapes the committed frame pool");
        }
        if (item.submeshIndex != Opaque3DFixtureSubmeshIndex)
        {
            return Core::failure(Core::CoreErrorCode::Unsupported,
                                 "Skinned Opaque3D received an unsupported submesh index");
        }
        if (!finiteInstance(item))
        {
            return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                                 "Skinned Opaque3D item transform or color is not finite");
        }
    }
    return Core::success();
}

Core::Result<BgfxOpaque3DFrameRequirements>
checkedOpaque3DFrame(RenderSceneView scene, FrameResourceTableView resources)
{
    const std::span<const RenderMesh3DItem> meshes = scene.meshes3D();
    const std::span<const RenderMesh3DItem> opaqueMeshes = scene.opaqueMeshes3D();
    const std::span<const RenderMesh3DBatch> batches = scene.mesh3DBatches();
    if (meshes.size() > (std::numeric_limits<u32>::max)() ||
        batches.size() > (std::numeric_limits<u32>::max)())
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Opaque3D frame exceeds backend count limits");
    }
    if (auto status = validateOpaque3DFrameResources(scene, resources); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (meshes.empty())
    {
        if (!batches.empty())
        {
            return invalidFrame("An empty Opaque3D frame must not contain instance batches");
        }
        return BgfxOpaque3DFrameRequirements{};
    }
    if (!opaqueMeshes.empty() && batches.empty())
    {
        return invalidFrame("Opaque3D mesh items require instance batches");
    }

    usize nextItem = 0;
    for (const RenderMesh3DBatch& batch : batches)
    {
        if (batch.itemCount == 0 || static_cast<usize>(batch.firstItem) != nextItem ||
            static_cast<usize>(batch.itemCount) > opaqueMeshes.size() - nextItem)
        {
            return invalidFrame("Opaque3D batches must cover mesh items contiguously");
        }
        // submeshIndex remains fixture-restricted until multi-submesh product path lands.
        if (batch.submeshIndex != Opaque3DFixtureSubmeshIndex)
        {
            return Core::failure(Core::CoreErrorCode::Unsupported,
                                 "Opaque3D received an unsupported submesh index");
        }

        const usize batchEnd = nextItem + static_cast<usize>(batch.itemCount);
        for (; nextItem < batchEnd; ++nextItem)
        {
            const RenderMesh3DItem& item = opaqueMeshes[nextItem];
            if (item.mesh != batch.mesh || item.material != batch.material ||
                item.submeshIndex != batch.submeshIndex || item.doubleSided != batch.doubleSided ||
                !finiteInstance(item))
            {
                return invalidFrame("Opaque3D batch metadata does not match its mesh items");
            }
        }
    }
    if (nextItem != opaqueMeshes.size())
    {
        return invalidFrame("Opaque3D batches do not cover every opaque mesh item");
    }

    return BgfxOpaque3DFrameRequirements{
        .instanceCount = static_cast<u32>(meshes.size()),
        .batchCount = static_cast<u32>(batches.size()),
    };
}

Core::Result<BgfxOpaque3DFrameRequirements>
writeOpaque3DInstanceData(RenderSceneView scene, FrameResourceTableView resources,
                          std::span<BgfxOpaque3DInstanceData> instances)
{
    auto requirements = checkedOpaque3DFrame(scene, resources);
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
