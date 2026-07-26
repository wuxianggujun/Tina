#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include "../RenderSurfaceStateTracker.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Tina::Render {
namespace {

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
        return GpuTextureId{.index = index, .generation = 1};
    }

    [[nodiscard]] Core::Status destroyTexture2D(GpuTextureId texture) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!texture || texture.index >= textures_.size() || !textures_[texture.index].live ||
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
        for (auto it = spriteBindings_.begin(); it != spriteBindings_.end();)
        {
            if (it->second == texture)
            {
                it = spriteBindings_.erase(it);
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

    [[nodiscard]] Core::Status setSprite2DTextureBinding(u32 spriteKey, GpuTextureId texture) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (spriteKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "spriteKey must be non-zero");
        }
        if (!texture)
        {
            spriteBindings_.erase(spriteKey);
            return Core::success();
        }
        if (texture.index >= textures_.size() || !textures_[texture.index].live ||
            textures_[texture.index].generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        spriteBindings_[spriteKey] = texture;
        return Core::success();
    }

    [[nodiscard]] Core::Result<GpuMeshId> createStaticMeshP3N3UV2(const StaticMeshUploadDesc& desc) override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (desc.vertexCount == 0 || desc.indexCount == 0 || (desc.indexCount % 3U) != 0U ||
            desc.vertices.size() != static_cast<std::size_t>(desc.vertexCount) * 8U ||
            desc.indices.size() != desc.indexCount)
        {
            return Core::failure(RenderErrorCode::InvalidMeshUpload, "invalid StaticMesh upload descriptor");
        }
        for (const u16 index : desc.indices)
        {
            if (static_cast<u32>(index) >= desc.vertexCount)
            {
                return Core::failure(RenderErrorCode::InvalidMeshUpload, "StaticMesh index out of range");
            }
        }
        const u32 slotIndex = static_cast<u32>(meshes_.size());
        meshes_.push_back(MeshSlot{.generation = 1,
                                   .vertexCount = desc.vertexCount,
                                   .indexCount = desc.indexCount,
                                   .live = true});
        ++statistics_.liveResources;
        return GpuMeshId{.index = slotIndex, .generation = 1};
    }

    [[nodiscard]] Core::Status destroyStaticMesh(GpuMeshId mesh) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!mesh || mesh.index >= meshes_.size() || !meshes_[mesh.index].live ||
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
        if (mesh.index >= meshes_.size() || !meshes_[mesh.index].live ||
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
            !std::isfinite(desc.metallicFactor) || !std::isfinite(desc.roughnessFactor))
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "metallic and roughness must be finite values in [0,1]");
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
        ambientScale_ = lighting.ambientScale;
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
        spriteBindings_.clear();
        textures_.clear();
        meshBindings_.clear();
        materialBindings_.clear();
        meshes_.clear();
        statistics_.liveResources = 0;
    }

  private:
    [[nodiscard]] bool isLiveTexture(GpuTextureId texture) const noexcept
    {
        return !texture || (texture.index < textures_.size() && textures_[texture.index].live &&
                            textures_[texture.index].generation == texture.generation);
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
        bool live = false;
    };

    Detail::RenderSurfaceStateTracker surfaceStateTracker_;
    RenderStatistics statistics_{};
    std::vector<TextureSlot> textures_{};
    std::unordered_map<u32, GpuTextureId> spriteBindings_{};
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
    float ambientScale_ = 0.18F;
    u64 nextFrameIndex_ = 0;
    u64 nextSubmissionIndex_ = 0;
    bool frameOpen_ = false;
    bool stopped_ = false;
};

} // namespace

Core::Result<std::unique_ptr<IRenderDevice>> createNullRenderDevice(const RenderDeviceCreateParams& params)
{
    auto surfaceStateTracker = Detail::RenderSurfaceStateTracker::create(params.initialPrimaryWindowSurface);
    if (!surfaceStateTracker)
    {
        return Core::failure(std::move(surfaceStateTracker.error()));
    }

    std::unique_ptr<IRenderDevice> renderDevice = std::make_unique<NullRenderDevice>(std::move(*surfaceStateTracker));
    return renderDevice;
}

} // namespace Tina::Render
