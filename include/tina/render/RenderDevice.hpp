#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderFrame.hpp>
#include <tina/render/RenderFrameCapture.hpp>
#include <tina/render/ShadowMapExtentConfig.hpp>

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace Tina::Render {

// Which graphics API a device should use. Backend-neutral by name so the public
// surface stays free of bgfx tokens; a backend that cannot honour the request fails
// device creation rather than silently substituting another API, because a game that
// asked for Vulkan and got GL would be told nothing.
//
// `Automatic` defers to the backend's own preference. It is not "no opinion": Tina
// applies its own platform preference first (see preferredRendererApi), because bgfx's
// auto-pick scores OpenGLES highest on Android and never scores Vulkan there at all,
// even though Vulkan has shipped since Android 7.0 and is compiled in
// (bgfx/src/config.h enables BGFX_CONFIG_RENDERER_VULKAN for BX_PLATFORM_ANDROID).
enum class RendererApi : u8 {
    Automatic = 0,
    Vulkan = 1,
    Direct3D11 = 2,
    Direct3D12 = 3,
    Metal = 4,
    OpenGL = 5,
    OpenGLES = 6,
};

[[nodiscard]] constexpr bool isSupportedRendererApi(RendererApi api) noexcept
{
    switch (api)
    {
    case RendererApi::Automatic:
    case RendererApi::Vulkan:
    case RendererApi::Direct3D11:
    case RendererApi::Direct3D12:
    case RendererApi::Metal:
    case RendererApi::OpenGL:
    case RendererApi::OpenGLES:
        return true;
    }
    return false;
}

// Tina's platform preference, applied when a caller leaves the choice Automatic.
//
// Mobile prefers Vulkan: it is the current API on Android and the one with a real
// driver investment behind it, while bgfx's own scoring would pick OpenGLES. The ESSL
// shader variants stay as the fallback for devices whose Vulkan driver is unusable.
//
// Desktop stays Automatic on purpose. Every product pixel fingerprint and visual-gate
// baseline in this repo was frozen under the backend bgfx picks on Windows (D3D11);
// forcing Vulkan here would invalidate all of them, which is a deliberate re-baseline
// rather than a default change. Games can still ask for Vulkan explicitly.
[[nodiscard]] constexpr RendererApi preferredRendererApi() noexcept
{
#if defined(__ANDROID__)
    return RendererApi::Vulkan;
#else
    return RendererApi::Automatic;
#endif
}

struct RenderDeviceCreateParams final {
    static constexpr u32 MinimumDrawCallCapacity = 1024;
    static constexpr u32 MaximumDrawCallCapacity = 65'535;
    static constexpr u32 DefaultDrawCallCapacity = MaximumDrawCallCapacity;

    [[nodiscard]] static constexpr bool isSupportedDrawCallCapacity(
        u32 capacity) noexcept
    {
        return capacity == MaximumDrawCallCapacity ||
               (capacity >= MinimumDrawCallCapacity &&
                capacity % MinimumDrawCallCapacity == 0U);
    }

    std::optional<RenderSurfaceState> initialPrimaryWindowSurface;
    ShadowMapExtentConfig shadowMapExtents{};
    // Requested graphics API. Automatic resolves through preferredRendererApi(); an
    // explicit value the backend cannot create fails device creation.
    RendererApi rendererApi = RendererApi::Automatic;
    // Maximum draw/compute submissions retained per frame. Capacities use
    // bgfx's 1024-entry blocks, except for the exact 65535 native maximum.
    u32 drawCallCapacity = DefaultDrawCallCapacity;
    // Backbuffer multisampling: 0 disables MSAA; 2/4/8/16 request that sample
    // count. Anything else fails device creation. Pixel-evidence gates keep 0.
    u8 msaaSamples = 0;
    // Initial vertical-sync state. Unlike the capacity and MSAA knobs above,
    // this one is not device-lifetime: see IRenderDevice::setVsyncEnabled.
    bool vsync = true;
};

struct RenderStatistics final {
    u64 submitted = 0;
    u64 presented = 0;
    u64 skippedSuspendedSurfaceFrames = 0;
    // Times the backbuffer was rebuilt because the surface's native window was
    // replaced (ADR 0034). Zero on desktop, where the native window is stable for the
    // device lifetime; a gate can assert that.
    u64 nativeSurfaceRebinds = 0;
    u64 liveResources = 0;
    // Resources that are logically stale but whose native handles are still
    // waiting for backend-proven GPU completion.
    u64 pendingGpuRetirements = 0;
    u64 completedGpuRetirements = 0;
};

// Backend-owned GPU texture handle. Owner rejects cross-device use, generation
// rejects stale slot reuse. Not a weak AssetHandle.
struct GpuTextureId final {
    inline static constexpr u32 InvalidIndex = (std::numeric_limits<u32>::max)();
    inline static constexpr u32 UnscopedOwner = (std::numeric_limits<u32>::max)();

    u32 owner = 0;
    u32 index = InvalidIndex;
    u32 generation = 0;

    constexpr GpuTextureId() noexcept = default;
    // Reserved unscoped namespace for deterministic backend test doubles. Real
    // devices issue the three-part owner/index/generation form and reject this owner.
    constexpr GpuTextureId(u32 indexValue, u32 generationValue) noexcept
        : owner(UnscopedOwner), index(indexValue), generation(generationValue)
    {
    }
    constexpr GpuTextureId(u32 ownerValue, u32 indexValue, u32 generationValue) noexcept
        : owner(ownerValue), index(indexValue), generation(generationValue)
    {
    }

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return owner != 0 && index != InvalidIndex && generation != 0;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return hasValue();
    }
    [[nodiscard]] friend constexpr bool operator==(const GpuTextureId&, const GpuTextureId&) = default;
};

// One backend-owned IBL resource: diffuse irradiance cubemap, prefiltered
// specular cubemap, and BRDF integration LUT. The three native textures share
// one generation identity and retire atomically.
struct GpuEnvironmentMapId final {
    inline static constexpr u32 InvalidIndex = (std::numeric_limits<u32>::max)();
    inline static constexpr u32 UnscopedOwner = (std::numeric_limits<u32>::max)();

    u32 owner = 0;
    u32 index = InvalidIndex;
    u32 generation = 0;

    constexpr GpuEnvironmentMapId() noexcept = default;
    constexpr GpuEnvironmentMapId(u32 indexValue, u32 generationValue) noexcept
        : owner(UnscopedOwner), index(indexValue), generation(generationValue)
    {
    }
    constexpr GpuEnvironmentMapId(u32 ownerValue, u32 indexValue, u32 generationValue) noexcept
        : owner(ownerValue), index(indexValue), generation(generationValue)
    {
    }

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return owner != 0 && index != InvalidIndex && generation != 0;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return hasValue(); }
    [[nodiscard]] friend constexpr bool operator==(const GpuEnvironmentMapId&,
                                                   const GpuEnvironmentMapId&) = default;
};

// Backend-owned GPU mesh handle. Owner rejects cross-device use,
// generation rejects stale slot reuse. Product-3D path (M11-E2).
struct GpuMeshId final {
    inline static constexpr u32 InvalidIndex = (std::numeric_limits<u32>::max)();
    inline static constexpr u32 UnscopedOwner = (std::numeric_limits<u32>::max)();

    u32 owner = 0;
    u32 index = InvalidIndex;
    u32 generation = 0;

    constexpr GpuMeshId() noexcept = default;
    // Reserved unscoped namespace for deterministic backend test doubles. Real
    // devices issue the three-part owner/index/generation form and reject this owner.
    constexpr GpuMeshId(u32 indexValue, u32 generationValue) noexcept
        : owner(UnscopedOwner), index(indexValue), generation(generationValue)
    {
    }
    constexpr GpuMeshId(u32 ownerValue, u32 indexValue, u32 generationValue) noexcept
        : owner(ownerValue), index(indexValue), generation(generationValue)
    {
    }

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return owner != 0 && index != InvalidIndex && generation != 0;
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

struct EnvironmentMapUploadDesc final {
    u16 diffuseFaceSize = 0;
    u16 specularFaceSize = 0;
    u16 specularMipCount = 0;
    u16 brdfWidth = 0;
    u16 brdfHeight = 0;
    // RGBA16F cube faces use +X, -X, +Y, -Y, +Z, -Z order. Diffuse has one
    // level. Specular is mip-major and must contain the complete chain to 1x1.
    std::span<const std::byte> diffuseRgba16FloatPixels{};
    std::span<const std::byte> specularRgba16FloatPixels{};
    // Row-major RG16F split-sum BRDF integration LUT.
    std::span<const std::byte> brdfRg16FloatPixels{};
};

[[nodiscard]] Core::Status
validateEnvironmentMapUploadDesc(const EnvironmentMapUploadDesc& desc) noexcept;

// Interleaved P3_N3_T4_UV2 floats (12 per vertex) + U16 triangle indices.
// tangent.xyz is non-zero and tangent.w is exactly -1 or +1.
struct StaticMeshUploadDesc final {
    u32 vertexCount = 0;
    u32 indexCount = 0;
    std::span<const float> vertices{}; // size == vertexCount * 12
    std::span<const u16> indices{};    // size == indexCount, multiple of 3
};

// SkinnedMesh v1 GPU upload: the StaticMesh P3N3T4UV2 stream
// plus a parallel skin stream of four joint influences per vertex. Weights are
// the cooked fixed-point values and must sum to exactly 0xFFFF per vertex;
// joint indices must stay below jointCount (at most 256). Backends convert the
// skin stream to their native attribute formats during upload.
struct SkinnedMeshUploadDesc final {
    u32 vertexCount = 0;
    u32 indexCount = 0;
    u16 jointCount = 0;
    std::span<const float> vertices{};   // size == vertexCount * 12
    std::span<const u16> jointIndices{}; // size == vertexCount * 4, each < jointCount
    std::span<const u16> jointWeights{}; // size == vertexCount * 4, per-vertex sum == 0xFFFF
    std::span<const u16> indices{};      // size == indexCount, multiple of 3
};

struct Mesh3DMaterialBindingDesc final {
    GpuTextureId baseColorTexture{};
    GpuTextureId metallicRoughnessTexture{};
    GpuTextureId normalTexture{};
    float metallicFactor = 0.0F;
    float roughnessFactor = 1.0F;
    Mesh3DAlphaMode alphaMode = Mesh3DAlphaMode::Opaque;

    [[nodiscard]] friend constexpr bool operator==(const Mesh3DMaterialBindingDesc&,
                                                   const Mesh3DMaterialBindingDesc&) = default;
};

struct Mesh3DImageBasedLightingDesc final {
    GpuEnvironmentMapId environmentMap{};
    float intensity = 1.0F;
    float rotationRadians = 0.0F;

    [[nodiscard]] friend constexpr bool operator==(const Mesh3DImageBasedLightingDesc&,
                                                   const Mesh3DImageBasedLightingDesc&) = default;
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

    IRenderDevice(const IRenderDevice&) = delete;
    IRenderDevice& operator=(const IRenderDevice&) = delete;
    IRenderDevice(IRenderDevice&&) = delete;
    IRenderDevice& operator=(IRenderDevice&&) = delete;

    // Every borrowed view carried by frame is valid only for this call. The
    // implementation must synchronously consume it and retain no view, span,
    // or element pointer after returning.
    [[nodiscard]] virtual Core::Result<RenderFrameSubmission> submitFrame(const RenderFrame& frame) = 0;
    [[nodiscard]] virtual Core::Status present() = 0;
    [[nodiscard]] virtual RenderStatistics statistics() const noexcept = 0;
    virtual void shutdown() noexcept = 0;

    // Vertical sync is the one device setting that may change after creation,
    // because a player-facing display option must not require a restart. The
    // backend applies it no later than the next present(); it is not required to
    // take effect within the frame that requested it. Devices that cannot
    // present (null/headless) accept and record the request. Callers must not
    // assume the value survives a device recreate: it is re-seeded from
    // RenderDeviceCreateParams::vsync.
    virtual void setVsyncEnabled(bool enabled) noexcept
    {
        static_cast<void>(enabled);
    }
    [[nodiscard]] virtual bool vsyncEnabled() const noexcept
    {
        return true;
    }

    // Optional GPU texture path (M10-A23). Default implementations return Unsupported.
    // Null records logical textures; bgfx creates real GPU textures.
    [[nodiscard]] virtual Core::Result<GpuTextureId> createTexture2DRgba8(const Texture2DUploadDesc& desc)
    {
        static_cast<void>(desc);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Texture2D upload");
    }
    // Non-consuming ownership check. Success proves texture is currently live on
    // this device; failure leaves the candidate and backend state unchanged.
    [[nodiscard]] virtual Core::Status validateTexture2D(GpuTextureId texture) const noexcept
    {
        static_cast<void>(texture);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Texture2D validation");
    }
    [[nodiscard]] virtual Core::Status destroyTexture2D(GpuTextureId texture) noexcept
    {
        static_cast<void>(texture);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Texture2D destroy");
    }
    // On success, logically invalidates texture and clears every device binding
    // that references it before returning. completionPin is released only after
    // the backend can safely hand the native resource to its destroy path. On
    // failure, texture, bindings, and completionPin are unchanged. Null/default
    // paths complete synchronously; real backends may retain the pin until a GPU
    // completion marker.
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
    [[nodiscard]] virtual Core::Result<GpuEnvironmentMapId>
    createEnvironmentMap(const EnvironmentMapUploadDesc& desc)
    {
        static_cast<void>(desc);
        return Core::failure(RenderErrorCode::EnvironmentMapUploadUnsupported,
                             "This render device does not support EnvironmentMap upload");
    }
    [[nodiscard]] virtual Core::Status
    validateEnvironmentMap(GpuEnvironmentMapId environmentMap) const noexcept
    {
        static_cast<void>(environmentMap);
        return Core::failure(RenderErrorCode::EnvironmentMapUploadUnsupported,
                             "This render device does not support EnvironmentMap validation");
    }
    [[nodiscard]] virtual Core::Status
    destroyEnvironmentMap(GpuEnvironmentMapId environmentMap) noexcept
    {
        static_cast<void>(environmentMap);
        return Core::failure(RenderErrorCode::EnvironmentMapUploadUnsupported,
                             "This render device does not support EnvironmentMap destroy");
    }
    [[nodiscard]] virtual Core::Status retireEnvironmentMap(
        GpuEnvironmentMapId environmentMap, FramePin& completionPin) noexcept
    {
        auto status = destroyEnvironmentMap(environmentMap);
        if (status)
        {
            completionPin.release();
        }
        return status;
    }
    // Bind a GPU texture for Texture2D frame-resource descriptors with a
    // matching non-zero deviceBindingKey.
    // An invalid GpuTextureId clears the binding.
    [[nodiscard]] virtual Core::Status setTexture2DBinding(u32 deviceBindingKey,
                                                           GpuTextureId texture) noexcept
    {
        static_cast<void>(deviceBindingKey);
        static_cast<void>(texture);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Texture2D binding");
    }
    // Transactionally allocates a non-zero key from this device's Texture2D key
    // namespace and binds texture. Backend failure does not consume the key.
    // Caller-selected keys passed directly to setTexture2DBinding share the
    // namespace and must not be mixed with allocator-managed bindings.
    [[nodiscard]] Core::Result<u32> createTexture2DBinding(GpuTextureId texture) noexcept
    {
        if (!texture)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "Texture2D binding requires a live GPU texture");
        }
        if (m_nextTexture2DBindingKey == 0)
        {
            return Core::failure(RenderErrorCode::TextureBindingKeyExhausted,
                                 "Render device exhausted non-zero Texture2D binding keys");
        }

        const u32 candidateKey = m_nextTexture2DBindingKey;
        if (auto status = setTexture2DBinding(candidateKey, texture); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        m_nextTexture2DBindingKey =
            candidateKey == (std::numeric_limits<u32>::max)() ? 0U : candidateKey + 1U;
        return candidateKey;
    }

    // M11-D1: capture primary backbuffer as RGBA8 (top-left origin) after present.
    // Default Unsupported; Null returns Unsupported; bgfx implements via requestScreenShot.
    // Must not be called while a frame is open (between submit and present).
    [[nodiscard]] virtual Core::Result<Rgba8FrameCapture> capturePrimaryFrameRgba8()
    {
        return Core::failure(RenderErrorCode::FrameCaptureUnsupported,
                             "This render device does not support primary frame capture");
    }

    // Optional StaticMesh GPU path (M11-E2 / RENDER-001-VERTEX-TANGENTS).
    // Null records logical meshes; bgfx creates a P3N3T4UV2 VB and U16 IB.
    [[nodiscard]] virtual Core::Result<GpuMeshId> createStaticMesh(const StaticMeshUploadDesc& desc)
    {
        static_cast<void>(desc);
        return Core::failure(RenderErrorCode::MeshUploadUnsupported,
                             "This render device does not support StaticMesh upload");
    }
    // Optional SkinnedMesh GPU path. Returned ids share the
    // GPU mesh slot/binding-key namespace and retire through
    // destroyGpuMesh()/retireGpuMesh(), but the slot remembers its skin
    // stream and joint count: only skinned draw items may consume it, and
    // static batches referencing a skinned binding fail closed.
    [[nodiscard]] virtual Core::Result<GpuMeshId> createSkinnedMesh(const SkinnedMeshUploadDesc& desc)
    {
        static_cast<void>(desc);
        return Core::failure(RenderErrorCode::MeshUploadUnsupported,
                             "This render device does not support SkinnedMesh upload");
    }
    [[nodiscard]] virtual Core::Status destroyGpuMesh(GpuMeshId mesh) noexcept
    {
        static_cast<void>(mesh);
        return Core::failure(RenderErrorCode::MeshUploadUnsupported,
                             "This render device does not support GPU mesh destroy");
    }
    // Same ownership contract as retireTexture2D().
    [[nodiscard]] virtual Core::Status retireGpuMesh(GpuMeshId mesh,
                                                     FramePin& completionPin) noexcept
    {
        auto status = destroyGpuMesh(mesh);
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
    // Bind a GPU mesh for Mesh3D descriptors with a matching device binding key
    // (0 clears binding). Key 1 remains the built-in procedural cube fixture when unbound.
    [[nodiscard]] virtual Core::Status setMesh3DBinding(u32 meshKey, GpuMeshId mesh) noexcept
    {
        static_cast<void>(meshKey);
        static_cast<void>(mesh);
        return Core::failure(RenderErrorCode::MeshUploadUnsupported,
                             "This render device does not support Mesh3D mesh binding");
    }
    // Transactionally allocates a key from this device's Mesh3D mesh namespace.
    // Key 1 remains reserved for the built-in procedural cube fixture. Failed
    // bindings do not consume a key; successful keys are never reused.
    // Caller-selected keys passed to setMesh3DBinding share this namespace and
    // must not be mixed with allocator-managed bindings.
    [[nodiscard]] Core::Result<u32> createMesh3DBinding(GpuMeshId mesh) noexcept
    {
        if (!mesh)
        {
            return Core::failure(RenderErrorCode::InvalidMeshUpload,
                                 "Mesh3D binding requires a live GPU mesh");
        }
        if (m_nextMesh3DBindingKey == 0)
        {
            return Core::failure(RenderErrorCode::Mesh3DBindingKeyExhausted,
                                 "Render device exhausted Mesh3D mesh binding keys");
        }

        const u32 candidateKey = m_nextMesh3DBindingKey;
        if (auto status = setMesh3DBinding(candidateKey, mesh); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        m_nextMesh3DBindingKey =
            candidateKey == (std::numeric_limits<u32>::max)() ? 0U : candidateKey + 1U;
        return candidateKey;
    }
    // Atomically replaces all texture and scalar state for one material key.
    // Invalid texture handles clear their corresponding optional binding.
    [[nodiscard]] virtual Core::Status setMesh3DMaterialBinding(
        u32 materialKey, const Mesh3DMaterialBindingDesc& desc) noexcept
    {
        static_cast<void>(materialKey);
        static_cast<void>(desc);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Mesh3D material binding");
    }
    // Erases every texture and scalar state for materialKey. Supporting backends
    // treat an already-clear non-zero key as success.
    [[nodiscard]] virtual Core::Status clearMesh3DMaterialBinding(u32 materialKey) noexcept
    {
        static_cast<void>(materialKey);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support clearing Mesh3D material bindings");
    }
    // Transactionally allocates a key from this device's independent Mesh3D
    // material namespace. Failed bindings do not consume a key; successful keys
    // are never reused.
    // Caller-selected keys passed to the material setters share this namespace
    // and must not be mixed with allocator-managed bindings.
    [[nodiscard]] Core::Result<u32> createMesh3DMaterialBinding(
        const Mesh3DMaterialBindingDesc& desc) noexcept
    {
        if (m_nextMesh3DMaterialBindingKey == 0)
        {
            return Core::failure(RenderErrorCode::Mesh3DMaterialBindingKeyExhausted,
                                 "Render device exhausted Mesh3D material binding keys");
        }

        const u32 candidateKey = m_nextMesh3DMaterialBindingKey;
        if (auto status = setMesh3DMaterialBinding(candidateKey, desc); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        m_nextMesh3DMaterialBindingKey =
            candidateKey == (std::numeric_limits<u32>::max)() ? 0U : candidateKey + 1U;
        return candidateKey;
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
    // RENDER-001: one bounded direct-light submission model for Opaque3D
    // Cook-Torrance GGX.
    // Supports 0..MaximumDirectionalLightCount world-space directional lights plus
    // non-negative ambient. Directions are normalized by the backend. Point/spot
    // positions and influence radii are world-space; spot cones use ordered cosines.
    // IBL is configured independently through setMesh3DImageBasedLighting().
    // Directional shadow authoring belongs to the frame snapshot and pass scheduler.
    [[nodiscard]] virtual Core::Status setMesh3DLighting(const Mesh3DLightingDesc& lighting) noexcept
    {
        static_cast<void>(lighting);
        return Core::failure(RenderErrorCode::TextureUploadUnsupported,
                             "This render device does not support Mesh3D lighting");
    }
    [[nodiscard]] virtual Core::Status setMesh3DImageBasedLighting(
        const Mesh3DImageBasedLightingDesc& lighting) noexcept
    {
        static_cast<void>(lighting);
        return Core::failure(RenderErrorCode::EnvironmentMapUploadUnsupported,
                             "This render device does not support Mesh3D image-based lighting");
    }
    [[nodiscard]] virtual Core::Status clearMesh3DImageBasedLighting() noexcept
    {
        return Core::failure(RenderErrorCode::EnvironmentMapUploadUnsupported,
                             "This render device does not support clearing Mesh3D image-based lighting");
    }

  protected:
    IRenderDevice() noexcept = default;

    [[nodiscard]] u32 resourceOwnerId() const noexcept { return m_resourceOwnerId; }

  private:
    [[nodiscard]] static u32 allocateResourceOwnerId() noexcept
    {
        static std::atomic<u64> nextOwner{1};
        constexpr u64 MaximumOwner = static_cast<u64>((std::numeric_limits<u32>::max)()) - 1U;

        u64 candidate = nextOwner.load(std::memory_order_relaxed);
        while (candidate <= MaximumOwner)
        {
            if (nextOwner.compare_exchange_weak(candidate, candidate + 1,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed))
            {
                return static_cast<u32>(candidate);
            }
        }
        std::terminate();
    }

    u32 m_resourceOwnerId = allocateResourceOwnerId();
    u32 m_nextTexture2DBindingKey = 1;
    u32 m_nextMesh3DBindingKey = 2;
    // Key 1 is reserved for the built-in opaque 3D fixture material.
    u32 m_nextMesh3DMaterialBindingKey = 2;
};

using RenderDeviceFactory =
    std::move_only_function<Core::Result<std::unique_ptr<IRenderDevice>>(const RenderDeviceCreateParams&)>;

} // namespace Tina::Render
