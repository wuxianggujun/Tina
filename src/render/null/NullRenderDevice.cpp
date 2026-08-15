#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include "../RenderSurfaceStateTracker.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <new>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Tina::Render {
namespace {

[[nodiscard]] Core::Status validateSprite2DResources(const RenderFrame& frame) noexcept
{
    for (const RenderSprite2DItem& sprite : frame.primaryWorldScene.sprites2D())
    {
        const FrameResourceDescriptor* descriptor =
            frame.resources.resolve(sprite.texture, FrameResourceKind::Texture2D);
        if (descriptor == nullptr
            || descriptor->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
        {
            return Core::failure(
                RenderErrorCode::InvalidFrameResource,
                "NullRender Sprite2D texture ref is stale, cross-packet, wrong-kind, or out of binding range");
        }
        if (sprite.normalTexture)
        {
            const FrameResourceDescriptor* normalDescriptor =
                frame.resources.resolve(sprite.normalTexture, FrameResourceKind::Texture2D);
            if (normalDescriptor == nullptr ||
                normalDescriptor->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "NullRender Sprite2D normal texture ref is stale, cross-packet, wrong-kind, or out of binding range");
            }
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateMesh3DResources(const RenderFrame& frame) noexcept
{
    for (const RenderMesh3DItem& mesh : frame.primaryWorldScene.meshes3D())
    {
        const FrameResourceDescriptor* geometry =
            frame.resources.resolve(mesh.mesh, FrameResourceKind::Mesh3DGeometry);
        const FrameResourceDescriptor* material =
            frame.resources.resolve(mesh.material, FrameResourceKind::Mesh3DMaterial);
        if (geometry == nullptr || material == nullptr ||
            geometry->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()) ||
            material->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
        {
            return Core::failure(
                RenderErrorCode::InvalidFrameResource,
                "NullRender Mesh3D refs are stale, cross-packet, wrong-kind, or out of binding range");
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateSkinnedMesh3DItemShape(const RenderFrame& frame) noexcept
{
    const std::span<const float> palette = frame.primaryWorldScene.skinnedMesh3DPalette();
    const u64 paletteJointCount =
        static_cast<u64>(palette.size() / SkinnedMesh3DPaletteFloatsPerJoint);
    for (const RenderSkinnedMesh3DItem& mesh : frame.primaryWorldScene.skinnedMeshes3D())
    {
        const FrameResourceDescriptor* geometry =
            frame.resources.resolve(mesh.mesh, FrameResourceKind::SkinnedMesh3DGeometry);
        const FrameResourceDescriptor* material =
            frame.resources.resolve(mesh.material, FrameResourceKind::Mesh3DMaterial);
        if (geometry == nullptr || material == nullptr ||
            geometry->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()) ||
            material->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
        {
            return Core::failure(
                RenderErrorCode::InvalidFrameResource,
                "NullRender SkinnedMesh3D refs are stale, cross-packet, wrong-kind, or out of binding range");
        }
        if (mesh.paletteJointCount == 0 ||
            mesh.paletteJointCount > MaxSkinnedMesh3DPaletteJointCount ||
            static_cast<u64>(mesh.paletteJointOffset) + mesh.paletteJointCount > paletteJointCount)
        {
            return Core::failure(
                RenderErrorCode::InvalidFrameResource,
                "NullRender SkinnedMesh3D palette range escapes the committed frame pool");
        }
    }
    return Core::success();
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

[[nodiscard]] Core::Status validateTransparent3DOrder(const RenderFrame& frame) noexcept
{
    const RenderSceneView scene = frame.primaryWorldScene;
    const std::span<const RenderMesh3DItem> staticItems = scene.meshes3D();
    const std::span<const RenderMesh3DItem> opaqueStaticItems = scene.opaqueMeshes3D();
    const std::span<const RenderSkinnedMesh3DItem> skinnedItems = scene.skinnedMeshes3D();
    const std::span<const RenderSkinnedMesh3DItem> opaqueSkinnedItems =
        scene.opaqueSkinnedMeshes3D();
    if ((!staticItems.empty() || !skinnedItems.empty()) &&
        !scene.perspectiveCamera().has_value())
    {
        return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                             "NullRender Mesh3D items require a perspective camera");
    }
    for (usize index = 0; index < staticItems.size(); ++index)
    {
        const Mesh3DAlphaMode expected = index < opaqueStaticItems.size()
                                             ? Mesh3DAlphaMode::Opaque
                                             : Mesh3DAlphaMode::Blend;
        if (staticItems[index].alphaMode != expected)
        {
            return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                                 "NullRender static Mesh3D alpha partition is invalid");
        }
    }
    for (usize index = 0; index < skinnedItems.size(); ++index)
    {
        const Mesh3DAlphaMode expected = index < opaqueSkinnedItems.size()
                                             ? Mesh3DAlphaMode::Opaque
                                             : Mesh3DAlphaMode::Blend;
        if (skinnedItems[index].alphaMode != expected)
        {
            return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                                 "NullRender skinned Mesh3D alpha partition is invalid");
        }
    }

    const std::span<const RenderTransparent3DDraw> draws = scene.transparent3DDraws();
    const usize expectedDrawCount =
        staticItems.size() - opaqueStaticItems.size() +
        skinnedItems.size() - opaqueSkinnedItems.size();
    if (draws.size() != expectedDrawCount)
    {
        return Core::failure(
            RenderErrorCode::InvalidRenderSceneInput,
            "NullRender Transparent3D draws do not completely cover transparent items");
    }

    const RenderPerspectiveCamera* camera = scene.perspectiveCamera().has_value()
                                                ? &*scene.perspectiveCamera()
                                                : nullptr;
    for (usize drawIndex = 0; drawIndex < draws.size(); ++drawIndex)
    {
        const RenderTransparent3DDraw& draw = draws[drawIndex];
        if (camera == nullptr || !std::isfinite(draw.cameraDistanceSquared) ||
            (drawIndex != 0 && transparentDrawLess(draw, draws[drawIndex - 1U])))
        {
            return Core::failure(
                RenderErrorCode::InvalidRenderSceneInput,
                "NullRender Transparent3D draws are not in deterministic back-to-front order");
        }

        u64 stableEntityKey = 0;
        double distanceSquared = 0.0;
        switch (draw.kind)
        {
        case RenderTransparent3DDrawKind::StaticMesh:
            if (draw.itemIndex < opaqueStaticItems.size() ||
                draw.itemIndex >= staticItems.size())
            {
                return Core::failure(
                    RenderErrorCode::InvalidRenderSceneInput,
                    "NullRender Transparent3D static draw index is invalid");
            }
            stableEntityKey = staticItems[draw.itemIndex].stableEntityKey;
            distanceSquared = cameraDistanceSquared(
                *camera, staticItems[draw.itemIndex].worldBoundsCenterX,
                staticItems[draw.itemIndex].worldBoundsCenterY,
                staticItems[draw.itemIndex].worldBoundsCenterZ);
            break;
        case RenderTransparent3DDrawKind::SkinnedMesh:
            if (draw.itemIndex < opaqueSkinnedItems.size() ||
                draw.itemIndex >= skinnedItems.size())
            {
                return Core::failure(
                    RenderErrorCode::InvalidRenderSceneInput,
                    "NullRender Transparent3D skinned draw index is invalid");
            }
            stableEntityKey = skinnedItems[draw.itemIndex].stableEntityKey;
            distanceSquared = cameraDistanceSquared(
                *camera, skinnedItems[draw.itemIndex].worldBoundsCenterX,
                skinnedItems[draw.itemIndex].worldBoundsCenterY,
                skinnedItems[draw.itemIndex].worldBoundsCenterZ);
            break;
        default:
            return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                                 "NullRender Transparent3D draw kind is invalid");
        }
        if (draw.stableEntityKey != stableEntityKey ||
            draw.cameraDistanceSquared != distanceSquared)
        {
            return Core::failure(
                RenderErrorCode::InvalidRenderSceneInput,
                "NullRender Transparent3D draw identity or camera distance is invalid");
        }
        if (drawIndex != 0)
        {
            const RenderTransparent3DDraw& previous = draws[drawIndex - 1U];
            if (draw.kind == previous.kind && draw.itemIndex == previous.itemIndex)
            {
                return Core::failure(RenderErrorCode::InvalidRenderSceneInput,
                                     "NullRender Transparent3D draws contain a duplicate item");
            }
        }
    }
    return Core::success();
}

class NullRenderDevice final : public IRenderDevice {
  public:
    explicit NullRenderDevice(Detail::RenderSurfaceStateTracker surfaceStateTracker) noexcept
        : surfaceStateTracker_(std::move(surfaceStateTracker))
    {
    }

    [[nodiscard]] Core::Result<RenderFrameSubmission> submitFrame(const RenderFrame& frame) override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (frameOpen_)
        {
            return Core::failure(RenderErrorCode::FrameAlreadyOpen,
                                 "The previously submitted frame has not been presented");
        }
        if (frame.frameIndex != nextFrameIndex_)
        {
            return Core::failure(RenderErrorCode::UnexpectedFrameIndex,
                                 "Render frame indices must be contiguous and begin at zero");
        }
        if (auto status = validateSprite2DResources(frame); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateMesh3DResources(frame); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateSkinnedMesh3DItemShape(frame); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateTransparent3DOrder(frame); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateMesh3DMaterialAlphaBindings(frame); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateSkinnedMesh3DBindings(frame); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateUIResources(frame); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (frame.primaryWorldScene.sprite2DLighting().has_value())
        {
            if (auto status = validateSprite2DLightingDesc(
                    frame.primaryWorldScene.sprite2DLighting()->descriptor());
                !status)
            {
                return Core::failure(std::move(status.error()));
            }
        }
        if (frame.primaryWorldScene.mesh3DLighting().has_value())
        {
            if (auto status = validateMesh3DLightingDesc(
                    frame.primaryWorldScene.mesh3DLighting()->descriptor());
                !status)
            {
                return Core::failure(std::move(status.error()));
            }
        }

        if (auto status = surfaceStateTracker_.validateAndCommit(frame.primaryWindowSurface); !status)
        {
            return Core::failure(std::move(status.error()));
        }

        ++nextFrameIndex_;
        if (frame.primaryWindowSurface.has_value() &&
            frame.primaryWindowSurface->availability == RenderSurfaceAvailability::Suspended)
        {
            ++statistics_.skippedSuspendedSurfaceFrames;
            return RenderFrameSubmission::SkippedSuspendedSurface();
        }

        frameOpen_ = true;
        ++statistics_.submitted;
        return RenderFrameSubmission::Submitted(nextSubmissionIndex_++);
    }

    [[nodiscard]] Core::Status present() override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!frameOpen_)
        {
            return Core::failure(RenderErrorCode::NoFrameSubmitted,
                                 "A frame must be submitted before it can be presented");
        }

        frameOpen_ = false;
        ++statistics_.presented;
        return Core::success();
    }

    [[nodiscard]] RenderStatistics statistics() const noexcept override
    {
        return statistics_;
    }

    [[nodiscard]] Core::Result<GpuTextureId> createTexture2DRgba8(const Texture2DUploadDesc& desc) override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (desc.width == 0 || desc.height == 0 ||
            desc.rgba8Pixels.size() != static_cast<std::size_t>(desc.width) * desc.height * 4U)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "invalid Texture2D RGBA8 upload descriptor");
        }
        const u32 index = static_cast<u32>(textures_.size());
        textures_.push_back(TextureSlot{.generation = 1, .width = desc.width, .height = desc.height, .live = true});
        ++statistics_.liveResources;
        return GpuTextureId{resourceOwnerId(), index, 1};
    }

    [[nodiscard]] Core::Status validateTexture2D(GpuTextureId texture) const noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!texture || !isLiveTexture(texture))
        {
            return Core::failure(RenderErrorCode::TextureNotFound,
                                 "Texture2D handle is invalid, stale, or belongs to another device");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status destroyTexture2D(GpuTextureId texture) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!texture || texture.owner != resourceOwnerId() || texture.index >= textures_.size() ||
            !textures_[texture.index].live ||
            textures_[texture.index].generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid or already destroyed");
        }
        textures_[texture.index].live = false;
        ++textures_[texture.index].generation;
        if (statistics_.liveResources > 0)
        {
            --statistics_.liveResources;
        }
        for (auto it = textureBindings_.begin(); it != textureBindings_.end();)
        {
            if (it->second == texture)
            {
                it = textureBindings_.erase(it);
            } else
            {
                ++it;
            }
        }
        for (auto& entry : materialBindings_)
        {
            Mesh3DMaterialBindingDesc& binding = entry.second;
            if (binding.baseColorTexture == texture)
            {
                binding.baseColorTexture = {};
            }
            if (binding.metallicRoughnessTexture == texture)
            {
                binding.metallicRoughnessTexture = {};
            }
            if (binding.normalTexture == texture)
            {
                binding.normalTexture = {};
            }
        }
        ++statistics_.completedGpuRetirements;
        return Core::success();
    }

    [[nodiscard]] Core::Result<GpuEnvironmentMapId>
    createEnvironmentMap(const EnvironmentMapUploadDesc& desc) override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (auto status = validateEnvironmentMapUploadDesc(desc); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (environmentMaps_.size() >= (std::numeric_limits<u32>::max)())
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                 "EnvironmentMap logical slot index space is exhausted");
        }

        const u32 index = static_cast<u32>(environmentMaps_.size());
        try
        {
            environmentMaps_.push_back(EnvironmentMapSlot{.generation = 1, .live = true});
        }
        catch (const std::bad_alloc&)
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory);
        }
        catch (const std::length_error&)
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded);
        }
        ++statistics_.liveResources;
        return GpuEnvironmentMapId{resourceOwnerId(), index, 1};
    }

    [[nodiscard]] Core::Status
    validateEnvironmentMap(GpuEnvironmentMapId environmentMap) const noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!environmentMap || !isLiveEnvironmentMap(environmentMap))
        {
            return Core::failure(
                RenderErrorCode::EnvironmentMapNotFound,
                "EnvironmentMap handle is invalid, stale, or belongs to another device");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status
    destroyEnvironmentMap(GpuEnvironmentMapId environmentMap) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!environmentMap || !isLiveEnvironmentMap(environmentMap))
        {
            return Core::failure(RenderErrorCode::EnvironmentMapNotFound,
                                 "EnvironmentMap handle is invalid or already destroyed");
        }

        EnvironmentMapSlot& slot = environmentMaps_[environmentMap.index];
        slot.live = false;
        ++slot.generation;
        if (mesh3DImageBasedLighting_.has_value() &&
            mesh3DImageBasedLighting_->environmentMap == environmentMap)
        {
            mesh3DImageBasedLighting_.reset();
        }
        if (statistics_.liveResources > 0)
        {
            --statistics_.liveResources;
        }
        ++statistics_.completedGpuRetirements;
        return Core::success();
    }

    [[nodiscard]] Core::Status setTexture2DBinding(u32 deviceBindingKey,
                                                   GpuTextureId texture) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (deviceBindingKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "Texture2D device binding key must be non-zero");
        }
        if (!texture)
        {
            textureBindings_.erase(deviceBindingKey);
            return Core::success();
        }
        if (texture.owner != resourceOwnerId() || texture.index >= textures_.size() ||
            !textures_[texture.index].live ||
            textures_[texture.index].generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        textureBindings_[deviceBindingKey] = texture;
        return Core::success();
    }

    [[nodiscard]] Core::Result<GpuMeshId> createStaticMesh(const StaticMeshUploadDesc& desc) override
    {
        return createStaticMeshRecord(desc);
    }

    [[nodiscard]] Core::Result<GpuMeshId> createSkinnedMesh(const SkinnedMeshUploadDesc& desc) override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        constexpr u16 MaxJointCount = static_cast<u16>(MaxSkinnedMesh3DPaletteJointCount);
        constexpr u32 InfluencesPerVertex = 4;
        if (desc.jointCount == 0 || desc.jointCount > MaxJointCount ||
            desc.vertexCount > (std::numeric_limits<u32>::max)() / InfluencesPerVertex ||
            desc.jointIndices.size() !=
                static_cast<std::size_t>(desc.vertexCount) * InfluencesPerVertex ||
            desc.jointWeights.size() !=
                static_cast<std::size_t>(desc.vertexCount) * InfluencesPerVertex)
        {
            return Core::failure(RenderErrorCode::InvalidMeshUpload,
                                 "invalid SkinnedMesh upload skin stream shape");
        }
        for (const u16 jointIndex : desc.jointIndices)
        {
            if (jointIndex >= desc.jointCount)
            {
                return Core::failure(RenderErrorCode::InvalidMeshUpload,
                                     "SkinnedMesh joint index out of range");
            }
        }
        for (std::size_t vertexIndex = 0; vertexIndex < desc.vertexCount; ++vertexIndex)
        {
            u32 weightSum = 0;
            for (std::size_t influence = 0; influence < InfluencesPerVertex; ++influence)
            {
                weightSum += desc.jointWeights[vertexIndex * InfluencesPerVertex + influence];
            }
            if (weightSum != 0xFFFFU)
            {
                return Core::failure(RenderErrorCode::InvalidMeshUpload,
                                     "SkinnedMesh vertex weights must sum to 0xFFFF");
            }
        }

        auto meshId = createStaticMeshRecord(StaticMeshUploadDesc{
            .vertexCount = desc.vertexCount,
            .indexCount = desc.indexCount,
            .vertices = desc.vertices,
            .indices = desc.indices,
        });
        if (!meshId)
        {
            return meshId;
        }
        MeshSlot& slot = meshes_[meshId->index];
        slot.skinned = true;
        slot.jointCount = desc.jointCount;
        return meshId;
    }

    [[nodiscard]] Core::Status destroyStaticMesh(GpuMeshId mesh) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!mesh || mesh.owner != resourceOwnerId() || mesh.index >= meshes_.size() ||
            !meshes_[mesh.index].live ||
            meshes_[mesh.index].generation != mesh.generation)
        {
            return Core::failure(RenderErrorCode::MeshNotFound, "StaticMesh handle is invalid or already destroyed");
        }
        meshes_[mesh.index].live = false;
        ++meshes_[mesh.index].generation;
        if (statistics_.liveResources > 0)
        {
            --statistics_.liveResources;
        }
        for (auto it = meshBindings_.begin(); it != meshBindings_.end();)
        {
            if (it->second == mesh)
            {
                it = meshBindings_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        ++statistics_.completedGpuRetirements;
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DBinding(u32 meshKey, GpuMeshId mesh) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (meshKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidMeshUpload, "meshKey must be non-zero");
        }
        if (!mesh)
        {
            meshBindings_.erase(meshKey);
            return Core::success();
        }
        if (mesh.owner != resourceOwnerId() || mesh.index >= meshes_.size() ||
            !meshes_[mesh.index].live ||
            meshes_[mesh.index].generation != mesh.generation)
        {
            return Core::failure(RenderErrorCode::MeshNotFound, "StaticMesh handle is invalid");
        }
        meshBindings_[meshKey] = mesh;
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DMaterialBinding(
        u32 materialKey, const Mesh3DMaterialBindingDesc& desc) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }
        if (!(desc.metallicFactor >= 0.0F && desc.metallicFactor <= 1.0F) ||
            !(desc.roughnessFactor >= 0.0F && desc.roughnessFactor <= 1.0F) ||
            !std::isfinite(desc.metallicFactor) || !std::isfinite(desc.roughnessFactor) ||
            !isSupportedMesh3DAlphaMode(desc.alphaMode))
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "Mesh3D material factors or alpha mode are invalid");
        }

        if (!isLiveTexture(desc.baseColorTexture) || !isLiveTexture(desc.metallicRoughnessTexture) ||
            !isLiveTexture(desc.normalTexture))
        {
            return Core::failure(RenderErrorCode::TextureNotFound,
                                 "Mesh3D material binding contains an invalid Texture2D handle");
        }
        return commitMaterialBinding(materialKey, desc);
    }

    [[nodiscard]] Core::Status clearMesh3DMaterialBinding(u32 materialKey) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }

        materialBindings_.erase(materialKey);
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DMaterialTextureBinding(u32 materialKey, GpuTextureId texture) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }
        if (!texture)
        {
            const auto existing = materialBindings_.find(materialKey);
            if (existing == materialBindings_.end())
            {
                return Core::success();
            }
            Mesh3DMaterialBindingDesc binding = existing->second;
            binding.baseColorTexture = {};
            return commitMaterialBinding(materialKey, binding);
        }
        if (!isLiveTexture(texture))
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        Mesh3DMaterialBindingDesc binding = materialBindingOrDefault(materialKey);
        binding.baseColorTexture = texture;
        return commitMaterialBinding(materialKey, binding);
    }

    [[nodiscard]] Core::Status setMesh3DMaterialFactors(u32 materialKey, float metallic,
                                                        float roughness) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }
        if (!(metallic >= 0.0F && metallic <= 1.0F) || !(roughness >= 0.0F && roughness <= 1.0F) ||
            !std::isfinite(metallic) || !std::isfinite(roughness))
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "metallic and roughness must be finite values in [0,1]");
        }
        Mesh3DMaterialBindingDesc binding = materialBindingOrDefault(materialKey);
        binding.metallicFactor = metallic;
        binding.roughnessFactor = roughness;
        return commitMaterialBinding(materialKey, binding);
    }

    [[nodiscard]] Core::Status setMesh3DMaterialNormalTextureBinding(u32 materialKey,
                                                                     GpuTextureId texture) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }
        if (!texture)
        {
            const auto existing = materialBindings_.find(materialKey);
            if (existing == materialBindings_.end())
            {
                return Core::success();
            }
            Mesh3DMaterialBindingDesc binding = existing->second;
            binding.normalTexture = {};
            return commitMaterialBinding(materialKey, binding);
        }
        if (!isLiveTexture(texture))
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        Mesh3DMaterialBindingDesc binding = materialBindingOrDefault(materialKey);
        binding.normalTexture = texture;
        return commitMaterialBinding(materialKey, binding);
    }

    [[nodiscard]] Core::Status setMesh3DLighting(const Mesh3DLightingDesc& lighting) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (auto status = validateMesh3DLightingDesc(lighting); !status)
        {
            return status;
        }

        directionalLightCount_ = lighting.directionalLights.size();
        for (std::size_t lightIndex = 0; lightIndex < directionalLights_.size(); ++lightIndex)
        {
            if (lightIndex >= directionalLightCount_)
            {
                directionalLights_[lightIndex] = {};
                continue;
            }

            const Mesh3DDirectionalLight& source = lighting.directionalLights[lightIndex];
            const float lengthSquared =
                source.directionTowardLightX * source.directionTowardLightX +
                source.directionTowardLightY * source.directionTowardLightY +
                source.directionTowardLightZ * source.directionTowardLightZ;
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            directionalLights_[lightIndex] = Mesh3DDirectionalLight{
                .directionTowardLightX = source.directionTowardLightX * inverseLength,
                .directionTowardLightY = source.directionTowardLightY * inverseLength,
                .directionTowardLightZ = source.directionTowardLightZ * inverseLength,
                .colorR = source.colorR,
                .colorG = source.colorG,
                .colorB = source.colorB,
            };
        }
        pointLightCount_ = lighting.pointLights.size();
        for (std::size_t lightIndex = 0; lightIndex < pointLights_.size(); ++lightIndex)
        {
            pointLights_[lightIndex] =
                lightIndex < pointLightCount_ ? lighting.pointLights[lightIndex] : Mesh3DPointLight{};
        }
        spotLightCount_ = lighting.spotLights.size();
        for (std::size_t lightIndex = 0; lightIndex < spotLights_.size(); ++lightIndex)
        {
            if (lightIndex >= spotLightCount_)
            {
                spotLights_[lightIndex] = {};
                continue;
            }

            const Mesh3DSpotLight& source = lighting.spotLights[lightIndex];
            const float lengthSquared = source.directionFromLightX * source.directionFromLightX +
                                        source.directionFromLightY * source.directionFromLightY +
                                        source.directionFromLightZ * source.directionFromLightZ;
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            spotLights_[lightIndex] = Mesh3DSpotLight{
                .positionX = source.positionX,
                .positionY = source.positionY,
                .positionZ = source.positionZ,
                .influenceRadius = source.influenceRadius,
                .directionFromLightX = source.directionFromLightX * inverseLength,
                .directionFromLightY = source.directionFromLightY * inverseLength,
                .directionFromLightZ = source.directionFromLightZ * inverseLength,
                .innerConeCosine = source.innerConeCosine,
                .outerConeCosine = source.outerConeCosine,
                .colorR = source.colorR,
                .colorG = source.colorG,
                .colorB = source.colorB,
            };
        }
        cascadedDirectionalShadow_ = lighting.cascadedDirectionalShadow;
        pointLightShadow_ = lighting.pointLightShadow;
        spotLightShadow_ = lighting.spotLightShadow;
        ambientScale_ = lighting.ambientScale;
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DImageBasedLighting(
        const Mesh3DImageBasedLightingDesc& lighting) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!lighting.environmentMap || !isLiveEnvironmentMap(lighting.environmentMap))
        {
            return Core::failure(RenderErrorCode::EnvironmentMapNotFound,
                                 "Mesh3D IBL requires a live EnvironmentMap handle");
        }
        if (!std::isfinite(lighting.intensity) || lighting.intensity < 0.0F ||
            !std::isfinite(lighting.rotationRadians))
        {
            return Core::failure(RenderErrorCode::InvalidEnvironmentMapUpload,
                                 "Mesh3D IBL parameters must be finite and intensity non-negative");
        }
        mesh3DImageBasedLighting_ = lighting;
        return Core::success();
    }

    [[nodiscard]] Core::Status clearMesh3DImageBasedLighting() noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        mesh3DImageBasedLighting_.reset();
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DMaterialMetallicRoughnessTextureBinding(
        u32 materialKey, GpuTextureId texture) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }
        if (!texture)
        {
            const auto existing = materialBindings_.find(materialKey);
            if (existing == materialBindings_.end())
            {
                return Core::success();
            }
            Mesh3DMaterialBindingDesc binding = existing->second;
            binding.metallicRoughnessTexture = {};
            return commitMaterialBinding(materialKey, binding);
        }
        if (!isLiveTexture(texture))
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        Mesh3DMaterialBindingDesc binding = materialBindingOrDefault(materialKey);
        binding.metallicRoughnessTexture = texture;
        return commitMaterialBinding(materialKey, binding);
    }

    void shutdown() noexcept override
    {
        stopped_ = true;
        frameOpen_ = false;
        textureBindings_.clear();
        textures_.clear();
        mesh3DImageBasedLighting_.reset();
        environmentMaps_.clear();
        meshBindings_.clear();
        materialBindings_.clear();
        meshes_.clear();
        statistics_.liveResources = 0;
    }

  private:
    struct TextureSlot final {
        u32 generation = 1;
        u16 width = 0;
        u16 height = 0;
        bool live = false;
    };
    struct MeshSlot final {
        u32 generation = 1;
        u32 vertexCount = 0;
        u32 indexCount = 0;
        u16 jointCount = 0;
        bool skinned = false;
        bool live = false;
    };
    struct EnvironmentMapSlot final {
        u32 generation = 1;
        bool live = false;
    };

    [[nodiscard]] Core::Result<GpuMeshId> createStaticMeshRecord(const StaticMeshUploadDesc& desc)
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        constexpr std::size_t MaxUploadBytes = (std::numeric_limits<u32>::max)();
        constexpr std::size_t FloatsPerVertex = 12U;
        const std::size_t vertexStrideBytes = FloatsPerVertex * sizeof(float);
        if (desc.vertexCount == 0 || desc.indexCount == 0 ||
            (desc.indexCount % 3U) != 0U ||
            desc.vertexCount > (std::numeric_limits<std::size_t>::max)() / FloatsPerVertex ||
            desc.vertices.size() != static_cast<std::size_t>(desc.vertexCount) * FloatsPerVertex ||
            desc.indices.size() != desc.indexCount ||
            desc.vertexCount > MaxUploadBytes / vertexStrideBytes ||
            desc.indexCount > MaxUploadBytes / sizeof(u16))
        {
            return Core::failure(RenderErrorCode::InvalidMeshUpload, "invalid StaticMesh upload descriptor");
        }
        for (const float value : desc.vertices)
        {
            if (!std::isfinite(value))
            {
                return Core::failure(RenderErrorCode::InvalidMeshUpload, "StaticMesh vertices must be finite");
            }
        }
        constexpr float MinimumTangentLengthSquared = 1.0e-12F;
        for (std::size_t vertexIndex = 0; vertexIndex < desc.vertexCount; ++vertexIndex)
        {
            const std::size_t tangentOffset = vertexIndex * FloatsPerVertex + 6U;
            const float tangentX = desc.vertices[tangentOffset];
            const float tangentY = desc.vertices[tangentOffset + 1U];
            const float tangentZ = desc.vertices[tangentOffset + 2U];
            const float tangentHandedness = desc.vertices[tangentOffset + 3U];
            const float tangentLengthSquared =
                tangentX * tangentX + tangentY * tangentY + tangentZ * tangentZ;
            if (!std::isfinite(tangentLengthSquared) ||
                tangentLengthSquared <= MinimumTangentLengthSquared ||
                (tangentHandedness != -1.0F && tangentHandedness != 1.0F))
            {
                return Core::failure(
                    RenderErrorCode::InvalidMeshUpload,
                    "StaticMesh vertex tangents require non-zero xyz and -1 or +1 handedness");
            }
        }
        for (const u16 index : desc.indices)
        {
            if (static_cast<u32>(index) >= desc.vertexCount)
            {
                return Core::failure(RenderErrorCode::InvalidMeshUpload, "StaticMesh index out of range");
            }
        }

        if (meshes_.size() >= (std::numeric_limits<u32>::max)())
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                 "StaticMesh logical slot index space is exhausted");
        }
        const u32 slotIndex = static_cast<u32>(meshes_.size());
        try
        {
            meshes_.push_back(MeshSlot{.generation = 1,
                                       .vertexCount = desc.vertexCount,
                                       .indexCount = desc.indexCount,
                                       .live = true});
        }
        catch (const std::bad_alloc&)
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory);
        }
        catch (const std::length_error&)
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded);
        }
        ++statistics_.liveResources;
        return GpuMeshId{resourceOwnerId(), slotIndex, 1};
    }

    [[nodiscard]] Core::Status validateMesh3DMaterialAlphaBindings(
        const RenderFrame& frame) const noexcept
    {
        const auto validateItems = [this, &frame](const auto items) noexcept -> Core::Status {
            for (const auto& item : items)
            {
                const FrameResourceDescriptor* material = frame.resources.resolve(
                    item.material, FrameResourceKind::Mesh3DMaterial);
                if (material == nullptr ||
                    material->deviceBindingKey >
                        static_cast<u64>((std::numeric_limits<u32>::max)()))
                {
                    return Core::failure(
                        RenderErrorCode::InvalidFrameResource,
                        "NullRender Mesh3D item references an invalid material resource");
                }
                const u32 materialKey = static_cast<u32>(material->deviceBindingKey);
                const auto binding = materialBindings_.find(materialKey);
                const Mesh3DAlphaMode resolvedAlphaMode =
                    binding == materialBindings_.end() ? Mesh3DAlphaMode::Opaque
                                                       : binding->second.alphaMode;
                if (resolvedAlphaMode != item.alphaMode)
                {
                    return Core::failure(
                        RenderErrorCode::InvalidFrameResource,
                        "NullRender Mesh3D item alpha mode does not match its material binding");
                }
            }
            return Core::success();
        };

        if (auto status = validateItems(frame.primaryWorldScene.meshes3D()); !status)
        {
            return status;
        }
        return validateItems(frame.primaryWorldScene.skinnedMeshes3D());
    }

    // Binding-level preflight: a bound static key must map to a non-skinned
    // slot, a bound skinned key must map to a skinned slot whose joint count
    // matches the item palette. Unbound keys stay tolerated like the static
    // path (bindings may arrive later; bgfx substitutes its fixture).
    [[nodiscard]] Core::Status validateSkinnedMesh3DBindings(const RenderFrame& frame) const noexcept
    {
        for (const RenderMesh3DItem& mesh : frame.primaryWorldScene.meshes3D())
        {
            const FrameResourceDescriptor* geometry =
                frame.resources.resolve(mesh.mesh, FrameResourceKind::Mesh3DGeometry);
            if (geometry == nullptr)
            {
                continue;
            }
            const MeshSlot* slot = boundMeshSlot(static_cast<u32>(geometry->deviceBindingKey));
            if (slot != nullptr && slot->skinned)
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "NullRender static Mesh3D item resolves a skinned mesh binding");
            }
        }
        for (const RenderSkinnedMesh3DItem& mesh : frame.primaryWorldScene.skinnedMeshes3D())
        {
            const FrameResourceDescriptor* geometry =
                frame.resources.resolve(mesh.mesh, FrameResourceKind::SkinnedMesh3DGeometry);
            if (geometry == nullptr)
            {
                continue;
            }
            const MeshSlot* slot = boundMeshSlot(static_cast<u32>(geometry->deviceBindingKey));
            if (slot == nullptr)
            {
                continue;
            }
            if (!slot->skinned)
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "NullRender SkinnedMesh3D item resolves a non-skinned mesh binding");
            }
            if (slot->jointCount != mesh.paletteJointCount)
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "NullRender SkinnedMesh3D palette joint count does not match the bound skeleton");
            }
        }
        return Core::success();
    }

    [[nodiscard]] const MeshSlot* boundMeshSlot(u32 meshKey) const noexcept
    {
        const auto binding = meshBindings_.find(meshKey);
        if (binding == meshBindings_.end())
        {
            return nullptr;
        }
        const GpuMeshId id = binding->second;
        if (id.index >= meshes_.size() || !meshes_[id.index].live ||
            meshes_[id.index].generation != id.generation)
        {
            return nullptr;
        }
        return &meshes_[id.index];
    }

    [[nodiscard]] Core::Status validateUIResources(const RenderFrame& frame) const noexcept
    {
        for (const UIDrawCommand& command : frame.primaryWindowUIDisplayList.commands())
        {
            if (command.kind != UIDrawCommandKind::ImageQuad)
            {
                continue;
            }
            const FrameResourceDescriptor* descriptor =
                frame.resources.resolve(command.texture, FrameResourceKind::Texture2D);
            if (descriptor == nullptr ||
                descriptor->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "NullRender UI image texture ref is stale, cross-packet, wrong-kind, or out of binding range");
            }
            const auto binding = textureBindings_.find(static_cast<u32>(descriptor->deviceBindingKey));
            if (binding == textureBindings_.end() || !binding->second || !isLiveTexture(binding->second))
            {
                return Core::failure(RenderErrorCode::InvalidFrameResource,
                                     "NullRender UI image texture binding is missing or no longer live");
            }
        }
        return Core::success();
    }

    [[nodiscard]] bool isLiveTexture(GpuTextureId texture) const noexcept
    {
        return !texture || (texture.owner == resourceOwnerId() && texture.index < textures_.size() &&
                            textures_[texture.index].live &&
                            textures_[texture.index].generation == texture.generation);
    }

    [[nodiscard]] bool isLiveEnvironmentMap(GpuEnvironmentMapId environmentMap) const noexcept
    {
        return environmentMap.owner == resourceOwnerId() &&
               environmentMap.index < environmentMaps_.size() &&
               environmentMaps_[environmentMap.index].live &&
               environmentMaps_[environmentMap.index].generation == environmentMap.generation;
    }

    [[nodiscard]] Mesh3DMaterialBindingDesc materialBindingOrDefault(u32 materialKey) const noexcept
    {
        const auto existing = materialBindings_.find(materialKey);
        return existing == materialBindings_.end() ? Mesh3DMaterialBindingDesc{} : existing->second;
    }

    [[nodiscard]] Core::Status commitMaterialBinding(
        u32 materialKey, const Mesh3DMaterialBindingDesc& binding) noexcept
    {
        try
        {
            materialBindings_.insert_or_assign(materialKey, binding);
            return Core::success();
        }
        catch (const std::bad_alloc&)
        {
            return Core::failure(Core::CoreErrorCode::OutOfMemory);
        }
        catch (const std::length_error&)
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded);
        }
    }

    Detail::RenderSurfaceStateTracker surfaceStateTracker_;
    RenderStatistics statistics_{};
    std::vector<TextureSlot> textures_{};
    std::unordered_map<u32, GpuTextureId> textureBindings_{};
    std::vector<EnvironmentMapSlot> environmentMaps_{};
    std::optional<Mesh3DImageBasedLightingDesc> mesh3DImageBasedLighting_{};
    std::vector<MeshSlot> meshes_{};
    std::unordered_map<u32, GpuMeshId> meshBindings_{};
    std::unordered_map<u32, Mesh3DMaterialBindingDesc> materialBindings_{};
    std::array<Mesh3DDirectionalLight, Mesh3DLightingDesc::MaximumDirectionalLightCount> directionalLights_{
        Mesh3DDirectionalLight{
            .directionTowardLightX = 0.4F,
            .directionTowardLightY = 0.85F,
            .directionTowardLightZ = 0.35F,
            .colorR = 1.0F,
            .colorG = 0.98F,
            .colorB = 0.92F,
        }};
    std::size_t directionalLightCount_ = 1;
    std::array<Mesh3DPointLight, Mesh3DLightingDesc::MaximumPointLightCount> pointLights_{};
    std::size_t pointLightCount_ = 0;
    std::array<Mesh3DSpotLight, Mesh3DLightingDesc::MaximumSpotLightCount> spotLights_{};
    std::size_t spotLightCount_ = 0;
    std::optional<Mesh3DCascadedDirectionalShadow> cascadedDirectionalShadow_{};
    std::optional<Mesh3DPointLightShadow> pointLightShadow_{};
    std::optional<Mesh3DSpotLightShadow> spotLightShadow_{};
    float ambientScale_ = 0.18F;
    u64 nextFrameIndex_ = 0;
    u64 nextSubmissionIndex_ = 0;
    bool frameOpen_ = false;
    bool stopped_ = false;
};

} // namespace

Core::Result<std::unique_ptr<IRenderDevice>> createNullRenderDevice(const RenderDeviceCreateParams& params)
{
    if (auto status = validateShadowMapExtentConfig(params.shadowMapExtents); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto surfaceStateTracker = Detail::RenderSurfaceStateTracker::create(params.initialPrimaryWindowSurface);
    if (!surfaceStateTracker)
    {
        return Core::failure(std::move(surfaceStateTracker.error()));
    }

    std::unique_ptr<IRenderDevice> renderDevice = std::make_unique<NullRenderDevice>(std::move(*surfaceStateTracker));
    return renderDevice;
}

} // namespace Tina::Render
