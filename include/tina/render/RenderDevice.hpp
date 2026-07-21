#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFrame.hpp>
#include <tina/render/RenderFrameCapture.hpp>

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>

namespace Tina::Render {

struct RenderDeviceCreateParams final {
    std::optional<RenderSurfaceState> initialPrimaryWindowSurface;
};

struct RenderStatistics final {
    u64 submitted = 0;
    u64 presented = 0;
    u64 skippedSuspendedSurfaceFrames = 0;
    u64 liveResources = 0;
};

// Backend-owned GPU texture handle (generation-aware). Not a weak AssetHandle.
struct GpuTextureId final {
    u32 index = (std::numeric_limits<u32>::max)();
    u32 generation = 0;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return index != (std::numeric_limits<u32>::max)() && generation != 0;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return hasValue();
    }
    [[nodiscard]] friend constexpr bool operator==(const GpuTextureId&, const GpuTextureId&) = default;
};

struct Texture2DUploadDesc final {
    u16 width = 0;
    u16 height = 0;
    // Row-major RGBA8 bytes; size must be width*height*4.
    std::span<const std::byte> rgba8Pixels{};
};

enum class RenderFrameSubmissionKind : u8 {
    Submitted,
    SkippedSuspendedSurface,
};

struct RenderFrameSubmission final {
    RenderFrameSubmissionKind kind = RenderFrameSubmissionKind::SkippedSuspendedSurface;
    u64 submissionIndex = 0;

    [[nodiscard]] static constexpr RenderFrameSubmission Submitted(u64 index) noexcept
    {
        return RenderFrameSubmission{RenderFrameSubmissionKind::Submitted, index};
    }

    [[nodiscard]] static constexpr RenderFrameSubmission SkippedSuspendedSurface() noexcept
    {
        return RenderFrameSubmission{};
    }

    [[nodiscard]] constexpr bool requiresPresent() const noexcept
    {
        return kind == RenderFrameSubmissionKind::Submitted;
    }
};

class IRenderDevice {
  public:
    virtual ~IRenderDevice() = default;

    // Every borrowed view carried by frame is valid only for this call. The
    // implementation must synchronously consume it and retain no view, span,
    // or element pointer after returning.
    [[nodiscard]] virtual Core::Result<RenderFrameSubmission> submitFrame(const RenderFrame& frame) = 0;
    [[nodiscard]] virtual Core::Status present() = 0;
    [[nodiscard]] virtual RenderStatistics statistics() const noexcept = 0;
    virtual void shutdown() noexcept = 0;

    // Optional GPU texture path (M10-A23). Default implementations return Unsupported.
    // Null records logical textures; bgfx creates real GPU textures.
    [[nodiscard]] virtual Core::Result<GpuTextureId> createTexture2DRgba8(const Texture2DUploadDesc& desc)
    {
        static_cast<void>(desc);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Texture2D upload");
    }
    [[nodiscard]] virtual Core::Status destroyTexture2D(GpuTextureId texture) noexcept
    {
        static_cast<void>(texture);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Texture2D destroy");
    }
    // Bind a GPU texture for Sprite2D batches with matching spriteKey (0 clears binding).
    [[nodiscard]] virtual Core::Status setSprite2DTextureBinding(u32 spriteKey, GpuTextureId texture) noexcept
    {
        static_cast<void>(spriteKey);
        static_cast<void>(texture);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Sprite2D texture binding");
    }

    // M11-D1: capture primary backbuffer as RGBA8 (top-left origin) after present.
    // Default Unsupported; Null returns Unsupported; bgfx implements via requestScreenShot.
    // Must not be called while a frame is open (between submit and present).
    [[nodiscard]] virtual Core::Result<Rgba8FrameCapture> capturePrimaryFrameRgba8()
    {
        return Core::failure(RenderErrorCode::FrameCaptureUnsupported,
                             "This render device does not support primary frame capture");
    }
};

using RenderDeviceFactory =
    std::move_only_function<Core::Result<std::unique_ptr<IRenderDevice>>(const RenderDeviceCreateParams&)>;

} // namespace Tina::Render
