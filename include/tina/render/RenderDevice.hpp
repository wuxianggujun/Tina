#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/FramePin.hpp>
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
    // Resources that are logically stale but whose native handles are still
    // waiting for backend-proven GPU completion.
    u64 pendingGpuRetirements = 0;
    u64 completedGpuRetirements = 0;
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

// Backend-owned GPU static mesh handle (generation-aware). Product-3D path (M11-E2).
struct GpuMeshId final {
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
    [[nodiscard]] friend constexpr bool operator==(const GpuMeshId&, const GpuMeshId&) = default;
};

struct Texture2DUploadDesc final {
    u16 width = 0;
    u16 height = 0;
    // Row-major RGBA8 bytes; size must be width*height*4.
    std::span<const std::byte> rgba8Pixels{};
};

// Interleaved P3_N3_UV2 floats (8 per vertex) + U16 triangle indices.
// Matches AssetFormat::StaticMeshVertexLayout::P3N3UV2 / Opaque3D fixture layout.
struct StaticMeshUploadDesc final {
    u32 vertexCount = 0;
    u32 indexCount = 0;
    std::span<const float> vertices{}; // size == vertexCount * 8
    std::span<const u16> indices{};    // size == indexCount, multiple of 3
};

struct Mesh3DDirectionalLight final {
    float directionTowardLightX = 0.0F;
    float directionTowardLightY = 1.0F;
    float directionTowardLightZ = 0.0F;
    float colorR = 1.0F;
    float colorG = 1.0F;
    float colorB = 1.0F;
};

struct Mesh3DLightingDesc final {
    static constexpr std::size_t MaximumDirectionalLightCount = 4;

    // Consumed synchronously by setMesh3DLighting(); the backend retains no span.
    std::span<const Mesh3DDirectionalLight> directionalLights{};
    float ambientScale = 0.18F;
};

[[nodiscard]] Core::Status validateMesh3DLightingDesc(const Mesh3DLightingDesc& lighting) noexcept;

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
    // Logically invalidates texture immediately and releases completionPin only
    // after the backend can safely hand the native resource to its destroy path.
    // On failure completionPin is not consumed. Null/default paths complete
    // synchronously; real backends may retain it until a GPU completion marker.
    [[nodiscard]] virtual Core::Status retireTexture2D(GpuTextureId texture,
                                                       FramePin& completionPin) noexcept
    {
        auto status = destroyTexture2D(texture);
        if (status)
        {
            completionPin.release();
        }
        return status;
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

    // Optional StaticMesh GPU path (M11-E2). Default Unsupported.
    // Null records logical meshes; bgfx creates real VB/IB (P3_N3_UV2 + U16).
    [[nodiscard]] virtual Core::Result<GpuMeshId> createStaticMeshP3N3UV2(const StaticMeshUploadDesc& desc)
    {
        static_cast<void>(desc);
        return Core::failure(RenderErrorCode::MeshUploadUnsupported,
                             "This render device does not support StaticMesh upload");
    }
    [[nodiscard]] virtual Core::Status destroyStaticMesh(GpuMeshId mesh) noexcept
    {
        static_cast<void>(mesh);
        return Core::failure(RenderErrorCode::MeshUploadUnsupported,
                             "This render device does not support StaticMesh destroy");
    }
    // Same ownership contract as retireTexture2D().
    [[nodiscard]] virtual Core::Status retireStaticMesh(GpuMeshId mesh,
                                                        FramePin& completionPin) noexcept
    {
        auto status = destroyStaticMesh(mesh);
        if (status)
        {
            completionPin.release();
        }
        return status;
    }

    // Advances already-submitted retirement completion without fabricating a
    // GPU fence. The default/Null path is already complete. A backend may
    // return GpuRetirementUnsupported when only shutdown can provide a drain.
    [[nodiscard]] virtual Core::Status drainGpuRetirements() noexcept
    {
        return Core::success();
    }
    // Bind a GPU mesh for Mesh3D batches with matching meshKey (0 clears binding).
    // meshKey=1 remains the built-in procedural cube fixture when unbound.
    [[nodiscard]] virtual Core::Status setMesh3DBinding(u32 meshKey, GpuMeshId mesh) noexcept
    {
        static_cast<void>(meshKey);
        static_cast<void>(mesh);
        return Core::failure(RenderErrorCode::MeshUploadUnsupported,
                             "This render device does not support Mesh3D mesh binding");
    }
    // M11-E5: bind baseColor texture for Mesh3D batches with matching materialKey
    // (0 clears). Unbound materialKey samples the default 1x1 white texture.
    [[nodiscard]] virtual Core::Status setMesh3DMaterialTextureBinding(u32 materialKey,
                                                                       GpuTextureId texture) noexcept
    {
        static_cast<void>(materialKey);
        static_cast<void>(texture);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Mesh3D material texture binding");
    }
    // RENDER-001: bind optional glTF metallic-roughness texture (G=roughness, B=metallic)
    // for Mesh3D batches with matching materialKey (invalid texture clears). Unbound uses
    // device defaults (or setMesh3DMaterialFactors) without sampling a map.
    [[nodiscard]] virtual Core::Status setMesh3DMaterialMetallicRoughnessTextureBinding(
        u32 materialKey, GpuTextureId texture) noexcept
    {
        static_cast<void>(materialKey);
        static_cast<void>(texture);
        return Core::failure(
            RenderErrorCode::TextureUploadUnsupported,
            "This render device does not support Mesh3D metallic-roughness texture binding");
    }
    // RENDER-001: per-materialKey metallic/roughness factors from Cooked Material v2.
    // Values must be in [0,1]. materialKey 0 is invalid.
    // Unset keys use device defaults metallic=0, roughness=1 (dielectric matte).
    // Cooked glTF defaults are often metallic=1; callers should setMesh3DMaterialFactors
    // from parseMaterialFromCooked (sample_3d does).
    [[nodiscard]] virtual Core::Status setMesh3DMaterialFactors(u32 materialKey, float metallic,
                                                                float roughness) noexcept
    {
        static_cast<void>(materialKey);
        static_cast<void>(metallic);
        static_cast<void>(roughness);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Mesh3D material factors");
    }
    // RENDER-001: optional tangent-space normal map for Mesh3D materialKey (invalid clears).
    // Unbound uses geometric normals only (no map sample).
    [[nodiscard]] virtual Core::Status setMesh3DMaterialNormalTextureBinding(u32 materialKey,
                                                                             GpuTextureId texture) noexcept
    {
        static_cast<void>(materialKey);
        static_cast<void>(texture);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Mesh3D normal texture binding");
    }
    // RENDER-001: one bounded lighting submission model for experimental Opaque3D MR.
    // Supports 0..MaximumDirectionalLightCount world-space directional lights plus
    // non-negative ambient. Directions are normalized by the backend. Not IBL/shadows.
    [[nodiscard]] virtual Core::Status setMesh3DLighting(const Mesh3DLightingDesc& lighting) noexcept
    {
        static_cast<void>(lighting);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Mesh3D lighting");
    }
};

using RenderDeviceFactory =
    std::move_only_function<Core::Result<std::unique_ptr<IRenderDevice>>(const RenderDeviceCreateParams&)>;

} // namespace Tina::Render
