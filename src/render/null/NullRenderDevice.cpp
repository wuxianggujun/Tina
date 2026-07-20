#include <tina/render/RenderErrors.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include "../RenderSurfaceStateTracker.hpp"

#include <memory>
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

    void shutdown() noexcept override
    {
        stopped_ = true;
        frameOpen_ = false;
        spriteBindings_.clear();
        textures_.clear();
        statistics_.liveResources = 0;
    }

  private:
    struct TextureSlot final {
        u32 generation = 1;
        u16 width = 0;
        u16 height = 0;
        bool live = false;
    };

    Detail::RenderSurfaceStateTracker surfaceStateTracker_;
    RenderStatistics statistics_{};
    std::vector<TextureSlot> textures_{};
    std::unordered_map<u32, GpuTextureId> spriteBindings_{};
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
