#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include "../RenderSurfaceStateTracker.hpp"

#include <cmath>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Tina::Render {
namespace {

[[nodiscard]] bool isFiniteNonNegativeRgb(float red, float green, float blue) noexcept
{
    return std::isfinite(red) && std::isfinite(green) && std::isfinite(blue) &&
           red >= 0.0F && green >= 0.0F && blue >= 0.0F;
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
        for (auto it = materialTextureBindings_.begin(); it != materialTextureBindings_.end();)
        {
            if (it->second == texture)
            {
                it = materialTextureBindings_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = materialMetallicRoughnessTextureBindings_.begin();
             it != materialMetallicRoughnessTextureBindings_.end();)
        {
            if (it->second == texture)
            {
                it = materialMetallicRoughnessTextureBindings_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = materialNormalTextureBindings_.begin(); it != materialNormalTextureBindings_.end();)
        {
            if (it->second == texture)
            {
                it = materialNormalTextureBindings_.erase(it);
            }
            else
            {
                ++it;
            }
        }
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
            materialTextureBindings_.erase(materialKey);
            return Core::success();
        }
        if (texture.index >= textures_.size() || !textures_[texture.index].live ||
            textures_[texture.index].generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        materialTextureBindings_[materialKey] = texture;
        return Core::success();
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
        materialFactors_[materialKey] = MaterialFactors{.metallic = metallic, .roughness = roughness};
        return Core::success();
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
            materialNormalTextureBindings_.erase(materialKey);
            return Core::success();
        }
        if (texture.index >= textures_.size() || !textures_[texture.index].live ||
            textures_[texture.index].generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        materialNormalTextureBindings_[materialKey] = texture;
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DDirectionalLight(float dirX, float dirY, float dirZ, float colorR,
                                                         float colorG, float colorB,
                                                         float ambientScale) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!std::isfinite(dirX) || !std::isfinite(dirY) || !std::isfinite(dirZ) ||
            !isFiniteNonNegativeRgb(colorR, colorG, colorB) || !std::isfinite(ambientScale) ||
            ambientScale < 0.0F)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "Mesh3D directional light parameters must be finite (color and ambient >= 0)");
        }
        const float lenSq = dirX * dirX + dirY * dirY + dirZ * dirZ;
        if (lenSq <= 1.0e-12F)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "Mesh3D light direction must be non-zero");
        }
        lightDirX_ = dirX;
        lightDirY_ = dirY;
        lightDirZ_ = dirZ;
        lightColorR_ = colorR;
        lightColorG_ = colorG;
        lightColorB_ = colorB;
        lightAmbient_ = ambientScale;
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DFillDirectionalLight(float dirX, float dirY, float dirZ, float colorR,
                                                             float colorG, float colorB) noexcept override
    {
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The null render device is stopped");
        }
        if (!std::isfinite(dirX) || !std::isfinite(dirY) || !std::isfinite(dirZ) ||
            !isFiniteNonNegativeRgb(colorR, colorG, colorB))
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "Mesh3D fill light parameters must be finite and color must be non-negative");
        }
        const float colorMag = std::abs(colorR) + std::abs(colorG) + std::abs(colorB);
        if (colorMag <= 1.0e-6F)
        {
            fillEnabled_ = false;
            fillDirX_ = 0.0F;
            fillDirY_ = 1.0F;
            fillDirZ_ = 0.0F;
            fillColorR_ = 0.0F;
            fillColorG_ = 0.0F;
            fillColorB_ = 0.0F;
            return Core::success();
        }
        const float lenSq = dirX * dirX + dirY * dirY + dirZ * dirZ;
        if (lenSq <= 1.0e-12F)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "Mesh3D fill light direction must be non-zero when color is non-zero");
        }
        fillEnabled_ = true;
        fillDirX_ = dirX;
        fillDirY_ = dirY;
        fillDirZ_ = dirZ;
        fillColorR_ = colorR;
        fillColorG_ = colorG;
        fillColorB_ = colorB;
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
            materialMetallicRoughnessTextureBindings_.erase(materialKey);
            return Core::success();
        }
        if (texture.index >= textures_.size() || !textures_[texture.index].live ||
            textures_[texture.index].generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        materialMetallicRoughnessTextureBindings_[materialKey] = texture;
        return Core::success();
    }

    void shutdown() noexcept override
    {
        stopped_ = true;
        frameOpen_ = false;
        spriteBindings_.clear();
        textures_.clear();
        meshBindings_.clear();
        materialTextureBindings_.clear();
        materialMetallicRoughnessTextureBindings_.clear();
        materialNormalTextureBindings_.clear();
        materialFactors_.clear();
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
        bool live = false;
    };

    Detail::RenderSurfaceStateTracker surfaceStateTracker_;
    RenderStatistics statistics_{};
    std::vector<TextureSlot> textures_{};
    std::unordered_map<u32, GpuTextureId> spriteBindings_{};
    std::vector<MeshSlot> meshes_{};
    std::unordered_map<u32, GpuMeshId> meshBindings_{};
    std::unordered_map<u32, GpuTextureId> materialTextureBindings_{};
    std::unordered_map<u32, GpuTextureId> materialMetallicRoughnessTextureBindings_{};
    std::unordered_map<u32, GpuTextureId> materialNormalTextureBindings_{};
    struct MaterialFactors final {
        float metallic = 0.0F;
        float roughness = 1.0F;
    };
    std::unordered_map<u32, MaterialFactors> materialFactors_{};
    float lightDirX_ = 0.4F;
    float lightDirY_ = 0.85F;
    float lightDirZ_ = 0.35F;
    float lightColorR_ = 1.0F;
    float lightColorG_ = 0.98F;
    float lightColorB_ = 0.92F;
    float lightAmbient_ = 0.18F;
    bool fillEnabled_ = false;
    float fillDirX_ = 0.0F;
    float fillDirY_ = 1.0F;
    float fillDirZ_ = 0.0F;
    float fillColorR_ = 0.0F;
    float fillColorG_ = 0.0F;
    float fillColorB_ = 0.0F;
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
