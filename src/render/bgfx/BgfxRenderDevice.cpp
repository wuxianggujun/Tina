#include "BgfxRenderDevice.hpp"
#include "BgfxCascadedDirectionalShadowMath.hpp"
#include "BgfxClearColor.hpp"
#include "BgfxCustomShader.hpp"
#include "BgfxCascadedDirectionalShadowResources.hpp"
#include "BgfxSpotLightShadowMath.hpp"
#include "BgfxSpotLightShadowResources.hpp"
#include "BgfxPointLightShadowMath.hpp"
#include "BgfxPointLightShadowResources.hpp"
#include "BgfxEnvironmentMapResources.hpp"
#include "BgfxOpaque3DGeometry.hpp"
#include "BgfxOpaque3DShader.hpp"
#include "BgfxResourceSlotGeneration.hpp"
#include "BgfxRetirementTimeline.hpp"
#include "BgfxSprite2DGeometry.hpp"
#include "BgfxSprite2DShader.hpp"
#include "BgfxSurfaceFramePlanner.hpp"
#include "BgfxTexture2DUpload.hpp"
#include "BgfxTransientFrameBudget.hpp"
#include "BgfxUIDisplayGeometry.hpp"
#include "BgfxUIAtlasTexture.hpp"
#include "BgfxUIImageShader.hpp"
#include "BgfxUITexturedShader.hpp"
#include "BgfxVideoDecode.hpp"

#include "../../integration/WindowSurfaceLeaseAccess.hpp"
#include "../RenderSurfaceStateTracker.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/render/RenderErrors.hpp>
#include <tina/render/RenderPassScheduler.hpp>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Tina::Render::Bgfx {
namespace {

inline constexpr int PrimaryFrameCaptureDeliveryFrameBudget = 120;
inline constexpr auto PrimaryFrameCapturePollDelay = std::chrono::milliseconds{1};

// M11-D1: bgfx CallbackI for requestScreenShot. screenShot may run on render thread.
class BgfxCaptureCallback final : public bgfx::CallbackI {
  public:
    void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) override
    {
        // Log then hard-exit. Prefer file over stderr: redirected pipes + render thread can lose
        // the line; std::abort() on MSVC Debug pops a modal dialog and freezes automation.
        // Do not write from traceVargs: multi-threaded bgfx + redirected stderr deadlocks.
        char path[512]{};
        const char* temp = std::getenv("TEMP");
        if (temp == nullptr || temp[0] == '\0')
        {
            temp = std::getenv("TMP");
        }
        if (temp == nullptr || temp[0] == '\0')
        {
            temp = ".";
        }
        std::snprintf(path, sizeof(path), "%s\\tina_bgfx_fatal.txt", temp);
        if (std::FILE* file = std::fopen(path, "wb"))
        {
            std::fprintf(file, "bgfx FATAL code=%d %s:%u %s\n", static_cast<int>(code),
                         filePath != nullptr ? filePath : "?", static_cast<unsigned>(line),
                         str != nullptr ? str : "");
            std::fflush(file);
            std::fclose(file);
        }
        std::fprintf(stderr, "bgfx FATAL code=%d %s:%u %s\n", static_cast<int>(code),
                     filePath != nullptr ? filePath : "?", static_cast<unsigned>(line),
                     str != nullptr ? str : "");
        std::fflush(stderr);
        std::_Exit(3);
    }

    // bgfx reports every recoverable backend failure through BX_TRACE and then degrades
    // silently, so a whole subsystem can decline to work while every API call still
    // returns success. Dropping these by default is deliberate (writing to a redirected
    // stderr from bgfx's render thread deadlocks), so the sink is a file and it is opt-in
    // via TINA_BGFX_TRACE_FILE. Nothing changes for a run that does not set it.
    void traceVargs(const char* /*filePath*/, uint16_t /*line*/, const char* format,
                    va_list argList) override
    {
        const char* path = std::getenv("TINA_BGFX_TRACE_FILE");
        if (path == nullptr || path[0] == '\0')
        {
            return;
        }
        char line[2048]{};
        const int written = std::vsnprintf(line, sizeof(line), format, argList);
        if (written <= 0)
        {
            return;
        }
        // bgfx traces from both the api and render threads; the lock keeps one trace per
        // line instead of two interleaved halves.
        const std::lock_guard<std::mutex> guard{traceMutex_};
        std::FILE* file = std::fopen(path, "ab");
        if (file == nullptr)
        {
            return;
        }
        std::fwrite(line, 1, static_cast<std::size_t>(written), file);
        std::fclose(file);
    }

    std::mutex traceMutex_;

    void profilerBegin(const char* /*name*/, uint32_t /*abgr*/, const char* /*filePath*/,
                       uint16_t /*line*/) override
    {
    }

    void profilerBeginLiteral(const char* /*name*/, uint32_t /*abgr*/, const char* /*filePath*/,
                              uint16_t /*line*/) override
    {
    }

    void profilerEnd() override {}

    uint32_t cacheReadSize(uint64_t /*id*/) override { return 0; }

    bool cacheRead(uint64_t /*id*/, void* /*data*/, uint32_t /*size*/) override { return false; }

    void cacheWrite(uint64_t /*id*/, const void* /*data*/, uint32_t /*size*/) override {}

    void screenShot(const char* /*filePath*/, uint32_t width, uint32_t height, uint32_t pitch,
                    bgfx::TextureFormat::Enum /*format*/, const void* data, uint32_t size,
                    bool yflip) override
    {
        std::lock_guard lock(mutex_);
        if (!pending_ || data == nullptr || width == 0 || height == 0 || pitch < width * 4U ||
            size < pitch * height)
        {
            ready_ = true;
            return;
        }

        capture_.width = width;
        capture_.height = height;
        capture_.rgba8Pixels.assign(static_cast<std::size_t>(width) * height * 4U, std::byte{0});
        const auto* srcBase = static_cast<const std::uint8_t*>(data);
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint32_t srcY = yflip ? (height - 1U - y) : y;
            const auto* srcRow = srcBase + static_cast<std::size_t>(srcY) * pitch;
            auto* dstRow = reinterpret_cast<std::uint8_t*>(
                capture_.rgba8Pixels.data() + static_cast<std::size_t>(y) * width * 4U);
            for (uint32_t x = 0; x < width; ++x)
            {
                // bgfx screenshot is BGRA; convert to RGBA8 top-left.
                const auto* px = srcRow + static_cast<std::size_t>(x) * 4U;
                dstRow[x * 4U + 0U] = px[2];
                dstRow[x * 4U + 1U] = px[1];
                dstRow[x * 4U + 2U] = px[0];
                dstRow[x * 4U + 3U] = px[3];
            }
        }
        ok_ = true;
        ready_ = true;
    }

    void captureBegin(uint32_t /*width*/, uint32_t /*height*/, uint32_t /*pitch*/,
                      bgfx::TextureFormat::Enum /*format*/, bool /*yflip*/) override
    {
    }

    void captureEnd() override {}

    void captureFrame(const void* /*data*/, uint32_t /*size*/) override {}

    void beginCapture()
    {
        std::lock_guard lock(mutex_);
        pending_ = true;
        ready_ = false;
        ok_ = false;
        capture_ = {};
    }

    [[nodiscard]] bool isReady() const
    {
        std::lock_guard lock(mutex_);
        return ready_;
    }

    [[nodiscard]] Core::Result<Rgba8FrameCapture> take()
    {
        std::lock_guard lock(mutex_);
        pending_ = false;
        if (!ready_ || !ok_ || capture_.empty())
        {
            return Core::failure(RenderErrorCode::FrameCaptureFailed,
                                 "bgfx screenshot callback did not produce a valid RGBA8 frame");
        }
        Rgba8FrameCapture out = std::move(capture_);
        capture_ = {};
        ready_ = false;
        ok_ = false;
        return out;
    }

  private:
    mutable std::mutex mutex_{};
    bool pending_ = false;
    bool ready_ = false;
    bool ok_ = false;
    Rgba8FrameCapture capture_{};
};

constexpr bgfx::ViewId kSurfaceClearView = 0;
constexpr std::array<bgfx::ViewId, BgfxCascadedDirectionalShadowCascadeCount>
    kCascadedDirectionalShadowViews{1, 2, 3, 4};
static_assert(BgfxCascadedDirectionalShadowCascadeCount ==
              Mesh3DCascadedDirectionalShadow::CascadeCount);
constexpr bgfx::ViewId kSpotLightShadowView = 5;
constexpr std::array<bgfx::ViewId, BgfxPointLightShadowFaceCount>
    kPointLightShadowViews{6, 7, 8, 9, 10, 11};
constexpr std::array<const char*, BgfxPointLightShadowFaceCount>
    kPointLightShadowSamplerNames{
        "s_pointShadowPosX", "s_pointShadowNegX", "s_pointShadowPosY",
        "s_pointShadowNegY", "s_pointShadowPosZ", "s_pointShadowNegZ"};

template <typename Handle, usize Count>
[[nodiscard]] constexpr std::array<Handle, Count> invalidBgfxHandleArray() noexcept
{
    std::array<Handle, Count> handles{};
    for (Handle& handle : handles)
    {
        handle = BGFX_INVALID_HANDLE;
    }
    return handles;
}
constexpr bgfx::ViewId kOpaque3DView = 12;
constexpr bgfx::ViewId kTransparent3DView = 13;
constexpr bgfx::ViewId kSprite2DView = 14;
constexpr bgfx::ViewId kUIView = 15;
// Must remain after every view that can reference a retireable resource.
constexpr bgfx::ViewId kRetirementMarkerView = 16;
// Reset flags that are fixed for the device lifetime. Vsync and MSAA are OR'd
// in separately: MSAA at creation, vsync whenever setVsyncEnabled changes it.
constexpr u32 kDefaultResetFlags = BGFX_RESET_MAXANISOTROPY;
// ADR 0042 clear-colour encoding lives in BgfxClearColor.hpp so it stays testable.
constexpr usize kIndicesPerSolidQuad = 6;
#if defined(BGFX_CONFIG_MAX_DRAW_CALLS)
constexpr u32 kCompiledBgfxMaximumDrawCalls = BGFX_CONFIG_MAX_DRAW_CALLS;
#else
constexpr u32 kCompiledBgfxMaximumDrawCalls =
    RenderDeviceCreateParams::MaximumDrawCallCapacity;
#endif
constexpr u64 kShadowSamplerFallbackFlags =
    BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL |
    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
// bgfx shader binary v11 reflects uniform array length as u8. The final joint
// uses a separate mat4 uniform so the public 256-joint contract stays intact.
constexpr u16 kOpaque3DSkinPaletteArrayJointCount = 255;
static_assert(MaxSkinnedMesh3DPaletteJointCount ==
              static_cast<u32>(kOpaque3DSkinPaletteArrayJointCount) + 1U);
constexpr u64 kOpaque3DState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
constexpr u64 kTransparent3DState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS |
    BGFX_STATE_BLEND_ALPHA;
constexpr u64 kOpaque3DShadowDepthState =
    BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
constexpr u64 kSprite2DPremultipliedAlphaState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
constexpr u64 kUIPremultipliedAlphaState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

static_assert(std::is_standard_layout_v<BgfxUIDisplayVertex>);
static_assert(sizeof(BgfxUIDisplayVertex) == sizeof(float) * 11U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, x) == 0U);
static_assert(offsetof(BgfxUIDisplayVertex, y) == sizeof(float));
static_assert(offsetof(BgfxUIDisplayVertex, abgr) == sizeof(float) * 2U);
static_assert(offsetof(BgfxUIDisplayVertex, u) == sizeof(float) * 2U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, v) == sizeof(float) * 3U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, shapeWidth) == sizeof(float) * 4U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, shapeHeight) == sizeof(float) * 5U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, shapeParameter) == sizeof(float) * 6U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, cornerRadiusTopLeft) ==
              sizeof(float) * 7U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, cornerRadiusTopRight) ==
              sizeof(float) * 8U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, cornerRadiusBottomRight) ==
              sizeof(float) * 9U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, cornerRadiusBottomLeft) ==
              sizeof(float) * 10U + sizeof(u32));
static_assert(sizeof(BgfxUIDisplayVertex) <= (std::numeric_limits<u16>::max)());
static_assert(sizeof(BgfxOpaque3DInstanceData) <= (std::numeric_limits<u16>::max)());
static_assert(std::is_standard_layout_v<BgfxSprite2DVertex>);
static_assert(sizeof(BgfxSprite2DVertex) == sizeof(float) * 4U + sizeof(u32));
static_assert(offsetof(BgfxSprite2DVertex, positionX) == 0U);
static_assert(offsetof(BgfxSprite2DVertex, positionY) == sizeof(float));
static_assert(offsetof(BgfxSprite2DVertex, textureU) == sizeof(float) * 2U);
static_assert(offsetof(BgfxSprite2DVertex, textureV) == sizeof(float) * 3U);
static_assert(offsetof(BgfxSprite2DVertex, abgr) == sizeof(float) * 4U);
static_assert(sizeof(BgfxSprite2DVertex) <= (std::numeric_limits<u16>::max)());

struct PreparedOpaque3D final {
    BgfxOpaque3DFrameRequirements requirements{};
    struct CascadedDirectionalShadow final {
        BgfxCascadedDirectionalShadowProjection projection{};
        u16 directionalLightIndex = 0;
        float depthBias = 0.0F;
        float normalBiasMeters = 0.0F;
    };
    std::optional<CascadedDirectionalShadow> cascadedDirectionalShadow{};
    struct SpotLightShadow final {
        BgfxSpotLightShadowProjection projection{};
        u16 spotLightIndex = 0;
        float depthBias = 0.0F;
        float normalBiasMeters = 0.0F;
    };
    std::optional<SpotLightShadow> spotLightShadow{};
    struct PointLightShadow final {
        BgfxPointLightShadowProjection projection{};
        u16 pointLightIndex = 0;
        float depthBias = 0.0F;
        float normalBiasMeters = 0.0F;
    };
    std::optional<PointLightShadow> pointLightShadow{};
};

struct PreparedSprite2D final {
    BgfxSprite2DFrameRequirements requirements{};
};

struct PreparedUIDisplayList final {
    u32 vertexCount = 0;
    u32 indexCount = 0;
};

using Mesh3DDirectionalLightUniformStorage =
    std::array<float, Mesh3DLightingDesc::MaximumDirectionalLightCount * 4U>;
using Mesh3DPointLightUniformStorage =
    std::array<float, Mesh3DLightingDesc::MaximumPointLightCount * 4U>;
using Mesh3DSpotLightUniformStorage =
    std::array<float, Mesh3DLightingDesc::MaximumSpotLightCount * 4U>;
using Sprite2DLightUniformStorage =
    std::array<float, Sprite2DLightingDesc::MaximumPointLightCount * 4U>;
using Sprite2DShadowUniformStorage =
    std::array<float, Sprite2DLightingDesc::MaximumShadowSegmentCount * 4U>;

void encodeMesh3DLighting(const Mesh3DLightingDesc& lighting,
                          Mesh3DDirectionalLightUniformStorage& directions,
                          Mesh3DDirectionalLightUniformStorage& directionalColors,
                          Mesh3DPointLightUniformStorage& pointPositionsAndRadii,
                          Mesh3DPointLightUniformStorage& pointColors,
                          Mesh3DSpotLightUniformStorage& spotPositionsAndRadii,
                          Mesh3DSpotLightUniformStorage& spotDirectionsAndInnerCosines,
                          Mesh3DSpotLightUniformStorage& spotColorsAndOuterCosines,
                          float& ambientScale) noexcept
{
    directions.fill(0.0F);
    directionalColors.fill(0.0F);
    pointPositionsAndRadii.fill(0.0F);
    pointColors.fill(0.0F);
    spotPositionsAndRadii.fill(0.0F);
    spotDirectionsAndInnerCosines.fill(0.0F);
    spotColorsAndOuterCosines.fill(0.0F);
    for (std::size_t lightIndex = 0; lightIndex < lighting.directionalLights.size(); ++lightIndex)
    {
        const Mesh3DDirectionalLight& source = lighting.directionalLights[lightIndex];
        const float lengthSquared = source.directionTowardLightX * source.directionTowardLightX +
                                    source.directionTowardLightY * source.directionTowardLightY +
                                    source.directionTowardLightZ * source.directionTowardLightZ;
        const float inverseLength = 1.0F / std::sqrt(lengthSquared);
        const std::size_t base = lightIndex * 4U;
        directions[base + 0U] = source.directionTowardLightX * inverseLength;
        directions[base + 1U] = source.directionTowardLightY * inverseLength;
        directions[base + 2U] = source.directionTowardLightZ * inverseLength;
        directions[base + 3U] = 1.0F;
        directionalColors[base + 0U] = source.colorR;
        directionalColors[base + 1U] = source.colorG;
        directionalColors[base + 2U] = source.colorB;
        directionalColors[base + 3U] = 1.0F;
    }
    for (std::size_t lightIndex = 0; lightIndex < lighting.pointLights.size(); ++lightIndex)
    {
        const Mesh3DPointLight& source = lighting.pointLights[lightIndex];
        const std::size_t base = lightIndex * 4U;
        pointPositionsAndRadii[base + 0U] = source.positionX;
        pointPositionsAndRadii[base + 1U] = source.positionY;
        pointPositionsAndRadii[base + 2U] = source.positionZ;
        pointPositionsAndRadii[base + 3U] = source.influenceRadius;
        pointColors[base + 0U] = source.colorR;
        pointColors[base + 1U] = source.colorG;
        pointColors[base + 2U] = source.colorB;
        pointColors[base + 3U] = 1.0F;
    }
    for (std::size_t lightIndex = 0; lightIndex < lighting.spotLights.size(); ++lightIndex)
    {
        const Mesh3DSpotLight& source = lighting.spotLights[lightIndex];
        const float lengthSquared = source.directionFromLightX * source.directionFromLightX +
                                    source.directionFromLightY * source.directionFromLightY +
                                    source.directionFromLightZ * source.directionFromLightZ;
        const float inverseLength = 1.0F / std::sqrt(lengthSquared);
        const std::size_t base = lightIndex * 4U;
        spotPositionsAndRadii[base + 0U] = source.positionX;
        spotPositionsAndRadii[base + 1U] = source.positionY;
        spotPositionsAndRadii[base + 2U] = source.positionZ;
        spotPositionsAndRadii[base + 3U] = source.influenceRadius;
        spotDirectionsAndInnerCosines[base + 0U] = source.directionFromLightX * inverseLength;
        spotDirectionsAndInnerCosines[base + 1U] = source.directionFromLightY * inverseLength;
        spotDirectionsAndInnerCosines[base + 2U] = source.directionFromLightZ * inverseLength;
        spotDirectionsAndInnerCosines[base + 3U] = source.innerConeCosine;
        spotColorsAndOuterCosines[base + 0U] = source.colorR;
        spotColorsAndOuterCosines[base + 1U] = source.colorG;
        spotColorsAndOuterCosines[base + 2U] = source.colorB;
        spotColorsAndOuterCosines[base + 3U] = source.outerConeCosine;
    }
    ambientScale = lighting.ambientScale;
}

void encodeSprite2DLighting(const Sprite2DLightingDesc& lighting,
                            Sprite2DLightUniformStorage& positionsAndRadii,
                            Sprite2DLightUniformStorage& colors,
                            Sprite2DShadowUniformStorage& shadowSegments,
                            std::array<float, 4>& params) noexcept
{
    positionsAndRadii.fill(0.0F);
    colors.fill(0.0F);
    shadowSegments.fill(0.0F);
    for (std::size_t lightIndex = 0; lightIndex < lighting.pointLights.size(); ++lightIndex)
    {
        const Sprite2DPointLight& source = lighting.pointLights[lightIndex];
        const std::size_t base = lightIndex * 4U;
        positionsAndRadii[base + 0U] = source.positionX;
        positionsAndRadii[base + 1U] = source.positionY;
        positionsAndRadii[base + 2U] = source.radiusMeters;
        positionsAndRadii[base + 3U] = 1.0F;
        colors[base + 0U] = source.colorR;
        colors[base + 1U] = source.colorG;
        colors[base + 2U] = source.colorB;
        colors[base + 3U] = source.sourceRadiusMeters;
    }
    for (std::size_t segmentIndex = 0; segmentIndex < lighting.shadowSegments.size(); ++segmentIndex)
    {
        const Sprite2DShadowSegment& source = lighting.shadowSegments[segmentIndex];
        const std::size_t base = segmentIndex * 4U;
        shadowSegments[base + 0U] = source.startX;
        shadowSegments[base + 1U] = source.startY;
        shadowSegments[base + 2U] = source.endX;
        shadowSegments[base + 3U] = source.endY;
    }
    params = {
        lighting.ambientScale,
        static_cast<float>(lighting.pointLights.size()),
        static_cast<float>(lighting.shadowSegments.size()),
        0.0F,
    };
}

struct BgfxScissorRect final {
    u16 x = 0;
    u16 y = 0;
    u16 width = 0;
    u16 height = 0;

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return width == 0 || height == 0;
    }
};

struct BgfxViewRect final {
    u16 x = 0;
    u16 y = 0;
    u16 width = 0;
    u16 height = 0;
};

struct DecodedNativeWindowBinding final {
    bgfx::PlatformData platformData{};
};

[[nodiscard]] void* toNativePointer(std::uintptr_t value) noexcept
{
    return reinterpret_cast<void*>(value);
}

[[nodiscard]] Core::Result<bgfx::PlatformData>
toBgfxPlatformData(const Integration::Detail::NativeWindowBinding& binding)
{
    if (binding.nativeWindow == 0 || binding.bindingRevision == 0)
    {
        return Core::failure(RenderErrorCode::InvalidNativeWindowBinding,
                             "The native window binding is missing its window handle or revision");
    }

    // Range-checked before the switch rather than handled by a default label. A default would
    // also swallow a newly added enumerator, which is the one case that should break the
    // build instead of failing at runtime -- the switch below is therefore exhaustive, and
    // this guard covers only values that are not enumerators at all.
    switch (binding.kind)
    {
    case Integration::Detail::NativeWindowBindingKind::Win32:
    case Integration::Detail::NativeWindowBindingKind::X11:
    case Integration::Detail::NativeWindowBindingKind::Wayland:
    case Integration::Detail::NativeWindowBindingKind::Android:
    case Integration::Detail::NativeWindowBindingKind::Html5:
    case Integration::Detail::NativeWindowBindingKind::Ios:
        break;
    default:
        return Core::failure(RenderErrorCode::InvalidNativeWindowBinding,
                             "The native window binding kind is not a known value");
    }

    bgfx::PlatformData platformData{};
    platformData.nwh = toNativePointer(binding.nativeWindow);

    switch (binding.kind)
    {
    case Integration::Detail::NativeWindowBindingKind::Win32:
        platformData.type = bgfx::NativeWindowHandleType::Default;
        break;
    case Integration::Detail::NativeWindowBindingKind::X11:
        if (binding.nativeDisplay == 0)
        {
            return Core::failure(RenderErrorCode::InvalidNativeWindowBinding,
                                 "The X11 native window binding is missing its Display pointer");
        }
        platformData.ndt = toNativePointer(binding.nativeDisplay);
        platformData.type = bgfx::NativeWindowHandleType::Default;
        break;
    case Integration::Detail::NativeWindowBindingKind::Wayland:
        if (binding.nativeDisplay == 0)
        {
            return Core::failure(RenderErrorCode::InvalidNativeWindowBinding,
                                 "The Wayland native window binding is missing its wl_display pointer");
        }
        platformData.ndt = toNativePointer(binding.nativeDisplay);
        platformData.type = bgfx::NativeWindowHandleType::Wayland;
        break;
    case Integration::Detail::NativeWindowBindingKind::Android:
        // ANativeWindow* is self-contained: bgfx's own Android entry hands it straight to
        // nwh with the default handle type and never sets ndt. A non-zero display here would
        // therefore be silently ignored, so reject it rather than accept a binding whose
        // extra field means nothing.
        if (binding.nativeDisplay != 0)
        {
            return Core::failure(RenderErrorCode::InvalidNativeWindowBinding,
                                 "The Android native window binding must not carry a display pointer");
        }
        platformData.type = bgfx::NativeWindowHandleType::Default;
        break;
    case Integration::Detail::NativeWindowBindingKind::Html5:
        // nwh is a CSS selector string, not a handle. bgfx's HTML5 GL context copies it out
        // with bx::strCopy before returning, so the string only has to outlive bgfx::init.
        // There is no display to carry, so a non-zero one would be silently ignored.
        if (binding.nativeDisplay != 0)
        {
            return Core::failure(RenderErrorCode::InvalidNativeWindowBinding,
                                 "The HTML5 native window binding must not carry a display pointer");
        }
        platformData.type = bgfx::NativeWindowHandleType::Default;
        break;
    case Integration::Detail::NativeWindowBindingKind::Ios:
        // CAMetalLayer* goes straight to nwh: bgfx's SwapChainMtl::init casts it and verifies the
        // class with isKindOfClass:, so the handle type stays Default (Wayland is the only other
        // value the enum has, and it is a Linux windowing concept). There is no display to carry,
        // so a non-zero one would be silently ignored.
        if (binding.nativeDisplay != 0)
        {
            return Core::failure(RenderErrorCode::InvalidNativeWindowBinding,
                                 "The iOS native window binding must not carry a display pointer");
        }
        platformData.type = bgfx::NativeWindowHandleType::Default;
        break;
    }

    // No default label: every enumerator is handled above, so adding a binding kind must
    // fail to compile here rather than fall through to a runtime rejection.

    return platformData;
}

[[nodiscard]] Core::Result<DecodedNativeWindowBinding>
decodeNativeWindowBinding(const Integration::NativeWindowSurfaceLease& lease)
{
    auto binding = Integration::Detail::NativeWindowSurfaceLeaseAccess::decode(lease);
    if (!binding)
    {
        return Core::failure(std::move(binding.error()));
    }

    auto platformData = toBgfxPlatformData(*binding);
    if (!platformData)
    {
        return Core::failure(std::move(platformData.error()));
    }

    return DecodedNativeWindowBinding{
        .platformData = *platformData,
    };
}

[[nodiscard]] RenderSurfaceId toRenderSurfaceId(Integration::WindowSurfaceId surface) noexcept
{
    if (!surface.hasValue())
    {
        return {};
    }
    return RenderSurfaceId{
        .owner = surface.owner().value(),
        .index = surface.index(),
        .generation = surface.generation(),
    };
}

[[nodiscard]] Core::Status bgfxInitFailed()
{
    return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                         "bgfx failed to initialize the primary window render device");
}

[[nodiscard]] Core::Error transientBufferCapacityError(std::string_view message)
{
    return Core::Error{RenderErrorCode::TransientBufferCapacityExceeded, message};
}

[[nodiscard]] Core::Status validateDisplayListBatches(UIDisplayListView displayList)
{
    if (displayList.empty())
    {
        if (!displayList.batches().empty())
        {
            return Core::failure(RenderErrorCode::InvalidDrawCommand,
                                 "An empty UI DisplayList must not contain draw batches");
        }
        return Core::success();
    }
    if (displayList.batches().empty())
    {
        return Core::failure(RenderErrorCode::InvalidDrawCommand,
                             "A non-empty UI DisplayList must contain draw batches");
    }

    usize nextCommand = 0;
    for (const UIDrawBatch& batch : displayList.batches())
    {
        if (batch.commandCount == 0)
        {
            return Core::failure(RenderErrorCode::InvalidDrawCommand,
                                 "A bgfx UI draw batch is empty");
        }
        if (batch.kind != UIDrawCommandKind::SolidQuad &&
            batch.kind != UIDrawCommandKind::SolidEllipse &&
            batch.kind != UIDrawCommandKind::Glyph &&
            batch.kind != UIDrawCommandKind::ImageQuad)
        {
            return Core::failure(RenderErrorCode::InvalidDrawCommand,
                                 "A bgfx UI draw batch has an unsupported command kind");
        }
        if (batch.kind == UIDrawCommandKind::Glyph
            && batch.atlasPage >= BgfxUIAtlasPageTable::MaxPages)
        {
            return Core::failure(RenderErrorCode::InvalidDrawCommand,
                                 "A bgfx UI Glyph batch references an out-of-range atlas page");
        }
        if (batch.kind == UIDrawCommandKind::ImageQuad &&
            (!batch.texture.hasValue() ||
             (batch.sampling != UITextureSampling::Linear && batch.sampling != UITextureSampling::Nearest)))
        {
            return Core::failure(RenderErrorCode::InvalidDrawCommand,
                                 "A bgfx UI ImageQuad batch has an invalid texture or sampling mode");
        }
        if (static_cast<usize>(batch.firstCommand) != nextCommand)
        {
            return Core::failure(RenderErrorCode::InvalidDrawCommand,
                                 "UI draw batches must cover commands contiguously in paint order");
        }
        if (static_cast<usize>(batch.commandCount) > displayList.commands().size() - nextCommand)
        {
            return Core::failure(RenderErrorCode::InvalidDrawCommand,
                                 "A UI draw batch exceeds the DisplayList command range");
        }
        if (batch.clip.hasClip() && displayList.resolveClip(batch.clip) == nullptr)
        {
            return Core::failure(RenderErrorCode::InvalidDrawCommand,
                                 "A UI draw batch references an invalid clip rectangle");
        }

        const usize batchEnd = nextCommand + static_cast<usize>(batch.commandCount);
        for (; nextCommand < batchEnd; ++nextCommand)
        {
            const UIDrawCommand& command = displayList.commands()[nextCommand];
            if (command.kind != batch.kind || command.clip != batch.clip ||
                (batch.kind == UIDrawCommandKind::ImageQuad &&
                 (command.texture != batch.texture || command.sampling != batch.sampling)))
            {
                return Core::failure(RenderErrorCode::InvalidDrawCommand,
                                     "A UI draw batch does not match its command range");
            }
        }
    }

    if (nextCommand != displayList.commands().size())
    {
        return Core::failure(RenderErrorCode::InvalidDrawCommand,
                             "UI draw batches do not cover every DisplayList command");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<PreparedUIDisplayList>
preflightUIDisplayList(UIDisplayListView displayList, FrameResourceTableView resources)
{
    auto requirements = checkedGeometryRequirements(displayList);
    if (!requirements)
    {
        if (requirements.error().code == Core::CoreErrorCode::CapacityExceeded)
        {
            return Core::failure(
                transientBufferCapacityError("UI DisplayList geometry exceeds the bgfx transient buffer count limits"));
        }
        return Core::failure(std::move(requirements.error()));
    }
    if (auto status = validateDisplayListBatches(displayList); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    for (const UIDrawCommand& command : displayList.commands())
    {
        if (command.kind != UIDrawCommandKind::ImageQuad)
        {
            continue;
        }
        const FrameResourceDescriptor* descriptor =
            resources.resolve(command.texture, FrameResourceKind::Texture2D);
        if (descriptor == nullptr ||
            descriptor->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
        {
            return Core::failure(RenderErrorCode::InvalidFrameResource,
                                 "A bgfx UI ImageQuad references an invalid Texture2D frame resource");
        }
    }

    constexpr usize MaxBgfxElementCount = static_cast<usize>((std::numeric_limits<u32>::max)());
    if (requirements->vertexCount > MaxBgfxElementCount || requirements->indexCount > MaxBgfxElementCount)
    {
        return Core::failure(
            transientBufferCapacityError("UI DisplayList geometry exceeds bgfx's u32 transient buffer count API"));
    }

    const auto vertexCount = static_cast<u32>(requirements->vertexCount);
    const auto indexCount = static_cast<u32>(requirements->indexCount);
    if (vertexCount == 0)
    {
        return PreparedUIDisplayList{};
    }

    const bgfx::Caps* const caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_INDEX32) == 0)
    {
        return Core::failure(
            transientBufferCapacityError("The active bgfx renderer does not support 32-bit UI transient indices"));
    }
    return PreparedUIDisplayList{
        .vertexCount = vertexCount,
        .indexCount = indexCount,
    };
}

[[nodiscard]] Core::Result<PreparedOpaque3D>
preflightOpaque3D(RenderSceneView scene, FrameResourceTableView resources,
                  u16 directionalCascadeTileExtent)
{
    auto requirements = checkedOpaque3DFrame(scene, resources);
    if (!requirements)
    {
        return Core::failure(std::move(requirements.error()));
    }
    if (auto status = validateSkinnedOpaque3DFrame(scene, resources); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    // Static items, including transparent ones, share the instance buffer.
    if (requirements->instanceCount == 0)
    {
        return PreparedOpaque3D{.requirements = *requirements};
    }

    const bgfx::Caps* const caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_INSTANCING) == 0)
    {
        return Core::failure(Core::CoreErrorCode::Unsupported,
                             "The active bgfx renderer does not support Opaque3D instancing");
    }
    PreparedOpaque3D prepared{.requirements = *requirements};
    // Only opaque static batches cast shadows. Transparent-only static scenes
    // still require instancing but must not prepare shadow projections.
    if (requirements->batchCount == 0)
    {
        return prepared;
    }
    if (!scene.perspectiveCamera().has_value() ||
        !scene.mesh3DLighting().has_value())
    {
        return prepared;
    }

    const RenderMesh3DLighting& lighting = *scene.mesh3DLighting();
    if (const auto& cascadedShadow = lighting.cascadedDirectionalShadow();
        cascadedShadow.has_value())
    {
        const Mesh3DCascadedDirectionalShadow& shadow = *cascadedShadow;
        const std::span<const Mesh3DDirectionalLight> directionalLights =
            lighting.directionalLights();
        if (shadow.directionalLightIndex >= directionalLights.size())
        {
            std::terminate();
        }
        auto projection = computeCascadedDirectionalShadowProjection(
            BgfxCascadedDirectionalShadowInput{
                .camera = *scene.perspectiveCamera(),
                .light = directionalLights[shadow.directionalLightIndex],
                .maximumDistanceMeters = shadow.maximumDistanceMeters,
                // The snap grid must be the atlas grid, so the configured tile extent has to
                // reach the math rather than the math assuming the default.
                .tileExtent = directionalCascadeTileExtent,
            },
            caps->homogeneousDepth,
            caps->originBottomLeft);
        if (!projection)
        {
            return Core::failure(std::move(projection.error()));
        }
        prepared.cascadedDirectionalShadow = PreparedOpaque3D::CascadedDirectionalShadow{
            .projection = *projection,
            .directionalLightIndex = static_cast<u16>(shadow.directionalLightIndex),
            .depthBias = shadow.depthBias,
            .normalBiasMeters = shadow.normalBiasMeters,
        };
    }

    if (const auto& spotLightShadow = lighting.spotLightShadow();
        spotLightShadow.has_value())
    {
        const Mesh3DSpotLightShadow& shadow = *spotLightShadow;
        const std::span<const Mesh3DSpotLight> spotLights = lighting.spotLights();
        if (shadow.spotLightIndex >= spotLights.size())
        {
            std::terminate();
        }
        auto projection = computeSpotLightShadowProjection(
            BgfxSpotLightShadowInput{
                .light = spotLights[shadow.spotLightIndex],
                .nearPlaneMeters = shadow.nearPlaneMeters,
            },
            caps->homogeneousDepth,
            caps->originBottomLeft);
        if (!projection)
        {
            return Core::failure(std::move(projection.error()));
        }
        prepared.spotLightShadow = PreparedOpaque3D::SpotLightShadow{
            .projection = *projection,
            .spotLightIndex = static_cast<u16>(shadow.spotLightIndex),
            .depthBias = shadow.depthBias,
            .normalBiasMeters = shadow.normalBiasMeters,
        };
    }
    if (const auto& pointLightShadow = lighting.pointLightShadow();
        pointLightShadow.has_value())
    {
        const Mesh3DPointLightShadow& shadow = *pointLightShadow;
        const std::span<const Mesh3DPointLight> pointLights = lighting.pointLights();
        if (shadow.pointLightIndex >= pointLights.size())
        {
            std::terminate();
        }
        auto projection = computePointLightShadowProjection(
            BgfxPointLightShadowInput{
                .light = pointLights[shadow.pointLightIndex],
                .nearPlaneMeters = shadow.nearPlaneMeters,
            },
            caps->homogeneousDepth,
            caps->originBottomLeft);
        if (!projection)
        {
            return Core::failure(std::move(projection.error()));
        }
        prepared.pointLightShadow = PreparedOpaque3D::PointLightShadow{
            .projection = *projection,
            .pointLightIndex = static_cast<u16>(shadow.pointLightIndex),
            .depthBias = shadow.depthBias,
            .normalBiasMeters = shadow.normalBiasMeters,
        };
    }
    return prepared;
}

[[nodiscard]] Core::Result<PreparedSprite2D>
preflightSprite2D(RenderSceneView scene, FrameResourceTableView resources)
{
    auto requirements = checkedSprite2DFrame(scene, resources);
    if (!requirements)
    {
        if (requirements.error().code == Core::CoreErrorCode::CapacityExceeded)
        {
            return Core::failure(
                transientBufferCapacityError("Sprite2D geometry exceeds the bgfx transient buffer count limits"));
        }
        return Core::failure(std::move(requirements.error()));
    }
    if (requirements->spriteCount == 0)
    {
        return PreparedSprite2D{.requirements = *requirements};
    }

    const bgfx::Caps* const caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_INDEX32) == 0)
    {
        return Core::failure(transientBufferCapacityError(
            "The active bgfx renderer does not support 32-bit Sprite2D transient indices"));
    }
    return PreparedSprite2D{.requirements = *requirements};
}

[[nodiscard]] Core::Status preflightTransientVertexPool(PreparedOpaque3D opaque3D, PreparedSprite2D sprite2D,
                                                        PreparedUIDisplayList ui, const bgfx::VertexLayout& byteLayout)
{
    constexpr u16 InstanceStride = static_cast<u16>(sizeof(BgfxOpaque3DInstanceData));
    const std::array requests{
        BgfxTransientVertexRequest{
            .count = opaque3D.requirements.instanceCount,
            .stride = InstanceStride,
        },
        BgfxTransientVertexRequest{
            .count = sprite2D.requirements.vertexCount,
            .stride = static_cast<u16>(sizeof(BgfxSprite2DVertex)),
        },
        BgfxTransientVertexRequest{
            .count = ui.vertexCount,
            .stride = static_cast<u16>(sizeof(BgfxUIDisplayVertex)),
        },
    };
    auto budget = checkedTransientVertexBudget(requests);
    if (!budget)
    {
        if (budget.error().code == Core::CoreErrorCode::CapacityExceeded)
        {
            return Core::failure(transientBufferCapacityError(
                "Opaque3D, Sprite2D and UI geometry exceed the bgfx transient vertex count limits"));
        }
        return Core::failure(std::move(budget.error()));
    }
    if (*budget != 0 && bgfx::getAvailTransientVertexBuffer(*budget, byteLayout) != *budget)
    {
        return Core::failure(transientBufferCapacityError(
            "The shared bgfx transient vertex pool cannot hold this frame's Opaque3D, Sprite2D and UI data"));
    }
    return Core::success();
}

[[nodiscard]] Core::Status preflightTransientIndexPool(PreparedSprite2D sprite2D, PreparedUIDisplayList ui)
{
    const std::array indexCounts{
        sprite2D.requirements.indexCount,
        ui.indexCount,
    };
    auto budget = checkedTransientIndexBudget(indexCounts);
    if (!budget)
    {
        if (budget.error().code == Core::CoreErrorCode::CapacityExceeded)
        {
            return Core::failure(
                transientBufferCapacityError("Sprite2D and UI geometry exceed the bgfx transient index count limits"));
        }
        return Core::failure(std::move(budget.error()));
    }
    if (*budget != 0 && bgfx::getAvailTransientIndexBuffer(*budget, true) != *budget)
    {
        return Core::failure(transientBufferCapacityError(
            "The shared bgfx transient index pool cannot hold this frame's Sprite2D and UI data"));
    }
    return Core::success();
}

[[nodiscard]] BgfxViewRect viewportRect(const RenderSurfaceState& surface, RenderNormalizedViewport viewport) noexcept
{
    const double surfaceWidth = surface.framebufferExtent.width;
    const double surfaceHeight = surface.framebufferExtent.height;
    const u32 left = static_cast<u32>(std::clamp(std::floor(viewport.x * surfaceWidth), 0.0, surfaceWidth));
    const u32 top = static_cast<u32>(std::clamp(std::floor(viewport.y * surfaceHeight), 0.0, surfaceHeight));
    const u32 right =
        static_cast<u32>(std::clamp(std::ceil((viewport.x + viewport.width) * surfaceWidth), 0.0, surfaceWidth));
    const u32 bottom =
        static_cast<u32>(std::clamp(std::ceil((viewport.y + viewport.height) * surfaceHeight), 0.0, surfaceHeight));
    return BgfxViewRect{
        .x = static_cast<u16>(left),
        .y = static_cast<u16>(top),
        .width = static_cast<u16>(right - left),
        .height = static_cast<u16>(bottom - top),
    };
}

[[nodiscard]] BgfxScissorRect clampScissorToSurface(const UIPixelRect& clip, RenderSurfaceExtent surfaceExtent) noexcept
{
    const i64 surfaceRight = static_cast<i64>(surfaceExtent.width);
    const i64 surfaceBottom = static_cast<i64>(surfaceExtent.height);
    const i64 clipLeft = std::clamp(static_cast<i64>(clip.x), i64{0}, surfaceRight);
    const i64 clipTop = std::clamp(static_cast<i64>(clip.y), i64{0}, surfaceBottom);
    const i64 clipRight = std::clamp(static_cast<i64>(clip.x) + static_cast<i64>(clip.width), i64{0}, surfaceRight);
    const i64 clipBottom = std::clamp(static_cast<i64>(clip.y) + static_cast<i64>(clip.height), i64{0}, surfaceBottom);
    if (clipRight <= clipLeft || clipBottom <= clipTop)
    {
        return {};
    }

    return BgfxScissorRect{
        .x = static_cast<u16>(clipLeft),
        .y = static_cast<u16>(clipTop),
        .width = static_cast<u16>(clipRight - clipLeft),
        .height = static_cast<u16>(clipBottom - clipTop),
    };
}

class BgfxRenderDevice final : public IRenderDevice {
  public:
    BgfxRenderDevice(Detail::RenderSurfaceStateTracker surfaceStateTracker, Integration::NativeWindowSurfaceLease lease,
                     RenderSurfaceState initialSurface, ShadowMapExtentConfig shadowMapExtents,
                     u32 drawCallCapacity, u32 resetFlags,
                     bgfx::RendererType::Enum requestedRenderer) noexcept
        : surfaceStateTracker_(std::move(surfaceStateTracker)), lease_(std::move(lease)),
          ownerThread_(std::this_thread::get_id()), committedSurfaceState_(initialSurface),
          shadowMapExtents_(shadowMapExtents), drawCallCapacity_(drawCallCapacity),
          resetFlags_(resetFlags), requestedRenderer_(requestedRenderer)
    {
    }

    ~BgfxRenderDevice() noexcept override
    {
        shutdown();
    }

    BgfxRenderDevice(const BgfxRenderDevice&) = delete;
    BgfxRenderDevice& operator=(const BgfxRenderDevice&) = delete;

    [[nodiscard]] Core::Status initialize(const bgfx::PlatformData& platformData)
    {
        const RenderSurfaceExtent initialBackbuffer =
            BgfxSurfaceFramePlanner::bootstrapBackbufferExtent(committedSurfaceState_);

        bgfx::Init init{};
        // Count is bgfx's "you choose" sentinel. Tina only passes it when the caller
        // asked for Automatic *and* has no platform preference; otherwise the resolved
        // renderer is named explicitly, because bgfx's own scoring never selects Vulkan
        // on Android even though it is compiled in there.
        init.type = requestedRenderer_;
        init.platformData = platformData;
        init.resolution.width = initialBackbuffer.width;
        init.resolution.height = initialBackbuffer.height;
        init.resolution.reset = resetFlags_;
        // Tina submits from the RenderDevice owner thread only. Explicitly
        // right-size bgfx's two frame-local render-item arrays and its per-
        // encoder uniform buffers instead of accepting the 65K/8-encoder
        // general-purpose defaults.
        init.limits.maxEncoders = 1;
        init.limits.numDrawCalls = drawCallCapacity_;
        init.limits.numDrawCallPeakFrames = 0;
        // M11-D1: required for requestScreenShot / product pixel evidence.
        init.callback = &captureCallback_;
        // Without this the backend never probes the decoder, so caps->codecs stays zero and
        // videoDecodeCapabilities() can only ever report "unsupported". bgfx's probe cannot
        // fail initialization: an unsupported device simply reports no codecs.
        init.videoDecode = true;

        if (!bgfx::init(init))
        {
            return bgfxInitFailed();
        }

        bgfxInitialized_ = true;
        appliedBackbuffer_ = initialBackbuffer;

        // bgfx accepts an explicit type and may still fall back to another renderer
        // when creation fails. A game that asked for Vulkan and silently got GL would
        // ship with the wrong performance characteristics and no way to tell, so an
        // unhonoured request fails device creation instead.
        if (requestedRenderer_ != bgfx::RendererType::Count &&
            bgfx::getRendererType() != requestedRenderer_)
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx created a different renderer than the requested graphics API");
        }

        const bgfx::Caps* const caps = bgfx::getCaps();
        constexpr u64 RequiredRetirementCaps =
            BGFX_CAPS_TEXTURE_BLIT | BGFX_CAPS_TEXTURE_READ_BACK;
        retirementMarkerSupported_ =
            caps != nullptr && (caps->supported & RequiredRetirementCaps) == RequiredRetirementCaps;
        if (retirementMarkerSupported_)
        {
            constexpr std::array<u8, 4> MarkerPixel{0x54, 0x49, 0x4e, 0x41};
            retirementMarkerSource_ = bgfx::createTexture2D(
                1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                bgfx::copy(MarkerPixel.data(), static_cast<u32>(MarkerPixel.size())));
            if (!bgfx::isValid(retirementMarkerSource_))
            {
                return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                     "bgfx rejected the GPU retirement marker source texture");
            }
            ++statistics_.liveResources;

            retirementMarkerReadback_ = bgfx::createTexture2D(
                1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
            if (!bgfx::isValid(retirementMarkerReadback_))
            {
                return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                     "bgfx rejected the GPU retirement marker readback texture");
            }
            ++statistics_.liveResources;
        }

        uiVertexLayout_.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
            .end();
        sprite2DVertexLayout_.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
        transientByteLayout_.begin().add(bgfx::Attrib::TexCoord7, 1, bgfx::AttribType::Uint8).end();
        if (transientByteLayout_.getStride() != 1U)
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx did not create the one-byte transient capacity layout");
        }
        opaque3DVertexLayout_.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        if (opaque3DVertexLayout_.getStride() != sizeof(BgfxOpaque3DVertex))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx created an unexpected Opaque3D vertex stride");
        }
        // Skinned stream 1: four u8 joint indices (0..255 covers the frozen 256
        // joint bound) + four float32 weights converted from the cooked u16
        // fixed-point values during upload.
        opaque3DSkinVertexLayout_.begin()
            .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8)
            .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
            .end();
        if (opaque3DSkinVertexLayout_.getStride() != sizeof(BgfxOpaque3DSkinVertex))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx created an unexpected Opaque3D skin vertex stride");
        }

        auto uiProgram = ShaderDetail::createUITexturedQuadProgram();
        if (!uiProgram)
        {
            return Core::failure(std::move(uiProgram.error()));
        }
        uiCoverageProgram_ = *uiProgram;
        ++statistics_.liveResources;

        auto uiImageProgram = ShaderDetail::createUIImageQuadProgram();
        if (!uiImageProgram)
        {
            return Core::failure(std::move(uiImageProgram.error()));
        }
        uiImageQuadProgram_ = *uiImageProgram;
        ++statistics_.liveResources;

        auto white = createUISolidWhiteTexture();
        if (!white)
        {
            return Core::failure(std::move(white.error()));
        }
        uiSolidWhiteTexture_ = *white;
        ++statistics_.liveResources;
        uiTexColorUniform_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(uiTexColorUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the UI texture sampler uniform");
        }
        ++statistics_.liveResources;

        auto sprite2DProgram = ShaderDetail::createSprite2DFixtureProgram();
        if (!sprite2DProgram)
        {
            return Core::failure(std::move(sprite2DProgram.error()));
        }
        sprite2DProgram_ = *sprite2DProgram;
        ++statistics_.liveResources;

        auto sprite2DVertexShader = ShaderDetail::createSprite2DVertexShader();
        if (!sprite2DVertexShader)
        {
            return Core::failure(std::move(sprite2DVertexShader.error()));
        }
        sprite2DVertexShader_ = *sprite2DVertexShader;
        ++statistics_.liveResources;

        sprite2DSampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(sprite2DSampler_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Sprite2D texture sampler uniform");
        }
        ++statistics_.liveResources;

        sprite2DNormalSampler_ = bgfx::createUniform("s_normalTex", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(sprite2DNormalSampler_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Sprite2D normal texture sampler uniform");
        }
        ++statistics_.liveResources;

        sprite2DLightPositionsUniform_ = bgfx::createUniform(
            "u_spriteLightPosRadius", bgfx::UniformType::Vec4,
            static_cast<u16>(Sprite2DLightingDesc::MaximumPointLightCount));
        if (!bgfx::isValid(sprite2DLightPositionsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Sprite2D point-light position array uniform");
        }
        ++statistics_.liveResources;

        sprite2DLightColorsUniform_ = bgfx::createUniform(
            "u_spriteLightColors", bgfx::UniformType::Vec4,
            static_cast<u16>(Sprite2DLightingDesc::MaximumPointLightCount));
        if (!bgfx::isValid(sprite2DLightColorsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Sprite2D point-light color array uniform");
        }
        ++statistics_.liveResources;

        sprite2DLightParamsUniform_ = bgfx::createUniform("u_spriteLightParams", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(sprite2DLightParamsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Sprite2D lighting params uniform");
        }
        ++statistics_.liveResources;

        sprite2DNormalParamsUniform_ = bgfx::createUniform("u_spriteNormalParams", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(sprite2DNormalParamsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Sprite2D normal-map params uniform");
        }
        ++statistics_.liveResources;

        sprite2DShadowSegmentsUniform_ = bgfx::createUniform(
            "u_spriteShadowSegments", bgfx::UniformType::Vec4,
            static_cast<u16>(Sprite2DLightingDesc::MaximumShadowSegmentCount));
        if (!bgfx::isValid(sprite2DShadowSegmentsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Sprite2D shadow-segment array uniform");
        }
        ++statistics_.liveResources;

        // Default 1x1 white RGBA8 so vertex color remains visible until product textures bind.
        constexpr std::array<u8, 4> WhitePixel{255, 255, 255, 255};
        const bgfx::Memory* whiteMemory = bgfx::copy(WhitePixel.data(), static_cast<u32>(WhitePixel.size()));
        sprite2DDefaultTexture_ =
            bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
                                  whiteMemory);
        if (!bgfx::isValid(sprite2DDefaultTexture_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Sprite2D default white texture");
        }
        ++statistics_.liveResources;

        // Flat +Z tangent-space normal. It remains bound when a batch has no map;
        // u_spriteNormalParams keeps that batch on the exact pre-normal-map path.
        constexpr std::array<u8, 4> FlatNormalPixel{128, 128, 255, 255};
        const bgfx::Memory* flatNormalMemory =
            bgfx::copy(FlatNormalPixel.data(), static_cast<u32>(FlatNormalPixel.size()));
        sprite2DDefaultNormalTexture_ = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            flatNormalMemory);
        if (!bgfx::isValid(sprite2DDefaultNormalTexture_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Sprite2D default flat-normal texture");
        }
        ++statistics_.liveResources;

        // RENDER-001: product Opaque3D uses experimental metallic-roughness hybrid.
        auto opaque3DProgram = ShaderDetail::createOpaque3DMrProgram();
        if (!opaque3DProgram)
        {
            return Core::failure(std::move(opaque3DProgram.error()));
        }
        opaque3DProgram_ = *opaque3DProgram;
        ++statistics_.liveResources;

        auto opaque3DCsmDepthProgram =
            ShaderDetail::createOpaque3DCascadedShadowDepthProgram();
        if (!opaque3DCsmDepthProgram)
        {
            return Core::failure(std::move(opaque3DCsmDepthProgram.error()));
        }
        opaque3DCsmDepthProgram_ = *opaque3DCsmDepthProgram;
        ++statistics_.liveResources;

        opaque3DCsmAtlasSampler_ =
            bgfx::createUniform("s_csmAtlas", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(opaque3DCsmAtlasSampler_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                  "bgfx rejected the Opaque3D CSM atlas sampler uniform");
        }
        ++statistics_.liveResources;

        opaque3DCsmMatricesUniform_ = bgfx::createUniform(
            "u_csmMatrices", bgfx::UniformType::Mat4,
            static_cast<u16>(BgfxCascadedDirectionalShadowCascadeCount));
        if (!bgfx::isValid(opaque3DCsmMatricesUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                  "bgfx rejected the Opaque3D CSM matrix array uniform");
        }
        ++statistics_.liveResources;

        opaque3DCsmSplitDepthsUniform_ =
            bgfx::createUniform("u_csmSplitDepths", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DCsmSplitDepthsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                  "bgfx rejected the Opaque3D CSM split-depth uniform");
        }
        ++statistics_.liveResources;

        opaque3DCsmParamsUniform_ =
            bgfx::createUniform("u_csmParams", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DCsmParamsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D CSM params uniform");
        }
        ++statistics_.liveResources;

        opaque3DSpotShadowMapSampler_ =
            bgfx::createUniform("s_spotShadowMap", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(opaque3DSpotShadowMapSampler_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D spot shadow-map sampler uniform");
        }
        ++statistics_.liveResources;

        opaque3DSpotShadowMatrixUniform_ =
            bgfx::createUniform("u_spotShadowMatrix", bgfx::UniformType::Mat4);
        if (!bgfx::isValid(opaque3DSpotShadowMatrixUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D spot shadow matrix uniform");
        }
        ++statistics_.liveResources;

        opaque3DSpotShadowParamsUniform_ =
            bgfx::createUniform("u_spotShadowParams", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DSpotShadowParamsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D spot shadow params uniform");
        }
        ++statistics_.liveResources;

        for (usize faceIndex = 0; faceIndex < opaque3DPointShadowMapSamplers_.size();
             ++faceIndex)
        {
            opaque3DPointShadowMapSamplers_[faceIndex] = bgfx::createUniform(
                kPointLightShadowSamplerNames[faceIndex], bgfx::UniformType::Sampler);
            if (!bgfx::isValid(opaque3DPointShadowMapSamplers_[faceIndex]))
            {
                return Core::failure(
                    RenderErrorCode::DeviceInitializationFailed,
                    "bgfx rejected an Opaque3D point shadow-map sampler uniform");
            }
            ++statistics_.liveResources;
        }

        opaque3DPointShadowMatricesUniform_ = bgfx::createUniform(
            "u_pointShadowMatrices", bgfx::UniformType::Mat4,
            static_cast<u16>(BgfxPointLightShadowFaceCount));
        if (!bgfx::isValid(opaque3DPointShadowMatricesUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D point shadow matrix array uniform");
        }
        ++statistics_.liveResources;

        opaque3DPointShadowParamsUniform_ =
            bgfx::createUniform("u_pointShadowParams", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DPointShadowParamsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D point shadow params uniform");
        }
        ++statistics_.liveResources;

        if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D16,
                                  kShadowSamplerFallbackFlags))
        {
            return Core::failure(
                RenderErrorCode::DeviceInitializationFailed,
                "The active bgfx renderer cannot create the sampled D16 shadow fallback texture");
        }
        opaque3DDefaultShadowTexture_ = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::D16,
            kShadowSamplerFallbackFlags, nullptr);
        if (!bgfx::isValid(opaque3DDefaultShadowTexture_))
        {
            return Core::failure(
                RenderErrorCode::DeviceInitializationFailed,
                "bgfx rejected the Opaque3D shadow fallback texture");
        }
        ++statistics_.liveResources;

        opaque3DSampler_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(opaque3DSampler_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D texture sampler uniform");
        }
        ++statistics_.liveResources;

        opaque3DMrSampler_ = bgfx::createUniform("s_texMR", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(opaque3DMrSampler_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D metallic-roughness sampler uniform");
        }
        ++statistics_.liveResources;

        opaque3DLightDirectionsUniform_ = bgfx::createUniform(
            "u_lightDirs", bgfx::UniformType::Vec4,
            static_cast<u16>(Mesh3DLightingDesc::MaximumDirectionalLightCount));
        if (!bgfx::isValid(opaque3DLightDirectionsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D light direction array uniform");
        }
        ++statistics_.liveResources;

        opaque3DLightColorsUniform_ = bgfx::createUniform(
            "u_lightColors", bgfx::UniformType::Vec4,
            static_cast<u16>(Mesh3DLightingDesc::MaximumDirectionalLightCount));
        if (!bgfx::isValid(opaque3DLightColorsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D light color array uniform");
        }
        ++statistics_.liveResources;

        opaque3DPointLightPositionsUniform_ = bgfx::createUniform(
            "u_pointLightPosRadius", bgfx::UniformType::Vec4,
            static_cast<u16>(Mesh3DLightingDesc::MaximumPointLightCount));
        if (!bgfx::isValid(opaque3DPointLightPositionsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D point light position/radius array uniform");
        }
        ++statistics_.liveResources;

        opaque3DPointLightColorsUniform_ = bgfx::createUniform(
            "u_pointLightColors", bgfx::UniformType::Vec4,
            static_cast<u16>(Mesh3DLightingDesc::MaximumPointLightCount));
        if (!bgfx::isValid(opaque3DPointLightColorsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D point light color array uniform");
        }
        ++statistics_.liveResources;

        opaque3DSpotLightPositionsUniform_ = bgfx::createUniform(
            "u_spotLightPosRadius", bgfx::UniformType::Vec4,
            static_cast<u16>(Mesh3DLightingDesc::MaximumSpotLightCount));
        if (!bgfx::isValid(opaque3DSpotLightPositionsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D spot light position/radius array uniform");
        }
        ++statistics_.liveResources;

        opaque3DSpotLightDirectionsUniform_ = bgfx::createUniform(
            "u_spotLightDirInner", bgfx::UniformType::Vec4,
            static_cast<u16>(Mesh3DLightingDesc::MaximumSpotLightCount));
        if (!bgfx::isValid(opaque3DSpotLightDirectionsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D spot light direction/inner-cone array uniform");
        }
        ++statistics_.liveResources;

        opaque3DSpotLightColorsUniform_ = bgfx::createUniform(
            "u_spotLightColorOuter", bgfx::UniformType::Vec4,
            static_cast<u16>(Mesh3DLightingDesc::MaximumSpotLightCount));
        if (!bgfx::isValid(opaque3DSpotLightColorsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D spot light color/outer-cone array uniform");
        }
        ++statistics_.liveResources;

        opaque3DMrParamsUniform_ = bgfx::createUniform("u_mrParams", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DMrParamsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D metallic-roughness params uniform");
        }
        ++statistics_.liveResources;

        opaque3DEmissiveFactorUniform_ =
            bgfx::createUniform("u_emissiveFactor", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DEmissiveFactorUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D emissive factor uniform");
        }
        ++statistics_.liveResources;

        opaque3DNormalSampler_ = bgfx::createUniform("s_texNormal", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(opaque3DNormalSampler_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D normal sampler uniform");
        }
        ++statistics_.liveResources;

        opaque3DNormalParamsUniform_ = bgfx::createUniform("u_normalParams", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DNormalParamsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D normal params uniform");
        }
        ++statistics_.liveResources;

        opaque3DIblDiffuseSampler_ = bgfx::createUniform("s_iblDiffuse", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(opaque3DIblDiffuseSampler_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D diffuse IBL sampler uniform");
        }
        ++statistics_.liveResources;

        opaque3DIblSpecularSampler_ = bgfx::createUniform("s_iblSpecular", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(opaque3DIblSpecularSampler_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D specular IBL sampler uniform");
        }
        ++statistics_.liveResources;

        opaque3DIblBrdfSampler_ = bgfx::createUniform("s_iblBrdf", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(opaque3DIblBrdfSampler_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D BRDF LUT sampler uniform");
        }
        ++statistics_.liveResources;

        opaque3DIblParamsUniform_ = bgfx::createUniform("u_iblParams", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DIblParamsUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D IBL params uniform");
        }
        ++statistics_.liveResources;

        // 3D-SKIN-001 A3: skinned vertex program shares fs_tina_opaque3d_mr.
        auto opaque3DSkinnedProgram = ShaderDetail::createOpaque3DSkinnedMrProgram();
        if (!opaque3DSkinnedProgram)
        {
            return Core::failure(std::move(opaque3DSkinnedProgram.error()));
        }
        opaque3DSkinnedProgram_ = *opaque3DSkinnedProgram;
        ++statistics_.liveResources;

        auto opaque3DMrVertexShader = ShaderDetail::createOpaque3DMrVertexShader();
        if (!opaque3DMrVertexShader)
        {
            return Core::failure(std::move(opaque3DMrVertexShader.error()));
        }
        opaque3DMrVertexShader_ = *opaque3DMrVertexShader;
        ++statistics_.liveResources;

        auto opaque3DSkinnedVertexShader = ShaderDetail::createOpaque3DSkinnedVertexShader();
        if (!opaque3DSkinnedVertexShader)
        {
            return Core::failure(std::move(opaque3DSkinnedVertexShader.error()));
        }
        opaque3DSkinnedVertexShader_ = *opaque3DSkinnedVertexShader;
        ++statistics_.liveResources;

        opaque3DSkinPaletteUniform_ = bgfx::createUniform(
            "u_tinaSkinPalette", bgfx::UniformType::Mat4,
            kOpaque3DSkinPaletteArrayJointCount);
        if (!bgfx::isValid(opaque3DSkinPaletteUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D skin palette uniform");
        }
        ++statistics_.liveResources;

        opaque3DSkinPaletteLastUniform_ =
            bgfx::createUniform("u_tinaSkinPaletteLast", bgfx::UniformType::Mat4);
        if (!bgfx::isValid(opaque3DSkinPaletteLastUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the final Opaque3D skin palette uniform");
        }
        ++statistics_.liveResources;

        opaque3DSkinColorUniform_ =
            bgfx::createUniform("u_tinaSkinColor", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DSkinColorUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D skin color uniform");
        }
        ++statistics_.liveResources;

        constexpr u64 IblSamplerFlags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP |
                                        BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
        opaque3DDefaultIblCube_ = bgfx::createTextureCube(
            1, false, 1, bgfx::TextureFormat::RGBA16F, IblSamplerFlags);
        if (!bgfx::isValid(opaque3DDefaultIblCube_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D default IBL cubemap");
        }
        constexpr std::array<u8, 8> BlackRgba16FloatPixel{};
        for (u8 face = 0; face < 6U; ++face)
        {
            bgfx::updateTextureCube(
                opaque3DDefaultIblCube_, 0, face, 0, 0, 0, 1, 1,
                bgfx::copy(BlackRgba16FloatPixel.data(),
                           static_cast<u32>(BlackRgba16FloatPixel.size())));
        }
        ++statistics_.liveResources;

        constexpr std::array<u8, 4> ZeroRg16FloatPixel{};
        opaque3DDefaultIblBrdfLut_ = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::RG16F,
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            bgfx::copy(ZeroRg16FloatPixel.data(),
                       static_cast<u32>(ZeroRg16FloatPixel.size())));
        if (!bgfx::isValid(opaque3DDefaultIblBrdfLut_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D default BRDF LUT");
        }
        ++statistics_.liveResources;

        // Default 1x1 white so baseColorFactor remains visible without a material bind.
        constexpr std::array<u8, 4> OpaqueWhitePixel{255, 255, 255, 255};
        const bgfx::Memory* opaqueWhiteMemory =
            bgfx::copy(OpaqueWhitePixel.data(), static_cast<u32>(OpaqueWhitePixel.size()));
        opaque3DDefaultTexture_ = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE, opaqueWhiteMemory);
        if (!bgfx::isValid(opaque3DDefaultTexture_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D default white texture");
        }
        ++statistics_.liveResources;

        // Default MR map: R=0, G=1 (roughness), B=0 (metallic), A=1 — dielectric matte.
        constexpr std::array<u8, 4> OpaqueDefaultMrPixel{0, 255, 0, 255};
        const bgfx::Memory* opaqueMrMemory =
            bgfx::copy(OpaqueDefaultMrPixel.data(), static_cast<u32>(OpaqueDefaultMrPixel.size()));
        opaque3DDefaultMrTexture_ = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE, opaqueMrMemory);
        if (!bgfx::isValid(opaque3DDefaultMrTexture_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D default metallic-roughness texture");
        }
        ++statistics_.liveResources;

        // Default normal map: flat +Z in tangent space (128,128,255).
        constexpr std::array<u8, 4> OpaqueDefaultNormalPixel{128, 128, 255, 255};
        const bgfx::Memory* opaqueNormalMemory =
            bgfx::copy(OpaqueDefaultNormalPixel.data(), static_cast<u32>(OpaqueDefaultNormalPixel.size()));
        opaque3DDefaultNormalTexture_ = bgfx::createTexture2D(
            1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE, opaqueNormalMemory);
        if (!bgfx::isValid(opaque3DDefaultNormalTexture_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D default normal texture");
        }
        ++statistics_.liveResources;

        const std::span<const BgfxOpaque3DVertex> vertices = canonicalCubeVertices();
        opaque3DVertexBuffer_ = bgfx::createVertexBuffer(
            bgfx::copy(vertices.data(), static_cast<u32>(vertices.size_bytes())), opaque3DVertexLayout_);
        if (!bgfx::isValid(opaque3DVertexBuffer_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Tina procedural Cube vertex buffer");
        }
        ++statistics_.liveResources;

        const std::span<const u16> indices = canonicalCubeIndices();
        opaque3DIndexBuffer_ =
            bgfx::createIndexBuffer(bgfx::copy(indices.data(), static_cast<u32>(indices.size_bytes())));
        if (!bgfx::isValid(opaque3DIndexBuffer_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Tina procedural Cube index buffer");
        }
        ++statistics_.liveResources;
        return Core::success();
    }

    [[nodiscard]] Core::Result<RenderFrameSubmission> submitFrame(const RenderFrame& frame) override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::submitFrame"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (frameOpen_)
        {
            return Core::failure(RenderErrorCode::FrameAlreadyOpen,
                                 "The previously submitted bgfx frame has not been presented");
        }
        if (frame.frameIndex != nextFrameIndex_)
        {
            return Core::failure(RenderErrorCode::UnexpectedFrameIndex,
                                 "Render frame indices must be contiguous and begin at zero");
        }
        // Fail closed rather than accept the frame and render none of the requested
        // post processing. The GPU implementation (offscreen framebuffers, HDR format
        // negotiation, the bloom mip chain, and the shader binding a CustomShader step
        // needs) is a separate slice; until it lands a caller must hear about it on the
        // first frame instead of wondering why nothing changed. The Null device
        // validates and executes the same contract headlessly today.
        if (frame.postProcess.enabled())
        {
            return Core::failure(
                RenderErrorCode::RenderTextureUnsupported,
                "The bgfx render device does not implement offscreen post processing yet");
        }
        if (auto status = validateSprite2DFrameResources(frame.primaryWorldScene, frame.resources); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateSprite2DShaderBindings(frame.primaryWorldScene, frame.resources);
            !status)
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
        if (auto status = validateOpaque3DFrameResources(frame.primaryWorldScene, frame.resources); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateMesh3DMaterialAlphaBindings(
                frame.primaryWorldScene, frame.resources);
            !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (auto status = validateMesh3DShaderBindings(frame.primaryWorldScene, frame.resources);
            !status)
        {
            return Core::failure(std::move(status.error()));
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
        if (auto status = validateFrameSurfaceBeforeCommit(frame); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        auto framePlan =
            BgfxSurfaceFramePlanner::planFrame(committedSurfaceState_, *frame.primaryWindowSurface, appliedBackbuffer_);
        if (!framePlan)
        {
            stopped_ = true;
            return Core::failure(std::move(framePlan.error()));
        }

        PreparedOpaque3D preparedOpaque3D{};
        PreparedSprite2D preparedSprite2D{};
        PreparedUIDisplayList preparedUI{};
        RenderPassSchedule passSchedule{};
        if (framePlan->shouldSubmit())
        {
            auto opaque3DPreflight =
                preflightOpaque3D(frame.primaryWorldScene, frame.resources,
                                  shadowMapExtents_.directionalCascadeTileExtent);
            if (!opaque3DPreflight)
            {
                return Core::failure(std::move(opaque3DPreflight.error()));
            }
            preparedOpaque3D = *opaque3DPreflight;

            if (auto status = validateSkinnedMesh3DBindings(frame.primaryWorldScene, frame.resources);
                !status)
            {
                return Core::failure(std::move(status.error()));
            }

            auto sprite2DPreflight = preflightSprite2D(frame.primaryWorldScene, frame.resources);
            if (!sprite2DPreflight)
            {
                return Core::failure(std::move(sprite2DPreflight.error()));
            }
            preparedSprite2D = *sprite2DPreflight;

            auto preflight = preflightUIDisplayList(frame.primaryWindowUIDisplayList, frame.resources);
            if (!preflight)
            {
                return Core::failure(std::move(preflight.error()));
            }
            preparedUI = *preflight;

            if (auto status = preflightUIImageBindings(frame.primaryWindowUIDisplayList, frame.resources);
                !status)
            {
                return Core::failure(std::move(status.error()));
            }

            if (auto status =
                    preflightTransientVertexPool(preparedOpaque3D, preparedSprite2D, preparedUI, transientByteLayout_);
                !status)
            {
                return Core::failure(std::move(status.error()));
            }
            if (auto status = preflightTransientIndexPool(preparedSprite2D, preparedUI); !status)
            {
                return Core::failure(std::move(status.error()));
            }
            auto scheduled = buildRenderPassSchedule(frame);
            if (!scheduled)
            {
                return Core::failure(std::move(scheduled.error()));
            }
            passSchedule = *scheduled;
            if (auto status = syncUIGlyphAtlas(frame.primaryWindowUIGlyphAtlas); !status)
            {
                return Core::failure(std::move(status.error()));
            }
            if (auto status = ensureShadowResources(preparedOpaque3D); !status)
            {
                return Core::failure(std::move(status.error()));
            }
        }
        if (auto status = surfaceStateTracker_.validateAndCommit(frame.primaryWindowSurface); !status)
        {
            return Core::failure(std::move(status.error()));
        }

        committedSurfaceState_ = *frame.primaryWindowSurface;
        ++nextFrameIndex_;

        // A replaced native window (Android background/resume) must be handed to bgfx
        // before anything is submitted against the old backbuffer. Device-scope
        // resources -- programs, textures, meshes, shadow atlases -- survive: only the
        // surface and swapchain are rebuilt, so no asset is re-uploaded (ADR 0034).
        if (surfaceStateTracker_.consumeNativeBindingChanged())
        {
            if (auto status = rebindNativeSurface(); !status)
            {
                return Core::failure(std::move(status.error()));
            }
        }

        if (!framePlan->shouldSubmit())
        {
            ++statistics_.skippedSuspendedSurfaceFrames;
            pumpRetirementOnlyFrameIfNeeded();
            return RenderFrameSubmission::SkippedSuspendedSurface();
        }

        if (framePlan->resetBackbuffer)
        {
            resetBackbuffer(framePlan->targetExtent);
            resetFlagsDirty_ = false;
        }
        else if (resetFlagsDirty_)
        {
            // A vsync toggle changes no geometry, so the surface frame planner
            // never asks for a reset. Re-apply the current extent so the new
            // flags reach the driver instead of waiting for the next resize.
            resetBackbuffer(appliedBackbuffer_);
            resetFlagsDirty_ = false;
        }

        submitPrimaryFrame(committedSurfaceState_, frame.primaryWorldScene, frame.resources, preparedOpaque3D,
                           preparedSprite2D, frame.primaryWindowUIDisplayList, preparedUI, passSchedule);
        frameOpen_ = true;
        ++statistics_.submitted;
        return RenderFrameSubmission::Submitted(nextSubmissionIndex_++);
    }

    [[nodiscard]] Core::Status present() override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::present"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (!frameOpen_)
        {
            return Core::failure(RenderErrorCode::NoFrameSubmitted,
                                 "A bgfx frame must be submitted before it can be presented");
        }

        // bgfx services a screenshot request right after it submits the frame that
        // carries it, so arming has to happen here, before this frame is dispatched.
        // Requesting it after bgfx::frame() would attach it to the next, empty frame.
        const bool capturingThisFrame = captureArmed_;
        if (capturingThisFrame)
        {
            captureArmed_ = false;
            captureCallback_.beginCapture();
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "tina-primary-frame");
        }

        submitRetirementMarkerIfNeeded();
        const u32 currentFrame = bgfx::frame();
        completeRetirementsThrough(currentFrame);
        frameOpen_ = false;
        ++statistics_.presented;

        if (capturingThisFrame)
        {
            awaitCaptureDelivery();
        }
        return Core::success();
    }

    [[nodiscard]] RenderStatistics statistics() const noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        return statistics_;
    }

    void setVsyncEnabled(bool enabled) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        const u32 next = enabled ? (resetFlags_ | BGFX_RESET_VSYNC)
                                 : (resetFlags_ & ~u32{BGFX_RESET_VSYNC});
        if (next == resetFlags_)
        {
            return;
        }
        resetFlags_ = next;
        // Deferred to the next submitted frame: bgfx::reset must not run between
        // a submitFrame and its present().
        resetFlagsDirty_ = true;
    }

    [[nodiscard]] bool vsyncEnabled() const noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        return (resetFlags_ & BGFX_RESET_VSYNC) != 0U;
    }

    [[nodiscard]] Core::Status drainGpuRetirements() noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::drainGpuRetirements"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (frameOpen_)
        {
            return Core::failure(RenderErrorCode::FrameAlreadyOpen,
                                 "Present the open frame before draining GPU retirements");
        }
        if (retirementTimeline_.pendingCount() == 0U)
        {
            return Core::success();
        }
        if (!retirementMarkerSupported_)
        {
            return Core::failure(RenderErrorCode::GpuRetirementUnsupported,
                                 "This bgfx backend can drain GPU retirements only during shutdown");
        }

        drainRetirementsWithMarkers();
        if (retirementTimeline_.pendingCount() != 0U)
        {
            return Core::failure(RenderErrorCode::GpuRetirementDrainFailed,
                                 "bgfx retirement marker did not complete within the drain budget");
        }
        return Core::success();
    }

    void shutdown() noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }

        stopped_ = true;
        frameOpen_ = false;

        if (bgfxInitialized_)
        {
            if (retirementMarkerSupported_ && retirementTimeline_.pendingCount() != 0U)
            {
                // Flushes an open frame without presenting and waits only for
                // readTexture-provided completion frames. A bounded failure
                // falls back to bgfx::shutdown as the final hard drain.
                drainRetirementsWithMarkers();
            }
            mesh3DBindings_.clear();
            mesh3DMaterialBindings_.clear();
            mesh3DImageBasedLighting_.reset();
            for (EnvironmentMapSlot& slot : environmentMaps_)
            {
                if (slot.live || slot.retirementPhase != RetirementPhase::None)
                {
                    destroyEnvironmentMapNativeResources(slot.resources);
                    slot.specularMipCount = 0;
                    if (slot.live)
                    {
                        slot.live = false;
                        slot.identity.advanceAfterRelease();
                    }
                }
            }
            for (MeshSlot& slot : meshes_)
            {
                if (slot.live || slot.retirementPhase != RetirementPhase::None)
                {
                    if (bgfx::isValid(slot.vertexBuffer))
                    {
                        bgfx::destroy(slot.vertexBuffer);
                        slot.vertexBuffer = BGFX_INVALID_HANDLE;
                        --statistics_.liveResources;
                    }
                    if (bgfx::isValid(slot.skinVertexBuffer))
                    {
                        bgfx::destroy(slot.skinVertexBuffer);
                        slot.skinVertexBuffer = BGFX_INVALID_HANDLE;
                        --statistics_.liveResources;
                    }
                    if (bgfx::isValid(slot.indexBuffer))
                    {
                        bgfx::destroy(slot.indexBuffer);
                        slot.indexBuffer = BGFX_INVALID_HANDLE;
                        --statistics_.liveResources;
                    }
                    if (slot.live)
                    {
                        slot.live = false;
                        slot.identity.advanceAfterRelease();
                    }
                }
            }
            if (bgfx::isValid(opaque3DIndexBuffer_))
            {
                bgfx::destroy(opaque3DIndexBuffer_);
                opaque3DIndexBuffer_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DVertexBuffer_))
            {
                bgfx::destroy(opaque3DVertexBuffer_);
                opaque3DVertexBuffer_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            const u64 cascadedDirectionalShadowResourceCount =
                static_cast<u64>(bgfx::isValid(cascadedDirectionalShadowResources_.frameBuffer)) +
                static_cast<u64>(bgfx::isValid(cascadedDirectionalShadowResources_.depthAtlas));
            destroyCascadedDirectionalShadowResources(cascadedDirectionalShadowResources_);
            statistics_.liveResources -= cascadedDirectionalShadowResourceCount;
            const u64 spotLightShadowResourceCount =
                static_cast<u64>(bgfx::isValid(spotLightShadowResources_.frameBuffer)) +
                static_cast<u64>(bgfx::isValid(spotLightShadowResources_.depthMap));
            destroySpotLightShadowResources(spotLightShadowResources_);
            statistics_.liveResources -= spotLightShadowResourceCount;
            u64 pointLightShadowResourceCount = 0;
            for (const bgfx::FrameBufferHandle frameBuffer :
                 pointLightShadowResources_.frameBuffers)
            {
                pointLightShadowResourceCount +=
                    static_cast<u64>(bgfx::isValid(frameBuffer));
            }
            for (const bgfx::TextureHandle depthMap : pointLightShadowResources_.depthMaps)
            {
                pointLightShadowResourceCount +=
                    static_cast<u64>(bgfx::isValid(depthMap));
            }
            destroyPointLightShadowResources(pointLightShadowResources_);
            statistics_.liveResources -= pointLightShadowResourceCount;
            if (bgfx::isValid(opaque3DDefaultShadowTexture_))
            {
                bgfx::destroy(opaque3DDefaultShadowTexture_);
                opaque3DDefaultShadowTexture_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            for (bgfx::UniformHandle& sampler : opaque3DPointShadowMapSamplers_)
            {
                if (bgfx::isValid(sampler))
                {
                    bgfx::destroy(sampler);
                    sampler = BGFX_INVALID_HANDLE;
                    --statistics_.liveResources;
                }
            }
            if (bgfx::isValid(opaque3DPointShadowMatricesUniform_))
            {
                bgfx::destroy(opaque3DPointShadowMatricesUniform_);
                opaque3DPointShadowMatricesUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DPointShadowParamsUniform_))
            {
                bgfx::destroy(opaque3DPointShadowParamsUniform_);
                opaque3DPointShadowParamsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSpotShadowMapSampler_))
            {
                bgfx::destroy(opaque3DSpotShadowMapSampler_);
                opaque3DSpotShadowMapSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSpotShadowMatrixUniform_))
            {
                bgfx::destroy(opaque3DSpotShadowMatrixUniform_);
                opaque3DSpotShadowMatrixUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSpotShadowParamsUniform_))
            {
                bgfx::destroy(opaque3DSpotShadowParamsUniform_);
                opaque3DSpotShadowParamsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DCsmDepthProgram_))
            {
                bgfx::destroy(opaque3DCsmDepthProgram_);
                opaque3DCsmDepthProgram_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DCsmAtlasSampler_))
            {
                bgfx::destroy(opaque3DCsmAtlasSampler_);
                opaque3DCsmAtlasSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DCsmMatricesUniform_))
            {
                bgfx::destroy(opaque3DCsmMatricesUniform_);
                opaque3DCsmMatricesUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DCsmSplitDepthsUniform_))
            {
                bgfx::destroy(opaque3DCsmSplitDepthsUniform_);
                opaque3DCsmSplitDepthsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DCsmParamsUniform_))
            {
                bgfx::destroy(opaque3DCsmParamsUniform_);
                opaque3DCsmParamsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DIblDiffuseSampler_))
            {
                bgfx::destroy(opaque3DIblDiffuseSampler_);
                opaque3DIblDiffuseSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DIblSpecularSampler_))
            {
                bgfx::destroy(opaque3DIblSpecularSampler_);
                opaque3DIblSpecularSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DIblBrdfSampler_))
            {
                bgfx::destroy(opaque3DIblBrdfSampler_);
                opaque3DIblBrdfSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DIblParamsUniform_))
            {
                bgfx::destroy(opaque3DIblParamsUniform_);
                opaque3DIblParamsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DProgram_))
            {
                bgfx::destroy(opaque3DProgram_);
                opaque3DProgram_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSkinnedProgram_))
            {
                bgfx::destroy(opaque3DSkinnedProgram_);
                opaque3DSkinnedProgram_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSkinPaletteUniform_))
            {
                bgfx::destroy(opaque3DSkinPaletteUniform_);
                opaque3DSkinPaletteUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSkinPaletteLastUniform_))
            {
                bgfx::destroy(opaque3DSkinPaletteLastUniform_);
                opaque3DSkinPaletteLastUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSkinColorUniform_))
            {
                bgfx::destroy(opaque3DSkinColorUniform_);
                opaque3DSkinColorUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSampler_))
            {
                bgfx::destroy(opaque3DSampler_);
                opaque3DSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DMrSampler_))
            {
                bgfx::destroy(opaque3DMrSampler_);
                opaque3DMrSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DLightDirectionsUniform_))
            {
                bgfx::destroy(opaque3DLightDirectionsUniform_);
                opaque3DLightDirectionsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DLightColorsUniform_))
            {
                bgfx::destroy(opaque3DLightColorsUniform_);
                opaque3DLightColorsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DPointLightPositionsUniform_))
            {
                bgfx::destroy(opaque3DPointLightPositionsUniform_);
                opaque3DPointLightPositionsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DPointLightColorsUniform_))
            {
                bgfx::destroy(opaque3DPointLightColorsUniform_);
                opaque3DPointLightColorsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSpotLightPositionsUniform_))
            {
                bgfx::destroy(opaque3DSpotLightPositionsUniform_);
                opaque3DSpotLightPositionsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSpotLightDirectionsUniform_))
            {
                bgfx::destroy(opaque3DSpotLightDirectionsUniform_);
                opaque3DSpotLightDirectionsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSpotLightColorsUniform_))
            {
                bgfx::destroy(opaque3DSpotLightColorsUniform_);
                opaque3DSpotLightColorsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DMrParamsUniform_))
            {
                bgfx::destroy(opaque3DMrParamsUniform_);
                opaque3DMrParamsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DEmissiveFactorUniform_))
            {
                bgfx::destroy(opaque3DEmissiveFactorUniform_);
                opaque3DEmissiveFactorUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DNormalSampler_))
            {
                bgfx::destroy(opaque3DNormalSampler_);
                opaque3DNormalSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DNormalParamsUniform_))
            {
                bgfx::destroy(opaque3DNormalParamsUniform_);
                opaque3DNormalParamsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DDefaultTexture_))
            {
                bgfx::destroy(opaque3DDefaultTexture_);
                opaque3DDefaultTexture_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DDefaultMrTexture_))
            {
                bgfx::destroy(opaque3DDefaultMrTexture_);
                opaque3DDefaultMrTexture_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DDefaultNormalTexture_))
            {
                bgfx::destroy(opaque3DDefaultNormalTexture_);
                opaque3DDefaultNormalTexture_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DDefaultIblCube_))
            {
                bgfx::destroy(opaque3DDefaultIblCube_);
                opaque3DDefaultIblCube_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DDefaultIblBrdfLut_))
            {
                bgfx::destroy(opaque3DDefaultIblBrdfLut_);
                opaque3DDefaultIblBrdfLut_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            mesh3DMaterialBindings_.clear();
            if (bgfx::isValid(sprite2DProgram_))
            {
                bgfx::destroy(sprite2DProgram_);
                sprite2DProgram_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(sprite2DVertexShader_))
            {
                bgfx::destroy(sprite2DVertexShader_);
                sprite2DVertexShader_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DMrVertexShader_))
            {
                bgfx::destroy(opaque3DMrVertexShader_);
                opaque3DMrVertexShader_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DSkinnedVertexShader_))
            {
                bgfx::destroy(opaque3DSkinnedVertexShader_);
                opaque3DSkinnedVertexShader_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(sprite2DDefaultTexture_))
            {
                bgfx::destroy(sprite2DDefaultTexture_);
                sprite2DDefaultTexture_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(sprite2DDefaultNormalTexture_))
            {
                bgfx::destroy(sprite2DDefaultNormalTexture_);
                sprite2DDefaultNormalTexture_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(sprite2DSampler_))
            {
                bgfx::destroy(sprite2DSampler_);
                sprite2DSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(sprite2DNormalSampler_))
            {
                bgfx::destroy(sprite2DNormalSampler_);
                sprite2DNormalSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(sprite2DLightPositionsUniform_))
            {
                bgfx::destroy(sprite2DLightPositionsUniform_);
                sprite2DLightPositionsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(sprite2DLightColorsUniform_))
            {
                bgfx::destroy(sprite2DLightColorsUniform_);
                sprite2DLightColorsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(sprite2DLightParamsUniform_))
            {
                bgfx::destroy(sprite2DLightParamsUniform_);
                sprite2DLightParamsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(sprite2DNormalParamsUniform_))
            {
                bgfx::destroy(sprite2DNormalParamsUniform_);
                sprite2DNormalParamsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(sprite2DShadowSegmentsUniform_))
            {
                bgfx::destroy(sprite2DShadowSegmentsUniform_);
                sprite2DShadowSegmentsUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            for (TextureSlot& slot : textures_)
            {
                if ((slot.live || slot.retirementPhase != RetirementPhase::None) &&
                    bgfx::isValid(slot.handle))
                {
                    bgfx::destroy(slot.handle);
                    slot.handle = BGFX_INVALID_HANDLE;
                    --statistics_.liveResources;
                }
                if (slot.live)
                {
                    slot.live = false;
                    slot.identity.advanceAfterRelease();
                }
            }
            texture2DBindings_.clear();
            for (ShaderSlot& slot : shaders_)
            {
                const bool occupied = slot.live || slot.retirementPhase != RetirementPhase::None;
                if (occupied && bgfx::isValid(slot.skinnedProgram))
                {
                    // Not counted separately in liveResources: one upload is one resource, and the
                    // skinned program is a second link of the same fragment binary.
                    bgfx::destroy(slot.skinnedProgram);
                    slot.skinnedProgram = BGFX_INVALID_HANDLE;
                }
                if (occupied && bgfx::isValid(slot.program))
                {
                    bgfx::destroy(slot.program);
                    slot.program = BGFX_INVALID_HANDLE;
                    --statistics_.liveResources;
                }
                if (slot.live)
                {
                    slot.live = false;
                    slot.identity.advanceAfterRelease();
                }
            }
            shaderBindings_.clear();
            shaderUniformBindings_.clear();
            if (bgfx::isValid(uiCoverageProgram_))
            {
                bgfx::destroy(uiCoverageProgram_);
                uiCoverageProgram_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(uiImageQuadProgram_))
            {
                bgfx::destroy(uiImageQuadProgram_);
                uiImageQuadProgram_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(uiSolidWhiteTexture_))
            {
                bgfx::destroy(uiSolidWhiteTexture_);
                uiSolidWhiteTexture_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(uiGlyphAtlasTexture_))
            {
                bgfx::destroy(uiGlyphAtlasTexture_);
                uiGlyphAtlasTexture_ = BGFX_INVALID_HANDLE;
                uiGlyphAtlasPageSize_ = {};
                uiGlyphAtlasUploadedPageRevision_ = 0;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(uiTexColorUniform_))
            {
                bgfx::destroy(uiTexColorUniform_);
                uiTexColorUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(retirementMarkerReadback_))
            {
                bgfx::destroy(retirementMarkerReadback_);
                retirementMarkerReadback_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(retirementMarkerSource_))
            {
                bgfx::destroy(retirementMarkerSource_);
                retirementMarkerSource_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            // Device-owned init resources (must match ++ in initialize):
            // uiCoverageProgram, uiSolidWhiteTexture, uiTexColorUniform,
            // sprite2DProgram, base/normal samplers, lighting/normal uniforms,
            // sprite2DDefaultTexture, sprite2DDefaultNormalTexture,
            // opaque3DProgram/shadowProgram, material/shadow/IBL samplers and uniforms,
            // the shadow sampler fallback plus any lazily created directional/spot/point
            // shadow depth textures/framebuffers, opaque3DMrSampler,
            // opaque3D directional/point/spot light arrays + MrParams uniforms, opaque3DDefaultTexture,
            // opaque3DDefaultMr/Normal/IBL textures, opaque3DVertexBuffer, opaque3DIndexBuffer,
            // retirement marker source/readback (+ dynamic mesh/texture/environment slots).
            if (statistics_.liveResources != 0)
            {
                char path[512]{};
                const char* temp = std::getenv("TEMP");
                if (temp == nullptr || temp[0] == '\0')
                {
                    temp = std::getenv("TMP");
                }
                if (temp == nullptr || temp[0] == '\0')
                {
                    temp = ".";
                }
                std::snprintf(path, sizeof(path), "%s\\tina_bgfx_ledger_imbalance.txt", temp);
                if (std::FILE* file = std::fopen(path, "wb"))
                {
                    std::fprintf(file, "BgfxRenderDevice liveResources imbalance at shutdown: %llu\n",
                                 static_cast<unsigned long long>(statistics_.liveResources));
                    std::fflush(file);
                    std::fclose(file);
                }
                std::fprintf(stderr, "BgfxRenderDevice liveResources imbalance at shutdown: %llu\n",
                             static_cast<unsigned long long>(statistics_.liveResources));
                std::fflush(stderr);
                std::_Exit(3);
            }
            bgfx::shutdown();
            bgfxInitialized_ = false;

            // When marker support is unavailable (or its bounded drain failed),
            // bgfx::shutdown is the final hard completion point. External pins
            // are therefore released only after shutdown returns.
            u64 shutdownCompleted = 0;
            for (TextureSlot& slot : textures_)
            {
                if (slot.retirementPhase != RetirementPhase::None)
                {
                    slot.completionPin.release();
                    slot.retirementPhase = RetirementPhase::None;
                    ++shutdownCompleted;
                }
            }
            for (MeshSlot& slot : meshes_)
            {
                if (slot.retirementPhase != RetirementPhase::None)
                {
                    slot.completionPin.release();
                    slot.retirementPhase = RetirementPhase::None;
                    ++shutdownCompleted;
                }
            }
            for (ShaderSlot& slot : shaders_)
            {
                if (slot.retirementPhase != RetirementPhase::None)
                {
                    slot.completionPin.release();
                    slot.retirementPhase = RetirementPhase::None;
                    ++shutdownCompleted;
                }
            }
            for (EnvironmentMapSlot& slot : environmentMaps_)
            {
                if (slot.retirementPhase != RetirementPhase::None)
                {
                    slot.completionPin.release();
                    slot.retirementPhase = RetirementPhase::None;
                    ++shutdownCompleted;
                }
            }
            if (shutdownCompleted > statistics_.pendingGpuRetirements)
            {
                std::terminate();
            }
            statistics_.pendingGpuRetirements -= shutdownCompleted;
            statistics_.completedGpuRetirements += shutdownCompleted;
            retirementTimeline_.reset();
            retirementMarkerSupported_ = false;
            textures_.clear();
            shaders_.clear();
            meshes_.clear();
            environmentMaps_.clear();
        }

        lease_ = Integration::NativeWindowSurfaceLease{};
    }

  private:
    [[nodiscard]] Core::Status validateApiThread(std::string_view operation) const
    {
        if (std::this_thread::get_id() == ownerThread_)
        {
            return Core::success();
        }

        Core::Error error{RenderErrorCode::WrongOwnerThread, "The bgfx render device must be used on its owner thread"};
        error.addContext(operation);
        return Core::failure(std::move(error));
    }

    [[nodiscard]] Core::Status validateFrameSurfaceBeforeCommit(const RenderFrame& frame) const
    {
        if (!frame.primaryWindowSurface.has_value())
        {
            return Core::failure(RenderErrorCode::InvalidSurfaceState,
                                 "The bgfx render device requires a primary window surface");
        }
        if (frame.primaryWindowSurface->surface != committedSurfaceState_.surface)
        {
            return Core::failure(RenderErrorCode::InvalidSurfaceState,
                                 "The bgfx primary surface identity changed after device creation");
        }
        return Core::success();
    }

    void resetBackbuffer(RenderSurfaceExtent extent) noexcept
    {
        bgfx::reset(extent.width, extent.height, resetFlags_);
        appliedBackbuffer_ = extent;
    }

    // Points bgfx at the surface's current native window and rebuilds the backbuffer.
    //
    // setPlatformData() is documented "must be called before bgfx::init", but its
    // implementation only forbids changing the display type and context after init
    // (bgfx.cpp:448-458) -- the native window handle is explicitly allowed, and
    // renderer_vk.cpp:7622-7626 recreates the surface when it observes a different nwh
    // on the following reset. This is the same path bgfx's own Android example relies
    // on, and it is why a rebind does not need bgfx::shutdown().
    [[nodiscard]] Core::Status rebindNativeSurface()
    {
        auto binding = decodeNativeWindowBinding(lease_);
        if (!binding)
        {
            return Core::failure(std::move(binding.error()));
        }
        bgfx::setPlatformData(binding->platformData);
        // Forced because the extent may be unchanged across the rebind: without this a
        // same-size resume would skip the reset that actually rebuilds the swapchain.
        const RenderSurfaceExtent extent =
            BgfxSurfaceFramePlanner::bootstrapBackbufferExtent(committedSurfaceState_);
        resetBackbuffer(extent);
        resetFlagsDirty_ = false;
        ++statistics_.nativeSurfaceRebinds;
        return Core::success();
    }

    void submitRetirementMarkerIfNeeded() noexcept
    {
        if (!retirementMarkerSupported_ || !retirementTimeline_.needsMarker())
        {
            return;
        }

        bgfx::setViewMode(kRetirementMarkerView, bgfx::ViewMode::Sequential);
        bgfx::blit(kRetirementMarkerView, retirementMarkerReadback_, 0, 0,
                   retirementMarkerSource_, 0, 0, 1, 1);
        const u32 readyFrame = bgfx::readTexture(retirementMarkerReadback_,
                                                 retirementMarkerBytes_.data());
        if (!retirementTimeline_.beginMarker(readyFrame))
        {
            std::terminate();
        }

        u32 waitingCount = 0;
        for (TextureSlot& slot : textures_)
        {
            if (slot.retirementPhase == RetirementPhase::Queued)
            {
                slot.retirementPhase = RetirementPhase::Waiting;
                ++waitingCount;
            }
        }
        for (MeshSlot& slot : meshes_)
        {
            if (slot.retirementPhase == RetirementPhase::Queued)
            {
                slot.retirementPhase = RetirementPhase::Waiting;
                ++waitingCount;
            }
        }
        for (ShaderSlot& slot : shaders_)
        {
            if (slot.retirementPhase == RetirementPhase::Queued)
            {
                slot.retirementPhase = RetirementPhase::Waiting;
                ++waitingCount;
            }
        }
        for (EnvironmentMapSlot& slot : environmentMaps_)
        {
            if (slot.retirementPhase == RetirementPhase::Queued)
            {
                slot.retirementPhase = RetirementPhase::Waiting;
                ++waitingCount;
            }
        }
        if (waitingCount != retirementTimeline_.waitingCount())
        {
            std::terminate();
        }
    }

    void completeRetirementsThrough(u32 currentFrame) noexcept
    {
        const u32 expected = retirementTimeline_.completeThrough(currentFrame);
        if (expected == 0U)
        {
            return;
        }

        u32 completed = 0;
        for (TextureSlot& slot : textures_)
        {
            if (slot.retirementPhase != RetirementPhase::Waiting)
            {
                continue;
            }
            if (bgfx::isValid(slot.handle))
            {
                bgfx::destroy(slot.handle);
                slot.handle = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            slot.width = 0;
            slot.height = 0;
            slot.retirementPhase = RetirementPhase::None;
            slot.completionPin.release();
            ++completed;
        }
        for (MeshSlot& slot : meshes_)
        {
            if (slot.retirementPhase != RetirementPhase::Waiting)
            {
                continue;
            }
            if (bgfx::isValid(slot.vertexBuffer))
            {
                bgfx::destroy(slot.vertexBuffer);
                slot.vertexBuffer = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(slot.skinVertexBuffer))
            {
                bgfx::destroy(slot.skinVertexBuffer);
                slot.skinVertexBuffer = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(slot.indexBuffer))
            {
                bgfx::destroy(slot.indexBuffer);
                slot.indexBuffer = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            slot.vertexCount = 0;
            slot.indexCount = 0;
            slot.jointCount = 0;
            slot.skinned = false;
            slot.retirementPhase = RetirementPhase::None;
            slot.completionPin.release();
            ++completed;
        }
        for (ShaderSlot& slot : shaders_)
        {
            if (slot.retirementPhase != RetirementPhase::Waiting)
            {
                continue;
            }
            if (bgfx::isValid(slot.skinnedProgram))
            {
                bgfx::destroy(slot.skinnedProgram);
                slot.skinnedProgram = BGFX_INVALID_HANDLE;
            }
            if (bgfx::isValid(slot.program))
            {
                bgfx::destroy(slot.program);
                slot.program = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            slot.shaderKind = GpuShaderKind::Invalid;
            slot.authorUniforms.clear();
            slot.authorUniformsRevision = 0;
            slot.retirementPhase = RetirementPhase::None;
            slot.completionPin.release();
            ++completed;
        }
        for (EnvironmentMapSlot& slot : environmentMaps_)
        {
            if (slot.retirementPhase != RetirementPhase::Waiting)
            {
                continue;
            }
            destroyEnvironmentMapNativeResources(slot.resources);
            slot.specularMipCount = 0;
            slot.retirementPhase = RetirementPhase::None;
            slot.completionPin.release();
            ++completed;
        }
        if (completed != expected || statistics_.pendingGpuRetirements < completed)
        {
            std::terminate();
        }
        statistics_.pendingGpuRetirements -= completed;
        statistics_.completedGpuRetirements += completed;
    }

    void pumpRetirementOnlyFrameIfNeeded() noexcept
    {
        if (!retirementMarkerSupported_ || retirementTimeline_.pendingCount() == 0U)
        {
            return;
        }
        submitRetirementMarkerIfNeeded();
        const u32 currentFrame = bgfx::frame(BGFX_FRAME_FLUSH);
        completeRetirementsThrough(currentFrame);
    }

    void drainRetirementsWithMarkers() noexcept
    {
        constexpr u32 MaximumDrainFrames = 16;
        for (u32 drainFrame = 0;
             drainFrame < MaximumDrainFrames && retirementTimeline_.pendingCount() != 0U;
             ++drainFrame)
        {
            submitRetirementMarkerIfNeeded();
            const u32 currentFrame = bgfx::frame(BGFX_FRAME_FLUSH);
            completeRetirementsThrough(currentFrame);
        }
    }

    [[nodiscard]] Core::Status ensureShadowResources(
        const PreparedOpaque3D& prepared)
    {
        const bool directionalValid =
            cascadedDirectionalShadowResources_.valid();
        const bool directionalHasAnyHandle =
            bgfx::isValid(cascadedDirectionalShadowResources_.frameBuffer) ||
            bgfx::isValid(cascadedDirectionalShadowResources_.depthAtlas);
        const bool spotValid = spotLightShadowResources_.valid();
        const bool spotHasAnyHandle =
            bgfx::isValid(spotLightShadowResources_.frameBuffer) ||
            bgfx::isValid(spotLightShadowResources_.depthMap);
        bool pointHasAnyHandle = false;
        for (const bgfx::FrameBufferHandle frameBuffer :
             pointLightShadowResources_.frameBuffers)
        {
            pointHasAnyHandle = pointHasAnyHandle || bgfx::isValid(frameBuffer);
        }
        for (const bgfx::TextureHandle depthMap :
             pointLightShadowResources_.depthMaps)
        {
            pointHasAnyHandle = pointHasAnyHandle || bgfx::isValid(depthMap);
        }
        const bool pointValid = pointLightShadowResources_.valid();
        if ((directionalHasAnyHandle && !directionalValid) ||
            (spotHasAnyHandle && !spotValid) ||
            (pointHasAnyHandle && !pointValid))
        {
            std::terminate();
        }

        const bool createDirectional =
            prepared.cascadedDirectionalShadow.has_value() && !directionalValid;
        const bool createSpot = prepared.spotLightShadow.has_value() && !spotValid;
        const bool createPoint = prepared.pointLightShadow.has_value() && !pointValid;
        BgfxCascadedDirectionalShadowResources newDirectional{};
        BgfxSpotLightShadowResources newSpot{};
        BgfxPointLightShadowResources newPoint{};
        const auto rollback = [&]() noexcept {
            destroyCascadedDirectionalShadowResources(newDirectional);
            destroySpotLightShadowResources(newSpot);
            destroyPointLightShadowResources(newPoint);
        };

        if (createDirectional)
        {
            auto resources = createCascadedDirectionalShadowResources(
                shadowMapExtents_.directionalCascadeTileExtent);
            if (!resources)
            {
                rollback();
                return Core::failure(std::move(resources.error()));
            }
            newDirectional = *resources;
        }
        if (createSpot)
        {
            auto resources =
                createSpotLightShadowResources(shadowMapExtents_.spotLightMapExtent);
            if (!resources)
            {
                rollback();
                return Core::failure(std::move(resources.error()));
            }
            newSpot = *resources;
        }
        if (createPoint)
        {
            auto resources = createPointLightShadowResources(
                shadowMapExtents_.pointLightFaceExtent);
            if (!resources)
            {
                rollback();
                return Core::failure(std::move(resources.error()));
            }
            newPoint = *resources;
        }

        if (createDirectional)
        {
            cascadedDirectionalShadowResources_ = newDirectional;
            statistics_.liveResources += 2U;
        }
        if (createSpot)
        {
            spotLightShadowResources_ = newSpot;
            statistics_.liveResources += 2U;
        }
        if (createPoint)
        {
            pointLightShadowResources_ = newPoint;
            statistics_.liveResources +=
                static_cast<u64>(BgfxPointLightShadowFaceCount * 2U);
        }
        return Core::success();
    }

    void configureSurfaceClearView(const RenderSurfaceState& surface, u32 clearRgba) noexcept
    {
        bgfx::setViewRect(kSurfaceClearView, 0, 0, static_cast<u16>(surface.framebufferExtent.width),
                          static_cast<u16>(surface.framebufferExtent.height));
        bgfx::setViewClear(kSurfaceClearView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearRgba, 1.0F, 0);
        bgfx::setViewMode(kSurfaceClearView, bgfx::ViewMode::Sequential);
        bgfx::touch(kSurfaceClearView);
    }

    void configureCascadedDirectionalShadowView(
        const PreparedOpaque3D::CascadedDirectionalShadow& shadow,
        usize cascadeIndex,
        bool clearDepth) noexcept
    {
        if (cascadeIndex >= BgfxCascadedDirectionalShadowCascadeCount ||
            !cascadedDirectionalShadowResources_.valid())
        {
            std::terminate();
        }
        const bgfx::ViewId view = kCascadedDirectionalShadowViews[cascadeIndex];
        const u16 tileX = static_cast<u16>((cascadeIndex % 2U) *
                                           shadowMapExtents_.directionalCascadeTileExtent);
        const u16 tileY = static_cast<u16>((cascadeIndex / 2U) *
                                           shadowMapExtents_.directionalCascadeTileExtent);
        const BgfxCascadedDirectionalShadowCascade& cascade =
            shadow.projection.cascades[cascadeIndex];
        bgfx::setViewRect(view, tileX, tileY,
                          shadowMapExtents_.directionalCascadeTileExtent,
                          shadowMapExtents_.directionalCascadeTileExtent);
        bgfx::setViewFrameBuffer(view,
                                 cascadedDirectionalShadowResources_.frameBuffer);
        bgfx::setViewClear(view,
                           clearDepth ? BGFX_CLEAR_DEPTH : BGFX_CLEAR_NONE,
                           0, 1.0F, 0);
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(view, cascade.lightView.data(),
                               cascade.lightProjection.data());
        bgfx::touch(view);
    }

    void configureSpotLightShadowView(
        const PreparedOpaque3D::SpotLightShadow& shadow,
        bool clearDepth) noexcept
    {
        if (!spotLightShadowResources_.valid())
        {
            std::terminate();
        }
        bgfx::setViewRect(kSpotLightShadowView, 0, 0,
                          shadowMapExtents_.spotLightMapExtent,
                          shadowMapExtents_.spotLightMapExtent);
        bgfx::setViewFrameBuffer(kSpotLightShadowView,
                                 spotLightShadowResources_.frameBuffer);
        bgfx::setViewClear(kSpotLightShadowView,
                           clearDepth ? BGFX_CLEAR_DEPTH : BGFX_CLEAR_NONE,
                           0, 1.0F, 0);
        bgfx::setViewMode(kSpotLightShadowView, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(kSpotLightShadowView,
                               shadow.projection.lightView.data(),
                               shadow.projection.lightProjection.data());
        bgfx::touch(kSpotLightShadowView);
    }

    void configurePointLightShadowView(
        const PreparedOpaque3D::PointLightShadow& shadow,
        usize faceIndex,
        bool clearDepth) noexcept
    {
        if (faceIndex >= BgfxPointLightShadowFaceCount)
        {
            std::terminate();
        }
        if (!pointLightShadowResources_.valid())
        {
            std::terminate();
        }
        const bgfx::ViewId view = kPointLightShadowViews[faceIndex];
        const BgfxPointLightShadowFace& face = shadow.projection.faces[faceIndex];
        bgfx::setViewRect(view, 0, 0, shadowMapExtents_.pointLightFaceExtent,
                          shadowMapExtents_.pointLightFaceExtent);
        bgfx::setViewFrameBuffer(view, pointLightShadowResources_.frameBuffers[faceIndex]);
        bgfx::setViewClear(view, clearDepth ? BGFX_CLEAR_DEPTH : BGFX_CLEAR_NONE,
                           0, 1.0F, 0);
        bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(view, face.lightView.data(), face.lightProjection.data());
        bgfx::touch(view);
    }

    void configureMesh3DView(bgfx::ViewId viewId, const RenderSurfaceState& surface,
                             const RenderPerspectiveCamera& camera,
                             bool clearColor, bool clearDepth, u32 clearRgba) noexcept
    {
        const BgfxViewRect rect = viewportRect(surface, camera.normalizedViewport);
        if (rect.width == 0 || rect.height == 0)
        {
            std::terminate();
        }
        bgfx::setViewRect(viewId, rect.x, rect.y, rect.width, rect.height);
        const u16 clearFlags = static_cast<u16>((clearColor ? BGFX_CLEAR_COLOR : 0U) |
                                                (clearDepth ? BGFX_CLEAR_DEPTH : 0U));
        bgfx::setViewClear(viewId, clearFlags, clearRgba, 1.0F, 0);
        bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

        const bx::Vec3 eye{camera.positionX, camera.positionY, camera.positionZ};
        const bx::Vec3 target{
            camera.positionX + camera.forwardX,
            camera.positionY + camera.forwardY,
            camera.positionZ + camera.forwardZ,
        };
        const bx::Vec3 up{camera.upX, camera.upY, camera.upZ};
        float viewMatrix[16]{};
        bx::mtxLookAt(viewMatrix, eye, target, up, bx::Handedness::Right);

        float projection[16]{};
        const bgfx::Caps* const caps = bgfx::getCaps();
        bx::mtxProj(projection, camera.verticalFovDegrees, camera.aspectRatio, camera.nearPlaneMeters,
                    camera.farPlaneMeters, caps != nullptr && caps->homogeneousDepth, bx::Handedness::Right);
        bgfx::setViewTransform(viewId, viewMatrix, projection);
        bgfx::touch(viewId);
    }

    void configureSprite2DView(const RenderSurfaceState& surface, const RenderCamera2D& camera,
                               bool clearColor, bool clearDepth, u32 clearRgba) noexcept
    {
        const BgfxViewRect rect = viewportRect(surface, camera.normalizedViewport);
        if (rect.width == 0 || rect.height == 0)
        {
            std::terminate();
        }
        bgfx::setViewRect(kSprite2DView, rect.x, rect.y, rect.width, rect.height);
        const u16 clearFlags = static_cast<u16>((clearColor ? BGFX_CLEAR_COLOR : 0U) |
                                                (clearDepth ? BGFX_CLEAR_DEPTH : 0U));
        bgfx::setViewClear(kSprite2DView, clearFlags, clearRgba, 1.0F, 0);
        bgfx::setViewMode(kSprite2DView, bgfx::ViewMode::Sequential);

        const float cosine = std::cos(camera.rotationRadians);
        const float sine = std::sin(camera.rotationRadians);
        const std::array<float, 16> view{
            cosine,
            -sine,
            0.0F,
            0.0F,
            sine,
            cosine,
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            1.0F,
            0.0F,
            -cosine * camera.centerX - sine * camera.centerY,
            sine * camera.centerX - cosine * camera.centerY,
            0.0F,
            1.0F,
        };

        float projection[16]{};
        const bgfx::Caps* const caps = bgfx::getCaps();
        bx::mtxOrtho(projection, -camera.worldWidth * 0.5F, camera.worldWidth * 0.5F, -camera.worldHeight * 0.5F,
                     camera.worldHeight * 0.5F, -1.0F, 1.0F, 0.0F, caps != nullptr && caps->homogeneousDepth);
        bgfx::setViewTransform(kSprite2DView, view.data(), projection);
        bgfx::touch(kSprite2DView);
    }

    void configureUIView(const RenderSurfaceState& surface, bool clearColor, bool clearDepth,
                         u32 clearRgba) noexcept
    {
        bgfx::setViewRect(kUIView, 0, 0, static_cast<u16>(surface.framebufferExtent.width),
                          static_cast<u16>(surface.framebufferExtent.height));
        const u16 clearFlags = static_cast<u16>((clearColor ? BGFX_CLEAR_COLOR : 0U) |
                                                (clearDepth ? BGFX_CLEAR_DEPTH : 0U));
        bgfx::setViewClear(kUIView, clearFlags, clearRgba, 1.0F, 0);
        bgfx::setViewMode(kUIView, bgfx::ViewMode::Sequential);

        float projection[16]{};
        const bgfx::Caps* const caps = bgfx::getCaps();
        bx::mtxOrtho(projection, 0.0F, static_cast<float>(surface.framebufferExtent.width),
                     static_cast<float>(surface.framebufferExtent.height), 0.0F, 0.0F, 1000.0F, 0.0F,
                     caps != nullptr && caps->homogeneousDepth);
        bgfx::setViewTransform(kUIView, nullptr, projection);
        bgfx::touch(kUIView);
    }

    struct ResolvedOpaque3DGeometry final {
        bgfx::VertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
    };

    [[nodiscard]] std::optional<ResolvedOpaque3DGeometry>
    resolveOpaque3DGeometry(u32 meshKey) const noexcept
    {
        ResolvedOpaque3DGeometry geometry{
            .vertexBuffer = opaque3DVertexBuffer_,
            .indexBuffer = opaque3DIndexBuffer_,
        };
        if (const auto binding = mesh3DBindings_.find(meshKey);
            binding != mesh3DBindings_.end())
        {
            const GpuMeshId id = binding->second;
            if (id.index < meshes_.size())
            {
                const MeshSlot& slot = meshes_[id.index];
                if (slot.live && slot.identity.value() == id.generation &&
                    bgfx::isValid(slot.vertexBuffer) && bgfx::isValid(slot.indexBuffer))
                {
                    geometry.vertexBuffer = slot.vertexBuffer;
                    geometry.indexBuffer = slot.indexBuffer;
                }
            }
        }
        else if (meshKey != Opaque3DmeshKey)
        {
            return std::nullopt;
        }
        return geometry;
    }

    struct ResolvedSkinnedOpaque3DGeometry final {
        bgfx::VertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle skinVertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
        u16 jointCount = 0;
    };

    // No built-in skinned fixture exists: only an explicitly bound live skinned
    // slot resolves. Static slots and stale bindings return nullopt.
    [[nodiscard]] std::optional<ResolvedSkinnedOpaque3DGeometry>
    resolveSkinnedOpaque3DGeometry(u32 meshKey) const noexcept
    {
        const auto binding = mesh3DBindings_.find(meshKey);
        if (binding == mesh3DBindings_.end())
        {
            return std::nullopt;
        }
        const GpuMeshId id = binding->second;
        if (id.index >= meshes_.size())
        {
            return std::nullopt;
        }
        const MeshSlot& slot = meshes_[id.index];
        if (!slot.live || slot.identity.value() != id.generation || !slot.skinned ||
            !bgfx::isValid(slot.vertexBuffer) || !bgfx::isValid(slot.skinVertexBuffer) ||
            !bgfx::isValid(slot.indexBuffer))
        {
            return std::nullopt;
        }
        return ResolvedSkinnedOpaque3DGeometry{
            .vertexBuffer = slot.vertexBuffer,
            .skinVertexBuffer = slot.skinVertexBuffer,
            .indexBuffer = slot.indexBuffer,
            .jointCount = slot.jointCount,
        };
    }

    // Declared here only so the accessor below can name it in a return type; the definition stays
    // with the other resource slots. A member function *body* sees the whole class, but a return
    // type is looked up in declaration order.
    struct ShaderSlot;
    struct ShaderUniformBindingTable;

    // A live Sprite2D program for a batch's shader descriptor, or nullptr when the batch named no
    // shader at all. Never a fallback: a sprite that named a shader and cannot get it is a frame
    // error, not a reason to draw with the engine program.
    [[nodiscard]] const ShaderSlot* resolveSprite2DShaderSlot(u32 shaderKey) const noexcept
    {
        const auto binding = shaderBindings_.find(shaderKey);
        if (binding == shaderBindings_.end())
        {
            return nullptr;
        }
        const GpuShaderId id = binding->second;
        if (id.index >= shaders_.size())
        {
            return nullptr;
        }
        const ShaderSlot& slot = shaders_[id.index];
        if (!slot.live || slot.identity.value() != id.generation ||
            slot.shaderKind != GpuShaderKind::Sprite2D || !bgfx::isValid(slot.program))
        {
            return nullptr;
        }
        return &slot;
    }

    [[nodiscard]] const ShaderSlot* resolveMesh3DShaderSlot(u32 shaderKey) const noexcept
    {
        const auto binding = shaderBindings_.find(shaderKey);
        if (binding == shaderBindings_.end())
        {
            return nullptr;
        }
        const GpuShaderId id = binding->second;
        if (id.index >= shaders_.size())
        {
            return nullptr;
        }
        const ShaderSlot& slot = shaders_[id.index];
        if (!slot.live || slot.identity.value() != id.generation ||
            slot.shaderKind != GpuShaderKind::Mesh3D || !bgfx::isValid(slot.program))
        {
            return nullptr;
        }
        return &slot;
    }

    // Picks the program for one Mesh3D draw and publishes its author uniforms, leaving the caller to
    // submit. Returns engineProgram when the draw named no shader.
    //
    // A named shader that does not resolve terminates rather than falling back to the engine program:
    // submitFrame validated every one against this same immutable packet view before any surface
    // state advanced, so reaching here means the binding table changed underneath a validated frame.
    // Falling back would report success and draw the wrong pixels.
    [[nodiscard]] bgfx::ProgramHandle
    resolveMesh3DDrawProgram(FrameResourceTableView resources, FrameResourceRef shaderRef,
                             FrameResourceRef uniformRef, bgfx::ProgramHandle engineProgram,
                             bool skinned) const noexcept
    {
        if (!shaderRef)
        {
            return engineProgram;
        }
        const FrameResourceDescriptor* shaderDescriptor =
            resources.resolve(shaderRef, FrameResourceKind::Shader);
        if (shaderDescriptor == nullptr)
        {
            std::terminate();
        }
        const ShaderSlot* slot =
            resolveMesh3DShaderSlot(static_cast<u32>(shaderDescriptor->deviceBindingKey));
        if (slot == nullptr)
        {
            std::terminate();
        }
        // Same fragment binary, different vertex stage. Both are linked at upload, so a valid slot
        // always carries both -- an invalid one here would mean the slot was built without a skinned
        // stage, which createShader does not allow for Mesh3D.
        const bgfx::ProgramHandle program = skinned ? slot->skinnedProgram : slot->program;
        if (!bgfx::isValid(program))
        {
            std::terminate();
        }

        // Every author uniform is published, not only the ones the binding names: bgfx keeps uniform
        // values across submits, so an unset one would silently inherit whatever the previous draw
        // wrote. A value the caller did not supply is zero.
        const ShaderUniformBindingTable* values = nullptr;
        if (uniformRef)
        {
            const FrameResourceDescriptor* uniformDescriptor =
                resources.resolve(uniformRef, FrameResourceKind::ShaderUniforms);
            if (uniformDescriptor == nullptr)
            {
                std::terminate();
            }
            if (const auto binding =
                    shaderUniformBindings_.find(static_cast<u32>(uniformDescriptor->deviceBindingKey));
                binding != shaderUniformBindings_.end())
            {
                values = &binding->second;
            }
        }
        publishAuthorUniforms(*slot, values);
        return program;
    }

    // Publishes every author uniform of a program, taking values from the batch's binding table and
    // zero for anything the caller left unset. Unset must publish zero rather than skip: bgfx keeps
    // uniform values across submits, so a skipped uniform would silently inherit the previous draw's.
    //
    // The name-to-index resolution is memoized on the slot, keyed by the binding table's key and
    // revision, because both are stable for the life of a batch while this runs per draw.
    // const because every draw path that reaches here resolves both the slot and the table through
    // const accessors; the memo is derived from data they already own, so filling it changes no
    // observable device state. It needs no lock: the device rejects any call off ownerThread_.
    void publishAuthorUniforms(const ShaderSlot& slot,
                               const ShaderUniformBindingTable* values) const noexcept
    {
        static constexpr std::array<float, 4> Zero{};
        if (values == nullptr)
        {
            // No binding at all: every uniform is unset, so there is nothing to resolve and no memo
            // to keep. Publishing zero is still mandatory for the same reason as below.
            for (const ShaderDetail::CustomShaderUniform& uniform : slot.authorUniforms)
            {
                bgfx::setUniform(uniform.handle, Zero.data());
            }
            return;
        }

        if (values->cachedAuthorUniformsRevision != slot.authorUniformsRevision)
        {
            values->valueIndices.assign(slot.authorUniforms.size(),
                                        ShaderUniformBindingTable::NoValueIndex);
            for (usize index = 0; index < slot.authorUniforms.size(); ++index)
            {
                const std::string_view wanted{slot.authorUniforms[index].name.data()};
                for (usize candidate = 0; candidate < values->values.size(); ++candidate)
                {
                    if (std::string_view{values->values[candidate].name.data()} == wanted)
                    {
                        values->valueIndices[index] = static_cast<u8>(candidate);
                        break;
                    }
                }
            }
            values->cachedAuthorUniformsRevision = slot.authorUniformsRevision;
        }

        for (usize index = 0; index < slot.authorUniforms.size(); ++index)
        {
            const u8 valueIndex = values->valueIndices[index];
            // Unset must publish zero rather than skip: bgfx keeps uniform values across submits, so a
            // skipped uniform would silently inherit whatever the previous draw wrote.
            const std::array<float, 4>& value = valueIndex == ShaderUniformBindingTable::NoValueIndex
                                                    ? Zero
                                                    : values->values[valueIndex].value;
            bgfx::setUniform(slot.authorUniforms[index].handle, value.data());
        }
    }

    // Sprite2D geometry validation already rejects a stale, cross-packet or wrong-kind frame
    // resource ref, but the device binding table it names is private to this device and therefore
    // outside that check. A ref that resolves to a key with no live Sprite2D program would
    // otherwise reach the submit loop, which has no way to report a failure -- and the earlier
    // behaviour there was to quietly draw the engine's own fragment stage instead. That returns
    // success with wrong pixels, so it is rejected here, before any surface state advances.
    [[nodiscard]] Core::Status validateSprite2DShaderBindings(
        RenderSceneView scene, FrameResourceTableView resources) const noexcept
    {
        for (const RenderSprite2DItem& sprite : scene.sprites2D())
        {
            if (!sprite.shader)
            {
                continue;
            }
            const FrameResourceDescriptor* descriptor =
                resources.resolve(sprite.shader, FrameResourceKind::Shader);
            if (descriptor == nullptr ||
                descriptor->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
            {
                return Core::failure(RenderErrorCode::InvalidFrameResource,
                                     "A Sprite2D shader ref is stale, cross-packet, wrong-kind, or "
                                     "out of binding range");
            }
            if (resolveSprite2DShaderSlot(static_cast<u32>(descriptor->deviceBindingKey)) == nullptr)
            {
                return Core::failure(
                    RenderErrorCode::ShaderNotFound,
                    "A Sprite2D shader ref resolves to a device binding key with no live Sprite2D "
                    "program: the binding is unset, retired, or was uploaded for another shader kind");
            }
        }
        return Core::success();
    }

    // Mesh3D uses the same shader binding pair for rigid batches and unbatched skinned items. Frame
    // resource shape is checked by the scene's 3D resource validator; this validates the device-local
    // binding table before a submit loop where returning an error is no longer possible.
    [[nodiscard]] Core::Status validateMesh3DShaderBindings(
        RenderSceneView scene, FrameResourceTableView resources) const noexcept
    {
        const auto validateItems = [this, resources](const auto items) noexcept -> Core::Status {
            for (const auto& item : items)
            {
                if (!item.shader)
                {
                    continue;
                }
                const FrameResourceDescriptor* descriptor =
                    resources.resolve(item.shader, FrameResourceKind::Shader);
                if (descriptor == nullptr ||
                    descriptor->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
                {
                    return Core::failure(RenderErrorCode::InvalidFrameResource,
                                         "A Mesh3D shader ref is stale, cross-packet, wrong-kind, or "
                                         "out of binding range");
                }
                const ShaderSlot* slot =
                    resolveMesh3DShaderSlot(static_cast<u32>(descriptor->deviceBindingKey));
                if (slot == nullptr || !bgfx::isValid(slot->skinnedProgram))
                {
                    return Core::failure(
                        RenderErrorCode::ShaderNotFound,
                        "A Mesh3D shader ref resolves to a device binding key with no live Mesh3D "
                        "program for both rigid and skinned geometry");
                }
            }
            return Core::success();
        };
        if (auto status = validateItems(scene.meshes3D()); !status)
        {
            return status;
        }
        return validateItems(scene.skinnedMeshes3D());
    }

    [[nodiscard]] Core::Status validateMesh3DMaterialAlphaBindings(
        RenderSceneView scene, FrameResourceTableView resources) const noexcept
    {
        const auto validateItems = [this, resources](const auto items) noexcept -> Core::Status {
            for (const auto& item : items)
            {
                const FrameResourceDescriptor* material =
                    resources.resolve(item.material, FrameResourceKind::Mesh3DMaterial);
                if (material == nullptr ||
                    material->deviceBindingKey >
                        static_cast<u64>((std::numeric_limits<u32>::max)()))
                {
                    return Core::failure(
                        RenderErrorCode::InvalidFrameResource,
                        "A Mesh3D item references an invalid material frame resource");
                }
                const u32 materialKey = static_cast<u32>(material->deviceBindingKey);
                const auto binding = mesh3DMaterialBindings_.find(materialKey);
                const Mesh3DAlphaMode resolvedAlphaMode =
                    binding == mesh3DMaterialBindings_.end()
                        ? Mesh3DAlphaMode::Opaque
                        : binding->second.alphaMode;
                if (resolvedAlphaMode != item.alphaMode)
                {
                    return Core::failure(
                        RenderErrorCode::InvalidFrameResource,
                        "A Mesh3D item alpha mode does not match its device material binding");
                }
            }
            return Core::success();
        };

        if (auto status = validateItems(scene.meshes3D()); !status)
        {
            return status;
        }
        return validateItems(scene.skinnedMeshes3D());
    }

    // Mirrors the Null device binding preflight: a bound static key must map to
    // a non-skinned slot, a bound skinned key to a skinned slot whose joint
    // count matches the item palette. Unbound keys stay tolerated (draw skips).
    [[nodiscard]] Core::Status validateSkinnedMesh3DBindings(
        RenderSceneView scene, FrameResourceTableView resources) const noexcept
    {
        for (const RenderMesh3DItem& item : scene.meshes3D())
        {
            const FrameResourceDescriptor* geometry =
                resources.resolve(item.mesh, FrameResourceKind::Mesh3DGeometry);
            if (geometry == nullptr)
            {
                continue;
            }
            const auto binding = mesh3DBindings_.find(static_cast<u32>(geometry->deviceBindingKey));
            if (binding == mesh3DBindings_.end() || binding->second.index >= meshes_.size())
            {
                continue;
            }
            const MeshSlot& slot = meshes_[binding->second.index];
            if (slot.live && slot.identity.value() == binding->second.generation && slot.skinned)
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "A static Opaque3D item resolves a skinned mesh binding");
            }
        }
        for (const RenderSkinnedMesh3DItem& item : scene.skinnedMeshes3D())
        {
            const FrameResourceDescriptor* geometry =
                resources.resolve(item.mesh, FrameResourceKind::SkinnedMesh3DGeometry);
            if (geometry == nullptr)
            {
                continue;
            }
            const auto binding = mesh3DBindings_.find(static_cast<u32>(geometry->deviceBindingKey));
            if (binding == mesh3DBindings_.end() || binding->second.index >= meshes_.size())
            {
                continue;
            }
            const MeshSlot& slot = meshes_[binding->second.index];
            if (!slot.live || slot.identity.value() != binding->second.generation)
            {
                continue;
            }
            if (!slot.skinned)
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "A skinned Opaque3D item resolves a non-skinned mesh binding");
            }
            if (slot.jointCount != item.paletteJointCount)
            {
                return Core::failure(
                    RenderErrorCode::InvalidFrameResource,
                    "A skinned Opaque3D palette joint count does not match the bound skeleton");
            }
        }
        return Core::success();
    }

    void prepareOpaque3DInstanceBuffer(RenderSceneView scene,
                                       FrameResourceTableView resources,
                                       const PreparedOpaque3D& prepared,
                                       bgfx::InstanceDataBuffer& instanceBuffer) noexcept
    {
        if (prepared.requirements.instanceCount == 0)
        {
            return;
        }

        constexpr u16 InstanceStride = static_cast<u16>(sizeof(BgfxOpaque3DInstanceData));
        bgfx::allocInstanceDataBuffer(&instanceBuffer, prepared.requirements.instanceCount, InstanceStride);
        auto instances = std::span{
            reinterpret_cast<BgfxOpaque3DInstanceData*>(instanceBuffer.data),
            static_cast<usize>(prepared.requirements.instanceCount),
        };
        auto written = writeOpaque3DInstanceData(scene, resources, instances);
        if (!written || written->instanceCount != prepared.requirements.instanceCount ||
            written->batchCount != prepared.requirements.batchCount)
        {
            std::terminate();
        }
    }

    void submitOpaque3DShadowDepth(
        RenderSceneView scene,
        FrameResourceTableView resources,
        const PreparedOpaque3D& prepared,
        bgfx::InstanceDataBuffer& instanceBuffer,
        bgfx::ViewId view) noexcept
    {
        if (prepared.requirements.batchCount == 0)
        {
            std::terminate();
        }

        bgfx::setScissor();
        for (const RenderMesh3DBatch& batch : scene.mesh3DBatches())
        {
            const FrameResourceDescriptor* meshResource =
                resources.resolve(batch.mesh, FrameResourceKind::Mesh3DGeometry);
            if (meshResource == nullptr ||
                meshResource->deviceBindingKey >
                    static_cast<u64>((std::numeric_limits<u32>::max)()))
            {
                std::terminate();
            }
            const auto geometry = resolveOpaque3DGeometry(
                static_cast<u32>(meshResource->deviceBindingKey));
            if (!geometry.has_value())
            {
                continue;
            }

            const u64 shadowState = kOpaque3DShadowDepthState |
                (batch.doubleSided ? 0 : BGFX_STATE_CULL_CW);
            bgfx::setState(shadowState);
            bgfx::setVertexBuffer(0, geometry->vertexBuffer);
            bgfx::setIndexBuffer(geometry->indexBuffer);
            bgfx::setInstanceDataBuffer(&instanceBuffer, batch.firstItem,
                                        batch.itemCount);
            bgfx::submit(view, opaque3DCsmDepthProgram_);
        }
    }

    void submitCascadedDirectionalShadowDepth(
        RenderSceneView scene,
        FrameResourceTableView resources,
        const PreparedOpaque3D& prepared,
        bgfx::InstanceDataBuffer& instanceBuffer,
        usize cascadeIndex) noexcept
    {
        if (!prepared.cascadedDirectionalShadow.has_value() ||
            cascadeIndex >= BgfxCascadedDirectionalShadowCascadeCount)
        {
            std::terminate();
        }
        submitOpaque3DShadowDepth(scene, resources, prepared, instanceBuffer,
                                  kCascadedDirectionalShadowViews[cascadeIndex]);
    }

    void submitSpotLightShadowDepth(
        RenderSceneView scene,
        FrameResourceTableView resources,
        const PreparedOpaque3D& prepared,
        bgfx::InstanceDataBuffer& instanceBuffer) noexcept
    {
        if (!prepared.spotLightShadow.has_value())
        {
            std::terminate();
        }
        submitOpaque3DShadowDepth(scene, resources, prepared, instanceBuffer,
                                  kSpotLightShadowView);
    }

    void submitPointLightShadowDepth(
        RenderSceneView scene,
        FrameResourceTableView resources,
        const PreparedOpaque3D& prepared,
        bgfx::InstanceDataBuffer& instanceBuffer,
        usize faceIndex) noexcept
    {
        if (!prepared.pointLightShadow.has_value() ||
            faceIndex >= BgfxPointLightShadowFaceCount)
        {
            std::terminate();
        }
        submitOpaque3DShadowDepth(scene, resources, prepared, instanceBuffer,
                                  kPointLightShadowViews[faceIndex]);
    }

    void submitMesh3D(RenderSceneView scene, FrameResourceTableView resources,
                      const PreparedOpaque3D& prepared,
                      bgfx::InstanceDataBuffer& instanceBuffer,
                      bool transparentPass) noexcept
    {
        const bool passEmpty = transparentPass
                                   ? scene.transparent3DDraws().empty()
                                   : (scene.opaqueMeshes3D().empty() &&
                                      scene.opaqueSkinnedMeshes3D().empty());
        if (passEmpty)
        {
            return;
        }
        if (!bgfx::isValid(opaque3DDefaultShadowTexture_))
        {
            std::terminate();
        }

        const Mesh3DDirectionalLightUniformStorage* lightDirections = &mesh3DLightDirections_;
        const Mesh3DDirectionalLightUniformStorage* lightColors = &mesh3DLightColors_;
        const Mesh3DPointLightUniformStorage* pointLightPositionsAndRadii =
            &mesh3DPointLightPositionsAndRadii_;
        const Mesh3DPointLightUniformStorage* pointLightColors = &mesh3DPointLightColors_;
        const Mesh3DSpotLightUniformStorage* spotLightPositionsAndRadii =
            &mesh3DSpotLightPositionsAndRadii_;
        const Mesh3DSpotLightUniformStorage* spotLightDirectionsAndInnerCosines =
            &mesh3DSpotLightDirectionsAndInnerCosines_;
        const Mesh3DSpotLightUniformStorage* spotLightColorsAndOuterCosines =
            &mesh3DSpotLightColorsAndOuterCosines_;
        float ambientScale = mesh3DAmbientScale_;
        Mesh3DDirectionalLightUniformStorage frameLightDirections{};
        Mesh3DDirectionalLightUniformStorage frameLightColors{};
        Mesh3DPointLightUniformStorage framePointLightPositionsAndRadii{};
        Mesh3DPointLightUniformStorage framePointLightColors{};
        Mesh3DSpotLightUniformStorage frameSpotLightPositionsAndRadii{};
        Mesh3DSpotLightUniformStorage frameSpotLightDirectionsAndInnerCosines{};
        Mesh3DSpotLightUniformStorage frameSpotLightColorsAndOuterCosines{};
        if (scene.mesh3DLighting().has_value())
        {
            encodeMesh3DLighting(scene.mesh3DLighting()->descriptor(), frameLightDirections,
                                 frameLightColors, framePointLightPositionsAndRadii,
                                 framePointLightColors, frameSpotLightPositionsAndRadii,
                                 frameSpotLightDirectionsAndInnerCosines,
                                 frameSpotLightColorsAndOuterCosines, ambientScale);
            lightDirections = &frameLightDirections;
            lightColors = &frameLightColors;
            pointLightPositionsAndRadii = &framePointLightPositionsAndRadii;
            pointLightColors = &framePointLightColors;
            spotLightPositionsAndRadii = &frameSpotLightPositionsAndRadii;
            spotLightDirectionsAndInnerCosines = &frameSpotLightDirectionsAndInnerCosines;
            spotLightColorsAndOuterCosines = &frameSpotLightColorsAndOuterCosines;
        }

        std::array<std::array<float, 16>, BgfxCascadedDirectionalShadowCascadeCount>
            csmMatrices{};
        for (auto& matrix : csmMatrices)
        {
            bx::mtxIdentity(matrix.data());
        }
        std::array<float, 4> csmSplitDepths{};
        std::array<float, 4> csmParams{
            0.0F,
            0.0F,
            1.0F / static_cast<float>(shadowMapExtents_.directionalAtlasExtent()),
            0.0F,
        };
        if (prepared.cascadedDirectionalShadow.has_value())
        {
            const auto& shadow = *prepared.cascadedDirectionalShadow;
            for (usize cascadeIndex = 0; cascadeIndex < csmMatrices.size(); ++cascadeIndex)
            {
                csmMatrices[cascadeIndex] =
                    shadow.projection.cascades[cascadeIndex].samplingTransform;
                csmSplitDepths[cascadeIndex] =
                    shadow.projection.splitDepthsMeters[cascadeIndex];
            }
            csmParams[0] = shadow.depthBias;
            csmParams[1] = shadow.normalBiasMeters;
            csmParams[3] = static_cast<float>(shadow.directionalLightIndex) + 1.0F;
        }

        std::array<float, 16> spotShadowMatrix{};
        bx::mtxIdentity(spotShadowMatrix.data());
        std::array<float, 4> spotShadowParams{
            0.0F,
            0.0F,
            1.0F / static_cast<float>(shadowMapExtents_.spotLightMapExtent),
            0.0F,
        };
        if (prepared.spotLightShadow.has_value())
        {
            const auto& shadow = *prepared.spotLightShadow;
            spotShadowMatrix = shadow.projection.samplingTransform;
            spotShadowParams[0] = shadow.depthBias;
            spotShadowParams[1] = shadow.normalBiasMeters;
            spotShadowParams[3] = static_cast<float>(shadow.spotLightIndex) + 1.0F;
        }

        std::array<std::array<float, 16>, BgfxPointLightShadowFaceCount>
            pointShadowMatrices{};
        for (auto& matrix : pointShadowMatrices)
        {
            bx::mtxIdentity(matrix.data());
        }
        std::array<float, 4> pointShadowParams{
            0.0F,
            0.0F,
            1.0F / static_cast<float>(shadowMapExtents_.pointLightFaceExtent),
            0.0F,
        };
        if (prepared.pointLightShadow.has_value())
        {
            const auto& shadow = *prepared.pointLightShadow;
            for (usize faceIndex = 0; faceIndex < pointShadowMatrices.size(); ++faceIndex)
            {
                pointShadowMatrices[faceIndex] =
                    shadow.projection.faces[faceIndex].samplingTransform;
            }
            pointShadowParams[0] = shadow.depthBias;
            pointShadowParams[1] = shadow.normalBiasMeters;
            pointShadowParams[3] = static_cast<float>(shadow.pointLightIndex) + 1.0F;
        }

        bgfx::TextureHandle iblDiffuse = opaque3DDefaultIblCube_;
        bgfx::TextureHandle iblSpecular = opaque3DDefaultIblCube_;
        bgfx::TextureHandle iblBrdf = opaque3DDefaultIblBrdfLut_;
        std::array<float, 4> iblParams{};
        if (mesh3DImageBasedLighting_.has_value() &&
            isLiveEnvironmentMap(mesh3DImageBasedLighting_->environmentMap))
        {
            const EnvironmentMapSlot& environment =
                environmentMaps_[mesh3DImageBasedLighting_->environmentMap.index];
            iblDiffuse = environment.resources.diffuseIrradiance;
            iblSpecular = environment.resources.prefilteredSpecular;
            iblBrdf = environment.resources.brdfLut;
            iblParams = {
                mesh3DImageBasedLighting_->intensity,
                static_cast<float>(environment.specularMipCount - 1U),
                1.0F,
                mesh3DImageBasedLighting_->rotationRadians,
            };
        }

        // bgfx::submit() discards draw state, so every draw publishes complete
        // material, texture, and frame uniform state. Shared by opaque batches
        // and transparent static/skinned per-item draws below.
        const auto bindMaterialAndFrameUniforms = [&](u32 materialKey) noexcept {
            const auto materialBinding = mesh3DMaterialBindings_.find(materialKey);
            const Mesh3DMaterialBindingDesc binding = materialBinding == mesh3DMaterialBindings_.end()
                                                          ? Mesh3DMaterialBindingDesc{}
                                                          : materialBinding->second;

            bgfx::TextureHandle materialTexture = opaque3DDefaultTexture_;
            if (binding.baseColorTexture)
            {
                const GpuTextureId id = binding.baseColorTexture;
                if (id.index < textures_.size())
                {
                    const TextureSlot& slot = textures_[id.index];
                    if (slot.live && slot.identity.value() == id.generation && bgfx::isValid(slot.handle))
                    {
                        materialTexture = slot.handle;
                    }
                }
            }

            bgfx::TextureHandle mrTexture = opaque3DDefaultMrTexture_;
            float mrMapBound = 0.0F;
            if (binding.metallicRoughnessTexture)
            {
                const GpuTextureId id = binding.metallicRoughnessTexture;
                if (id.index < textures_.size())
                {
                    const TextureSlot& slot = textures_[id.index];
                    if (slot.live && slot.identity.value() == id.generation && bgfx::isValid(slot.handle))
                    {
                        mrTexture = slot.handle;
                        mrMapBound = 1.0F;
                    }
                }
            }

            bgfx::TextureHandle normalTexture = opaque3DDefaultNormalTexture_;
            float normalMapBound = 0.0F;
            if (binding.normalTexture)
            {
                const GpuTextureId id = binding.normalTexture;
                if (id.index < textures_.size())
                {
                    const TextureSlot& slot = textures_[id.index];
                    if (slot.live && slot.identity.value() == id.generation && bgfx::isValid(slot.handle))
                    {
                        normalTexture = slot.handle;
                        normalMapBound = 1.0F;
                    }
                }
            }

            // ambient from the current frame snapshot or device fallback; w = MR map bound flag.
            const std::array<float, 4> mrParams{
                binding.metallicFactor, binding.roughnessFactor, ambientScale, mrMapBound};
            // x = normal map bound; yzw unused.
            const std::array<float, 4> normalParams{normalMapBound, 0.0F, 0.0F, 0.0F};
            // Linear radiance the material emits regardless of lighting (ADR 0043); w unused.
            const std::array<float, 4> emissiveFactor{binding.emissiveFactorR,
                                                      binding.emissiveFactorG,
                                                      binding.emissiveFactorB, 0.0F};

            bgfx::setTexture(0, opaque3DSampler_, materialTexture);
            bgfx::setTexture(1, opaque3DMrSampler_, mrTexture);
            bgfx::setTexture(2, opaque3DNormalSampler_, normalTexture);
            const bgfx::TextureHandle directionalShadowTexture =
                cascadedDirectionalShadowResources_.valid()
                    ? cascadedDirectionalShadowResources_.depthAtlas
                    : opaque3DDefaultShadowTexture_;
            const bgfx::TextureHandle spotShadowTexture =
                spotLightShadowResources_.valid()
                    ? spotLightShadowResources_.depthMap
                    : opaque3DDefaultShadowTexture_;
            bgfx::setTexture(3, opaque3DCsmAtlasSampler_, directionalShadowTexture);
            bgfx::setTexture(4, opaque3DIblDiffuseSampler_, iblDiffuse);
            bgfx::setTexture(5, opaque3DIblSpecularSampler_, iblSpecular);
            bgfx::setTexture(6, opaque3DIblBrdfSampler_, iblBrdf);
            bgfx::setTexture(7, opaque3DSpotShadowMapSampler_, spotShadowTexture);
            for (usize faceIndex = 0; faceIndex < opaque3DPointShadowMapSamplers_.size();
                 ++faceIndex)
            {
                const bgfx::TextureHandle pointShadowTexture =
                    pointLightShadowResources_.valid()
                        ? pointLightShadowResources_.depthMaps[faceIndex]
                        : opaque3DDefaultShadowTexture_;
                bgfx::setTexture(static_cast<u8>(8U + faceIndex),
                                 opaque3DPointShadowMapSamplers_[faceIndex],
                                 pointShadowTexture);
            }
            bgfx::setUniform(opaque3DLightDirectionsUniform_, lightDirections->data(),
                             static_cast<u16>(Mesh3DLightingDesc::MaximumDirectionalLightCount));
            bgfx::setUniform(opaque3DLightColorsUniform_, lightColors->data(),
                             static_cast<u16>(Mesh3DLightingDesc::MaximumDirectionalLightCount));
            bgfx::setUniform(opaque3DPointLightPositionsUniform_, pointLightPositionsAndRadii->data(),
                             static_cast<u16>(Mesh3DLightingDesc::MaximumPointLightCount));
            bgfx::setUniform(opaque3DPointLightColorsUniform_, pointLightColors->data(),
                             static_cast<u16>(Mesh3DLightingDesc::MaximumPointLightCount));
            bgfx::setUniform(opaque3DSpotLightPositionsUniform_, spotLightPositionsAndRadii->data(),
                             static_cast<u16>(Mesh3DLightingDesc::MaximumSpotLightCount));
            bgfx::setUniform(opaque3DSpotLightDirectionsUniform_, spotLightDirectionsAndInnerCosines->data(),
                             static_cast<u16>(Mesh3DLightingDesc::MaximumSpotLightCount));
            bgfx::setUniform(opaque3DSpotLightColorsUniform_, spotLightColorsAndOuterCosines->data(),
                             static_cast<u16>(Mesh3DLightingDesc::MaximumSpotLightCount));
            bgfx::setUniform(opaque3DMrParamsUniform_, mrParams.data());
            bgfx::setUniform(opaque3DNormalParamsUniform_, normalParams.data());
            bgfx::setUniform(opaque3DEmissiveFactorUniform_, emissiveFactor.data());
            bgfx::setUniform(opaque3DCsmMatricesUniform_, csmMatrices.front().data(),
                             static_cast<u16>(csmMatrices.size()));
            bgfx::setUniform(opaque3DCsmSplitDepthsUniform_, csmSplitDepths.data());
            bgfx::setUniform(opaque3DCsmParamsUniform_, csmParams.data());
            bgfx::setUniform(opaque3DSpotShadowMatrixUniform_, spotShadowMatrix.data());
            bgfx::setUniform(opaque3DSpotShadowParamsUniform_, spotShadowParams.data());
            bgfx::setUniform(opaque3DPointShadowMatricesUniform_,
                             pointShadowMatrices.front().data(),
                             static_cast<u16>(pointShadowMatrices.size()));
            bgfx::setUniform(opaque3DPointShadowParamsUniform_, pointShadowParams.data());
            bgfx::setUniform(opaque3DIblParamsUniform_, iblParams.data());
        };

        bgfx::setScissor();
        if (!transparentPass && prepared.requirements.batchCount != 0)
        {
            for (const RenderMesh3DBatch& batch : scene.mesh3DBatches())
            {
                const FrameResourceDescriptor* meshResource =
                    resources.resolve(batch.mesh, FrameResourceKind::Mesh3DGeometry);
                const FrameResourceDescriptor* materialResource =
                    resources.resolve(batch.material, FrameResourceKind::Mesh3DMaterial);
                if (meshResource == nullptr || materialResource == nullptr ||
                    meshResource->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()) ||
                    materialResource->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
                {
                    // submitFrame preflight validated the same immutable packet view.
                    std::terminate();
                }
                const u32 meshKey = static_cast<u32>(meshResource->deviceBindingKey);
                const u32 materialKey = static_cast<u32>(materialResource->deviceBindingKey);
                const u64 renderState = kOpaque3DState | (batch.doubleSided ? 0 : BGFX_STATE_CULL_CW);
                bgfx::setState(renderState);

                const auto geometry = resolveOpaque3DGeometry(meshKey);
                if (!geometry.has_value())
                {
                    // Unbound non-fixture meshKey: skip submit rather than draw wrong geometry.
                    continue;
                }

                bgfx::setVertexBuffer(0, geometry->vertexBuffer);
                bgfx::setIndexBuffer(geometry->indexBuffer);
                bgfx::setInstanceDataBuffer(&instanceBuffer, batch.firstItem, batch.itemCount);
                bindMaterialAndFrameUniforms(materialKey);

                // The engine program unless this batch named a custom shader. Uniform and sampler
                // handles stay the engine's own: bgfx dedupes uniforms by name across programs, and
                // the contract .sh the custom fragment stage must include declares exactly the same
                // set.
                const bgfx::ProgramHandle batchProgram = resolveMesh3DDrawProgram(
                    resources, batch.shader, batch.shaderUniforms, opaque3DProgram_, false);
                bgfx::submit(kOpaque3DView, batchProgram);
            }
        }

        const std::span<const float> palette = scene.skinnedMesh3DPalette();
        const auto submitSkinnedItem = [&](const RenderSkinnedMesh3DItem& item,
                                           bgfx::ViewId viewId,
                                           u64 passState) noexcept {
            const FrameResourceDescriptor* meshResource =
                resources.resolve(item.mesh, FrameResourceKind::SkinnedMesh3DGeometry);
            const FrameResourceDescriptor* materialResource =
                resources.resolve(item.material, FrameResourceKind::Mesh3DMaterial);
            if (meshResource == nullptr || materialResource == nullptr ||
                meshResource->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()) ||
                materialResource->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
            {
                // submitFrame preflight validated the same immutable packet view.
                std::terminate();
            }
            const auto geometry =
                resolveSkinnedOpaque3DGeometry(static_cast<u32>(meshResource->deviceBindingKey));
            if (!geometry.has_value())
            {
                // Unbound skinned meshKey: there is no skinned fixture, skip.
                return;
            }
            if (geometry->jointCount != item.paletteJointCount ||
                static_cast<u64>(item.paletteJointOffset) + item.paletteJointCount >
                    palette.size() / SkinnedMesh3DPaletteFloatsPerJoint)
            {
                // submitFrame preflight validated palette shape against this binding.
                std::terminate();
            }

            const u64 renderState = passState | (item.doubleSided ? 0 : BGFX_STATE_CULL_CW);
            bgfx::setState(renderState);
            bgfx::setTransform(item.columnMajorWorldTransform.data());
            bgfx::setVertexBuffer(0, geometry->vertexBuffer);
            bgfx::setVertexBuffer(1, geometry->skinVertexBuffer);
            bgfx::setIndexBuffer(geometry->indexBuffer);
            const float* itemPalette =
                palette.data() + static_cast<usize>(item.paletteJointOffset) *
                                     SkinnedMesh3DPaletteFloatsPerJoint;
            const u16 paletteArrayJointCount = static_cast<u16>((std::min)(
                item.paletteJointCount,
                static_cast<u32>(kOpaque3DSkinPaletteArrayJointCount)));
            bgfx::setUniform(opaque3DSkinPaletteUniform_,
                             itemPalette,
                             paletteArrayJointCount);
            if (item.paletteJointCount > kOpaque3DSkinPaletteArrayJointCount)
            {
                bgfx::setUniform(
                    opaque3DSkinPaletteLastUniform_,
                    itemPalette + static_cast<usize>(kOpaque3DSkinPaletteArrayJointCount) *
                                      SkinnedMesh3DPaletteFloatsPerJoint);
            }
            const std::array<float, 4> skinColor{
                item.baseColorFactor.red,
                item.baseColorFactor.green,
                item.baseColorFactor.blue,
                item.baseColorFactor.alpha,
            };
            bgfx::setUniform(opaque3DSkinColorUniform_, skinColor.data());
            bindMaterialAndFrameUniforms(static_cast<u32>(materialResource->deviceBindingKey));
            // Per-item because skinned draws are never batched. The skinned link of the same cooked
            // fragment binary, so one authored Mesh3D shader covers rigid and skinned geometry.
            const bgfx::ProgramHandle itemProgram =
                resolveMesh3DDrawProgram(resources, item.shader, item.shaderUniforms,
                                         opaque3DSkinnedProgram_, true);
            bgfx::submit(viewId, itemProgram);
        };

        if (!transparentPass)
        {
            for (const RenderSkinnedMesh3DItem& item : scene.opaqueSkinnedMeshes3D())
            {
                submitSkinnedItem(item, kOpaque3DView, kOpaque3DState);
            }
            return;
        }

        const std::span<const RenderMesh3DItem> staticItems = scene.meshes3D();
        const std::span<const RenderSkinnedMesh3DItem> skinnedItems =
            scene.skinnedMeshes3D();
        for (const RenderTransparent3DDraw& draw : scene.transparent3DDraws())
        {
            switch (draw.kind)
            {
            case RenderTransparent3DDrawKind::StaticMesh:
            {
                if (draw.itemIndex >= staticItems.size())
                {
                    std::terminate();
                }
                const RenderMesh3DItem& item = staticItems[draw.itemIndex];
                const FrameResourceDescriptor* meshResource =
                    resources.resolve(item.mesh, FrameResourceKind::Mesh3DGeometry);
                const FrameResourceDescriptor* materialResource =
                    resources.resolve(item.material, FrameResourceKind::Mesh3DMaterial);
                if (meshResource == nullptr || materialResource == nullptr ||
                    meshResource->deviceBindingKey >
                        static_cast<u64>((std::numeric_limits<u32>::max)()) ||
                    materialResource->deviceBindingKey >
                        static_cast<u64>((std::numeric_limits<u32>::max)()))
                {
                    std::terminate();
                }
                const auto geometry = resolveOpaque3DGeometry(
                    static_cast<u32>(meshResource->deviceBindingKey));
                if (!geometry.has_value())
                {
                    continue;
                }

                const u64 renderState =
                    kTransparent3DState | (item.doubleSided ? 0 : BGFX_STATE_CULL_CW);
                bgfx::setState(renderState);
                bgfx::setVertexBuffer(0, geometry->vertexBuffer);
                bgfx::setIndexBuffer(geometry->indexBuffer);
                bgfx::setInstanceDataBuffer(&instanceBuffer, draw.itemIndex, 1);
                bindMaterialAndFrameUniforms(
                    static_cast<u32>(materialResource->deviceBindingKey));
                // Per-item, not per-batch: the transparent pass submits each item alone so that the
                // back-to-front order across static and skinned draws is preserved.
                const bgfx::ProgramHandle itemProgram = resolveMesh3DDrawProgram(
                    resources, item.shader, item.shaderUniforms, opaque3DProgram_, false);
                bgfx::submit(kTransparent3DView, itemProgram);
                break;
            }
            case RenderTransparent3DDrawKind::SkinnedMesh:
                if (draw.itemIndex >= skinnedItems.size())
                {
                    std::terminate();
                }
                submitSkinnedItem(skinnedItems[draw.itemIndex], kTransparent3DView,
                                  kTransparent3DState);
                break;
            default:
                std::terminate();
            }
        }
    }

    void submitSprite2D(RenderSceneView scene, FrameResourceTableView resources,
                        PreparedSprite2D prepared) noexcept
    {
        if (prepared.requirements.spriteCount == 0)
        {
            return;
        }

        bgfx::TransientVertexBuffer transientVertices{};
        bgfx::TransientIndexBuffer transientIndices{};
        bgfx::allocTransientVertexBuffer(&transientVertices, prepared.requirements.vertexCount, sprite2DVertexLayout_);
        bgfx::allocTransientIndexBuffer(&transientIndices, prepared.requirements.indexCount, true);

        auto vertices = std::span{reinterpret_cast<BgfxSprite2DVertex*>(transientVertices.data),
                                  static_cast<usize>(prepared.requirements.vertexCount)};
        auto indices = std::span{reinterpret_cast<u32*>(transientIndices.data),
                                 static_cast<usize>(prepared.requirements.indexCount)};
        auto written = writeSprite2DGeometry(scene, resources, vertices, indices);
        if (!written || written->spriteCount != prepared.requirements.spriteCount ||
            written->vertexCount != prepared.requirements.vertexCount ||
            written->indexCount != prepared.requirements.indexCount ||
            written->batchCount != prepared.requirements.batchCount)
        {
            std::terminate();
        }

        // One submit per contiguous (base texture, normal texture) batch so both
        // packet-local bindings remain stable for the complete draw.
        // bgfx::submit() discards draw state by default, so every batch must
        // publish the complete state required by the following submit.
        const auto sprites = scene.sprites2D();
        Sprite2DLightUniformStorage lightPositionsAndRadii{};
        Sprite2DLightUniformStorage lightColors{};
        Sprite2DShadowUniformStorage shadowSegments{};
        std::array<float, 4> lightParams{1.0F, 0.0F, 0.0F, 0.0F};
        if (scene.sprite2DLighting().has_value())
        {
            encodeSprite2DLighting(scene.sprite2DLighting()->descriptor(), lightPositionsAndRadii,
                                   lightColors, shadowSegments, lightParams);
        }
        u32 batchBegin = 0;
        while (batchBegin < prepared.requirements.spriteCount)
        {
            const FrameResourceRef batchTexture = sprites[batchBegin].texture;
            const FrameResourceRef batchNormalTexture = sprites[batchBegin].normalTexture;
            const FrameResourceRef batchShader = sprites[batchBegin].shader;
            const FrameResourceDescriptor* descriptor =
                resources.resolve(batchTexture, FrameResourceKind::Texture2D);
            if (descriptor == nullptr)
            {
                std::terminate();
            }
            const u32 batchKey = static_cast<u32>(descriptor->deviceBindingKey);
            const FrameResourceDescriptor* normalDescriptor = nullptr;
            if (batchNormalTexture)
            {
                normalDescriptor = resources.resolve(batchNormalTexture, FrameResourceKind::Texture2D);
                if (normalDescriptor == nullptr)
                {
                    std::terminate();
                }
            }
            const FrameResourceDescriptor* shaderDescriptor = nullptr;
            if (batchShader)
            {
                shaderDescriptor = resources.resolve(batchShader, FrameResourceKind::Shader);
                if (shaderDescriptor == nullptr)
                {
                    std::terminate();
                }
            }
            const FrameResourceRef batchShaderUniforms = sprites[batchBegin].shaderUniforms;
            const FrameResourceDescriptor* shaderUniformDescriptor = nullptr;
            if (batchShaderUniforms)
            {
                shaderUniformDescriptor =
                    resources.resolve(batchShaderUniforms, FrameResourceKind::ShaderUniforms);
                if (shaderUniformDescriptor == nullptr)
                {
                    std::terminate();
                }
            }
            u32 batchEnd = batchBegin + 1U;
            while (batchEnd < prepared.requirements.spriteCount &&
                   sprites[batchEnd].texture == batchTexture &&
                   sprites[batchEnd].normalTexture == batchNormalTexture &&
                   sprites[batchEnd].shader == batchShader &&
                   sprites[batchEnd].shaderUniforms == batchShaderUniforms)
            {
                ++batchEnd;
            }

            bgfx::TextureHandle texture = sprite2DDefaultTexture_;
            if (const auto binding = texture2DBindings_.find(batchKey); binding != texture2DBindings_.end())
            {
                const GpuTextureId id = binding->second;
                if (id.index < textures_.size())
                {
                    const TextureSlot& slot = textures_[id.index];
                    if (slot.live && slot.identity.value() == id.generation && bgfx::isValid(slot.handle))
                    {
                        texture = slot.handle;
                    }
                }
            }

            bgfx::TextureHandle normalTexture = sprite2DDefaultNormalTexture_;
            float normalMapBound = 0.0F;
            if (normalDescriptor != nullptr)
            {
                const u32 normalBatchKey = static_cast<u32>(normalDescriptor->deviceBindingKey);
                if (const auto binding = texture2DBindings_.find(normalBatchKey);
                    binding != texture2DBindings_.end())
                {
                    const GpuTextureId id = binding->second;
                    if (id.index < textures_.size())
                    {
                        const TextureSlot& slot = textures_[id.index];
                        if (slot.live && slot.identity.value() == id.generation && bgfx::isValid(slot.handle))
                        {
                            normalTexture = slot.handle;
                            normalMapBound = 1.0F;
                        }
                    }
                }
            }
            // The engine program unless this batch named a custom shader. Uniform and sampler
            // handles stay the engine's own: bgfx dedupes uniforms by name across programs, and the
            // contract .sh the custom fragment stage must include declares exactly the same set.
            //
            // A named shader that does not resolve terminates rather than falling back to the engine
            // program: submitFrame validated every one of these before committing surface state, so
            // reaching here means the binding table changed underneath a validated frame.
            bgfx::ProgramHandle batchProgram = sprite2DProgram_;
            const ShaderSlot* batchShaderSlot = nullptr;
            if (shaderDescriptor != nullptr)
            {
                batchShaderSlot =
                    resolveSprite2DShaderSlot(static_cast<u32>(shaderDescriptor->deviceBindingKey));
                if (batchShaderSlot == nullptr)
                {
                    std::terminate();
                }
                batchProgram = batchShaderSlot->program;
            }

            const std::array<float, 4> normalParams{normalMapBound, 0.0F, 0.0F, 0.0F};
            bgfx::setScissor();
            bgfx::setState(kSprite2DPremultipliedAlphaState);
            bgfx::setVertexBuffer(0, &transientVertices, 0, prepared.requirements.vertexCount);
            bgfx::setTexture(0, sprite2DSampler_, texture);
            bgfx::setTexture(1, sprite2DNormalSampler_, normalTexture);
            bgfx::setUniform(sprite2DLightPositionsUniform_, lightPositionsAndRadii.data(),
                             static_cast<u16>(Sprite2DLightingDesc::MaximumPointLightCount));
            bgfx::setUniform(sprite2DLightColorsUniform_, lightColors.data(),
                             static_cast<u16>(Sprite2DLightingDesc::MaximumPointLightCount));
            bgfx::setUniform(sprite2DLightParamsUniform_, lightParams.data());
            bgfx::setUniform(sprite2DNormalParamsUniform_, normalParams.data());
            bgfx::setUniform(sprite2DShadowSegmentsUniform_, shadowSegments.data(),
                             static_cast<u16>(Sprite2DLightingDesc::MaximumShadowSegmentCount));
            if (batchShaderSlot != nullptr)
            {
                const ShaderUniformBindingTable* values = nullptr;
                if (shaderUniformDescriptor != nullptr)
                {
                    const u32 uniformBatchKey = static_cast<u32>(shaderUniformDescriptor->deviceBindingKey);
                    if (const auto binding = shaderUniformBindings_.find(uniformBatchKey);
                        binding != shaderUniformBindings_.end())
                    {
                        values = &binding->second;
                    }
                }
                publishAuthorUniforms(*batchShaderSlot, values);
            }
            const u32 firstIndex = batchBegin * 6U;
            const u32 indexCount = (batchEnd - batchBegin) * 6U;
            bgfx::setIndexBuffer(&transientIndices, firstIndex, indexCount);
            bgfx::submit(kSprite2DView, batchProgram);
            ++statistics_.sprite2DDrawsSubmitted;
            batchBegin = batchEnd;
        }
    }

    [[nodiscard]] Core::Result<GpuShaderId> createShader(const GpuShaderUploadDesc& desc) override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::createShader"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (auto status = validateShaderUploadDesc(desc); !status)
        {
            return Core::failure(std::move(status.error()));
        }

        const std::span<const std::byte> binary = ShaderDetail::selectCustomShaderBinary(desc);
        if (binary.empty())
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "The shader upload contains no binary for the active bgfx renderer");
        }

        bgfx::ShaderHandle vertexShader = BGFX_INVALID_HANDLE;
        // Valid only for kinds that have a skinned vertex stage. When it stays invalid the upload
        // produces a single program, which is what a Sprite2D draw needs.
        bgfx::ShaderHandle skinnedVertexShader = BGFX_INVALID_HANDLE;
        // The contract .sh set for the kind. Subtracted from reflection so what remains is exactly
        // what the author added; these handles are the device's own, and bgfx dedupes uniforms by
        // name globally, so a re-declaration in the custom source resolves to the same handle.
        std::array<bgfx::UniformHandle, 32> engineUniformStorage{};
        usize engineUniformCount = 0;
        switch (desc.shaderKind)
        {
        case GpuShaderKind::Sprite2D:
            vertexShader = sprite2DVertexShader_;
            engineUniformStorage = {sprite2DSampler_,             sprite2DNormalSampler_,
                                    sprite2DLightPositionsUniform_, sprite2DLightColorsUniform_,
                                    sprite2DLightParamsUniform_,  sprite2DNormalParamsUniform_,
                                    sprite2DShadowSegmentsUniform_};
            engineUniformCount = 7;
            break;
        case GpuShaderKind::Mesh3D:
            vertexShader = opaque3DMrVertexShader_;
            // Both Mesh3D vertex stages declare the same varyings, so one fragment binary links
            // against either. Building both here is what lets an author cook a single Mesh3D shader
            // and use it on rigid and skinned geometry alike.
            skinnedVertexShader = opaque3DSkinnedVertexShader_;
            engineUniformStorage = {
                opaque3DSampler_, opaque3DMrSampler_, opaque3DNormalSampler_, opaque3DCsmAtlasSampler_,
                opaque3DIblDiffuseSampler_, opaque3DIblSpecularSampler_, opaque3DIblBrdfSampler_,
                opaque3DSpotShadowMapSampler_, opaque3DPointShadowMapSamplers_[0],
                opaque3DPointShadowMapSamplers_[1], opaque3DPointShadowMapSamplers_[2],
                opaque3DPointShadowMapSamplers_[3], opaque3DPointShadowMapSamplers_[4],
                opaque3DPointShadowMapSamplers_[5], opaque3DLightDirectionsUniform_,
                opaque3DLightColorsUniform_, opaque3DPointLightPositionsUniform_,
                opaque3DPointLightColorsUniform_, opaque3DSpotLightPositionsUniform_,
                opaque3DSpotLightDirectionsUniform_, opaque3DSpotLightColorsUniform_,
                opaque3DMrParamsUniform_, opaque3DNormalParamsUniform_, opaque3DEmissiveFactorUniform_,
                opaque3DCsmMatricesUniform_, opaque3DCsmSplitDepthsUniform_, opaque3DCsmParamsUniform_,
                opaque3DSpotShadowMatrixUniform_, opaque3DSpotShadowParamsUniform_,
                opaque3DPointShadowMatricesUniform_, opaque3DPointShadowParamsUniform_,
                opaque3DIblParamsUniform_};
            engineUniformCount = 32;
            break;
        // Invalid is already refused by the shared validateShaderUploadDesc above, so this is
        // unreachable rather than a second policy: keeping a named failure here would put the
        // decision of which kinds exist in two places that can disagree.
        default:
            return Core::failure(RenderErrorCode::InvalidShaderUpload, "Invalid shader kind");
        }

        auto program = ShaderDetail::createCustomFragmentProgram(
            vertexShader, skinnedVertexShader, binary,
            std::span{engineUniformStorage.data(), engineUniformCount});
        if (!program)
        {
            return Core::failure(std::move(program.error()));
        }

        u32 slotIndex = 0;
        bool reused = false;
        for (u32 index = 0; index < static_cast<u32>(shaders_.size()); ++index)
        {
            if (shaders_[index].identity.canReuse(shaders_[index].live,
                                                  shaders_[index].retirementPhase != RetirementPhase::None))
            {
                slotIndex = index;
                reused = true;
                break;
            }
        }
        if (!reused)
        {
            if (shaders_.size() >= (std::numeric_limits<u32>::max)())
            {
                if (bgfx::isValid(program->skinnedProgram))
                {
                    bgfx::destroy(program->skinnedProgram);
                }
                bgfx::destroy(program->program);
                return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                     "Shader slot table is at maximum capacity");
            }
            slotIndex = static_cast<u32>(shaders_.size());
            shaders_.push_back(ShaderSlot{});
        }

        ShaderSlot& slot = shaders_[slotIndex];
        slot.program = program->program;
        slot.skinnedProgram = program->skinnedProgram;
        slot.shaderKind = desc.shaderKind;
        slot.authorUniforms = std::move(program->authorUniforms);
        // Fresh revision so no binding table can reuse a memo built against whatever program occupied
        // this slot before: the uniform names differ even when the two tables happen to be the same
        // size, and the slot index alone cannot distinguish them.
        slot.authorUniformsRevision = nextShaderUniformRevision_++;
        slot.live = true;
        slot.retirementPhase = RetirementPhase::None;
        ++statistics_.liveResources;
        return GpuShaderId{resourceOwnerId(), slotIndex, slot.identity.value()};
    }

    [[nodiscard]] Core::Status validateShader(GpuShaderId shader) const noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::validateShader"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (!shader || shader.owner != resourceOwnerId() || shader.index >= shaders_.size())
        {
            return Core::failure(RenderErrorCode::ShaderNotFound,
                                 "Shader handle is invalid, stale, or belongs to another device");
        }
        const ShaderSlot& slot = shaders_[shader.index];
        if (!slot.live || slot.identity.value() != shader.generation)
        {
            return Core::failure(RenderErrorCode::ShaderNotFound, "Shader handle is stale or destroyed");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status destroyShader(GpuShaderId shader) noexcept override
    {
        FramePin completionPin;
        return retireShader(shader, completionPin);
    }

    [[nodiscard]] Core::Status retireShader(GpuShaderId shader, FramePin& completionPin) noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::retireShader"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        const auto disposition = RetirementDetail::selectRetirementDisposition(
            retirementMarkerSupported_, completionPin.hasValue());
        if (disposition == RetirementDetail::RetirementDisposition::RejectExternalPin)
        {
            return Core::failure(RenderErrorCode::GpuRetirementUnsupported,
                                 "This bgfx backend cannot retain an external retirement pin");
        }
        if (!shader || shader.owner != resourceOwnerId() || shader.index >= shaders_.size())
        {
            return Core::failure(RenderErrorCode::ShaderNotFound, "Shader handle is invalid");
        }
        ShaderSlot& slot = shaders_[shader.index];
        if (!slot.live || slot.identity.value() != shader.generation)
        {
            return Core::failure(RenderErrorCode::ShaderNotFound, "Shader handle is stale or destroyed");
        }
        slot.live = false;
        slot.identity.advanceAfterRelease();
        for (auto it = shaderBindings_.begin(); it != shaderBindings_.end();)
        {
            if (it->second == shader)
            {
                it = shaderBindings_.erase(it);
            } else
            {
                ++it;
            }
        }
        if (disposition == RetirementDetail::RetirementDisposition::DestroyImmediately)
        {
            if (bgfx::isValid(slot.skinnedProgram))
            {
                bgfx::destroy(slot.skinnedProgram);
                slot.skinnedProgram = BGFX_INVALID_HANDLE;
            }
            if (bgfx::isValid(slot.program))
            {
                bgfx::destroy(slot.program);
                slot.program = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            slot.shaderKind = GpuShaderKind::Invalid;
            slot.authorUniforms.clear();
            slot.authorUniformsRevision = 0;
            slot.retirementPhase = RetirementPhase::None;
            ++statistics_.completedGpuRetirements;
            return Core::success();
        }

        slot.retirementPhase = RetirementPhase::Queued;
        slot.completionPin = std::move(completionPin);
        retirementTimeline_.queue();
        ++statistics_.pendingGpuRetirements;
        return Core::success();
    }

    [[nodiscard]] Core::Status setShaderBinding(u32 deviceBindingKey, GpuShaderId shader) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (deviceBindingKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload, "deviceBindingKey must be non-zero");
        }
        if (!shader)
        {
            shaderBindings_.erase(deviceBindingKey);
            return Core::success();
        }
        if (shader.owner != resourceOwnerId() || shader.index >= shaders_.size() ||
            !shaders_[shader.index].live ||
            shaders_[shader.index].identity.value() != shader.generation)
        {
            return Core::failure(RenderErrorCode::ShaderNotFound, "Shader handle is invalid");
        }
        shaderBindings_[deviceBindingKey] = shader;
        return Core::success();
    }

    [[nodiscard]] Core::Status
    setShaderUniformBinding(u32 deviceBindingKey, const GpuShaderUniformBindingDesc& desc) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (deviceBindingKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload, "deviceBindingKey must be non-zero");
        }
        if (desc.values.empty())
        {
            shaderUniformBindings_.erase(deviceBindingKey);
            return Core::success();
        }
        if (auto status = validateShaderUniformBindingDesc(desc); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        // Copied rather than referenced: the values are re-published every frame the binding is drawn,
        // long after the caller's span has gone.
        ShaderUniformBindingTable& table = shaderUniformBindings_[deviceBindingKey];
        table.values.assign(desc.values.begin(), desc.values.end());
        // Dropped on every write, including a rewrite of the same key with a same-sized table: the
        // names may have moved even when the count did not, and revision 0 never matches a slot.
        table.valueIndices.clear();
        table.cachedAuthorUniformsRevision = 0;
        return Core::success();
    }

    [[nodiscard]] Core::Result<GpuTextureId> createTexture2D(const Texture2DUploadDesc& desc) override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::createTexture2D"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        auto created = createTexture2DUpload(desc);
        if (!created)
        {
            return Core::failure(std::move(created.error()));
        }
        const bgfx::TextureHandle handle = *created;

        const u32 index = acquireTextureSlot();
        TextureSlot& slot = textures_[index];
        slot.handle = handle;
        slot.width = desc.baseWidth();
        slot.height = desc.baseHeight();
        slot.live = true;
        slot.retirementPhase = RetirementPhase::None;
        ++statistics_.liveResources;
        statistics_.uploadedTextureLevels += desc.levels.size();
        return GpuTextureId{resourceOwnerId(), index, slot.identity.value()};
    }

    [[nodiscard]] Core::Status validateTexture2D(GpuTextureId texture) const noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::validateTexture2D"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
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
        FramePin completionPin;
        return retireTexture2D(texture, completionPin);
    }

    [[nodiscard]] Core::Status retireTexture2D(GpuTextureId texture,
                                               FramePin& completionPin) noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::retireTexture2D"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        const auto disposition = RetirementDetail::selectRetirementDisposition(
            retirementMarkerSupported_, completionPin.hasValue());
        if (disposition == RetirementDetail::RetirementDisposition::RejectExternalPin)
        {
            return Core::failure(RenderErrorCode::GpuRetirementUnsupported,
                                 "This bgfx backend cannot retain an external retirement pin");
        }
        if (!texture || texture.owner != resourceOwnerId() || texture.index >= textures_.size())
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        TextureSlot& slot = textures_[texture.index];
        if (!slot.live || slot.identity.value() != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is stale or destroyed");
        }
        slot.live = false;
        slot.identity.advanceAfterRelease();
        for (auto it = texture2DBindings_.begin(); it != texture2DBindings_.end();)
        {
            if (it->second == texture)
            {
                it = texture2DBindings_.erase(it);
            } else
            {
                ++it;
            }
        }
        for (auto& entry : mesh3DMaterialBindings_)
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
        if (disposition == RetirementDetail::RetirementDisposition::DestroyImmediately)
        {
            if (bgfx::isValid(slot.handle))
            {
                bgfx::destroy(slot.handle);
                slot.handle = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            slot.width = 0;
            slot.height = 0;
            slot.retirementPhase = RetirementPhase::None;
            ++statistics_.completedGpuRetirements;
            return Core::success();
        }

        slot.retirementPhase = RetirementPhase::Queued;
        slot.completionPin = std::move(completionPin);
        retirementTimeline_.queue();
        ++statistics_.pendingGpuRetirements;
        return Core::success();
    }

    [[nodiscard]] VideoDecodeCapabilities videoDecodeCapabilities() const noexcept override
    {
        if (stopped_ || !bgfxInitialized_)
        {
            return VideoDecodeCapabilities{};
        }
        return Bgfx::readVideoDecodeCapabilities();
    }

    [[nodiscard]] bool isVideoDecodeSupported(const VideoDecodeTextureDesc& desc) const noexcept override
    {
        if (stopped_ || !bgfxInitialized_)
        {
            return false;
        }
        if (auto status = validateVideoDecodeTextureDesc(desc); !status)
        {
            return false;
        }
        return Bgfx::isVideoDecodeSupported(desc);
    }

    [[nodiscard]] Core::Result<GpuTextureId> createVideoDecodeTexture(
        const VideoDecodeTextureDesc& desc) override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::createVideoDecodeTexture"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        auto created = Bgfx::createVideoDecodeTexture(desc);
        if (!created)
        {
            return Core::failure(std::move(created.error()));
        }

        const u32 index = acquireTextureSlot();
        TextureSlot& slot = textures_[index];
        slot.handle = *created;
        slot.width = desc.codedWidth;
        slot.height = desc.codedHeight;
        slot.live = true;
        slot.videoDecode = true;
        slot.retirementPhase = RetirementPhase::None;
        ++statistics_.liveResources;
        // uploadedTextureLevels counts CPU-side pixel uploads. A decode target never
        // receives any, so leaving it untouched keeps the upload budget honest.
        return GpuTextureId{resourceOwnerId(), index, slot.identity.value()};
    }

    [[nodiscard]] Core::Status submitVideoDecodeFrame(
        GpuTextureId texture, const VideoDecodeSubmission& submission) noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::submitVideoDecodeFrame"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (!texture || !isLiveTexture(texture))
        {
            return Core::failure(RenderErrorCode::TextureNotFound,
                                 "Texture2D handle is invalid, stale, or belongs to another device");
        }
        const TextureSlot& slot = textures_[texture.index];
        if (!slot.videoDecode)
        {
            return Core::failure(RenderErrorCode::VideoDecodeTextureNotFound,
                                 "Texture2D handle does not refer to a video decode target");
        }
        if (auto status = validateVideoDecodeSubmission(submission); !status)
        {
            return status;
        }
        return Bgfx::submitVideoDecodeFrame(slot.handle, slot.width, slot.height, submission);
    }

    [[nodiscard]] Core::Result<GpuEnvironmentMapId>
    createEnvironmentMap(const EnvironmentMapUploadDesc& desc) override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::createEnvironmentMap"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        auto nativeResources = createEnvironmentMapResources(desc);
        if (!nativeResources)
        {
            return Core::failure(std::move(nativeResources.error()));
        }

        u32 slotIndex = (std::numeric_limits<u32>::max)();
        for (u32 index = 0; index < static_cast<u32>(environmentMaps_.size()); ++index)
        {
            if (environmentMaps_[index].identity.canReuse(
                    environmentMaps_[index].live,
                    environmentMaps_[index].retirementPhase != RetirementPhase::None))
            {
                slotIndex = index;
                break;
            }
        }
        if (slotIndex == (std::numeric_limits<u32>::max)())
        {
            if (environmentMaps_.size() >= (std::numeric_limits<u32>::max)())
            {
                destroyEnvironmentMapResources(*nativeResources);
                return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                     "EnvironmentMap GPU slot index space is exhausted");
            }
            slotIndex = static_cast<u32>(environmentMaps_.size());
            try
            {
                environmentMaps_.push_back(EnvironmentMapSlot{});
            }
            catch (const std::bad_alloc&)
            {
                destroyEnvironmentMapResources(*nativeResources);
                return Core::failure(Core::CoreErrorCode::OutOfMemory);
            }
            catch (const std::length_error&)
            {
                destroyEnvironmentMapResources(*nativeResources);
                return Core::failure(Core::CoreErrorCode::CapacityExceeded);
            }
        }

        EnvironmentMapSlot& slot = environmentMaps_[slotIndex];
        slot.resources = *nativeResources;
        slot.specularMipCount = desc.specularMipCount;
        slot.live = true;
        slot.retirementPhase = RetirementPhase::None;
        statistics_.liveResources += BgfxEnvironmentMapNativeTextureCount;
        return GpuEnvironmentMapId{resourceOwnerId(), slotIndex, slot.identity.value()};
    }

    [[nodiscard]] Core::Status
    validateEnvironmentMap(GpuEnvironmentMapId environmentMap) const noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::validateEnvironmentMap"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (!isLiveEnvironmentMap(environmentMap))
        {
            return Core::failure(RenderErrorCode::EnvironmentMapNotFound,
                                 "EnvironmentMap handle is invalid, stale, or belongs to another device");
        }
        return Core::success();
    }

    [[nodiscard]] Core::Status
    destroyEnvironmentMap(GpuEnvironmentMapId environmentMap) noexcept override
    {
        FramePin completionPin;
        return retireEnvironmentMap(environmentMap, completionPin);
    }

    [[nodiscard]] Core::Status retireEnvironmentMap(
        GpuEnvironmentMapId environmentMap, FramePin& completionPin) noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::retireEnvironmentMap"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        const auto disposition = RetirementDetail::selectRetirementDisposition(
            retirementMarkerSupported_, completionPin.hasValue());
        if (disposition == RetirementDetail::RetirementDisposition::RejectExternalPin)
        {
            return Core::failure(RenderErrorCode::GpuRetirementUnsupported,
                                 "This bgfx backend cannot retain an external retirement pin");
        }
        if (!isLiveEnvironmentMap(environmentMap))
        {
            return Core::failure(RenderErrorCode::EnvironmentMapNotFound,
                                 "EnvironmentMap handle is invalid, stale, or destroyed");
        }

        EnvironmentMapSlot& slot = environmentMaps_[environmentMap.index];
        slot.live = false;
        slot.identity.advanceAfterRelease();
        if (mesh3DImageBasedLighting_.has_value() &&
            mesh3DImageBasedLighting_->environmentMap == environmentMap)
        {
            mesh3DImageBasedLighting_.reset();
        }

        if (disposition == RetirementDetail::RetirementDisposition::DestroyImmediately)
        {
            destroyEnvironmentMapNativeResources(slot.resources);
            slot.specularMipCount = 0;
            slot.retirementPhase = RetirementPhase::None;
            ++statistics_.completedGpuRetirements;
            return Core::success();
        }

        slot.retirementPhase = RetirementPhase::Queued;
        slot.completionPin = std::move(completionPin);
        retirementTimeline_.queue();
        ++statistics_.pendingGpuRetirements;
        return Core::success();
    }

    [[nodiscard]] Core::Status setTexture2DBinding(u32 deviceBindingKey,
                                                   GpuTextureId texture) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (deviceBindingKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload,
                                 "Sprite2D device binding key must be non-zero");
        }
        if (!texture)
        {
            texture2DBindings_.erase(deviceBindingKey);
            return Core::success();
        }
        if (!isLiveTexture(texture))
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        texture2DBindings_[deviceBindingKey] = texture;
        return Core::success();
    }

    [[nodiscard]] Core::Result<GpuMeshId> createStaticMesh(const StaticMeshUploadDesc& desc) override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::createStaticMesh"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }

        constexpr u32 MaxUploadBytes = (std::numeric_limits<u32>::max)();
        constexpr usize FloatsPerVertex = 12U;
        constexpr usize vertexStrideBytes = FloatsPerVertex * sizeof(float);
        if (desc.vertexCount == 0 || desc.indexCount == 0 ||
            (desc.indexCount % 3U) != 0U ||
            desc.vertexCount > (std::numeric_limits<usize>::max)() / FloatsPerVertex ||
            desc.vertices.size() != static_cast<usize>(desc.vertexCount) * FloatsPerVertex ||
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
        for (usize vertexIndex = 0; vertexIndex < desc.vertexCount; ++vertexIndex)
        {
            const usize tangentOffset = vertexIndex * FloatsPerVertex + 6U;
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

        const u32 vertexBytes = static_cast<u32>(static_cast<usize>(desc.vertexCount) * vertexStrideBytes);
        const u32 indexBytes = desc.indexCount * static_cast<u32>(sizeof(u16));
        const bgfx::Memory* vertexMemory = bgfx::copy(desc.vertices.data(), vertexBytes);
        const bgfx::VertexBufferHandle vb = bgfx::createVertexBuffer(vertexMemory, opaque3DVertexLayout_);
        if (!bgfx::isValid(vb))
        {
            return Core::failure(RenderErrorCode::InvalidMeshUpload, "bgfx rejected StaticMesh vertex buffer");
        }
        const bgfx::Memory* indexMemory = bgfx::copy(desc.indices.data(), indexBytes);
        const bgfx::IndexBufferHandle ib = bgfx::createIndexBuffer(indexMemory);
        if (!bgfx::isValid(ib))
        {
            bgfx::destroy(vb);
            return Core::failure(RenderErrorCode::InvalidMeshUpload, "bgfx rejected StaticMesh index buffer");
        }

        u32 slotIndex = (std::numeric_limits<u32>::max)();
        for (u32 index = 0; index < static_cast<u32>(meshes_.size()); ++index)
        {
            if (meshes_[index].identity.canReuse(
                    meshes_[index].live,
                    meshes_[index].retirementPhase != RetirementPhase::None))
            {
                slotIndex = index;
                break;
            }
        }
        if (slotIndex == (std::numeric_limits<u32>::max)())
        {
            if (meshes_.size() >= (std::numeric_limits<u32>::max)())
            {
                bgfx::destroy(ib);
                bgfx::destroy(vb);
                return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                     "StaticMesh GPU slot index space is exhausted");
            }
            slotIndex = static_cast<u32>(meshes_.size());
            try
            {
                meshes_.push_back(MeshSlot{});
            }
            catch (const std::bad_alloc&)
            {
                bgfx::destroy(ib);
                bgfx::destroy(vb);
                return Core::failure(Core::CoreErrorCode::OutOfMemory);
            }
            catch (const std::length_error&)
            {
                bgfx::destroy(ib);
                bgfx::destroy(vb);
                return Core::failure(Core::CoreErrorCode::CapacityExceeded);
            }
        }

        MeshSlot& slot = meshes_[slotIndex];
        slot.vertexBuffer = vb;
        slot.indexBuffer = ib;
        slot.skinVertexBuffer = BGFX_INVALID_HANDLE;
        slot.vertexCount = desc.vertexCount;
        slot.indexCount = desc.indexCount;
        slot.jointCount = 0;
        slot.skinned = false;
        slot.live = true;
        slot.retirementPhase = RetirementPhase::None;
        ++statistics_.liveResources;
        ++statistics_.liveResources;
        return GpuMeshId{resourceOwnerId(), slotIndex, slot.identity.value()};
    }

    [[nodiscard]] Core::Result<GpuMeshId> createSkinnedMesh(const SkinnedMeshUploadDesc& desc) override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::createSkinnedMesh"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        constexpr u16 MaxJointCount = static_cast<u16>(MaxSkinnedMesh3DPaletteJointCount);
        constexpr u32 InfluencesPerVertex = 4;
        if (desc.jointCount == 0 || desc.jointCount > MaxJointCount ||
            desc.vertexCount > (std::numeric_limits<u32>::max)() / InfluencesPerVertex ||
            desc.jointIndices.size() != static_cast<usize>(desc.vertexCount) * InfluencesPerVertex ||
            desc.jointWeights.size() != static_cast<usize>(desc.vertexCount) * InfluencesPerVertex)
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
        for (usize vertexIndex = 0; vertexIndex < desc.vertexCount; ++vertexIndex)
        {
            u32 weightSum = 0;
            for (usize influence = 0; influence < InfluencesPerVertex; ++influence)
            {
                weightSum += desc.jointWeights[vertexIndex * InfluencesPerVertex + influence];
            }
            if (weightSum != 0xFFFFU)
            {
                return Core::failure(RenderErrorCode::InvalidMeshUpload,
                                     "SkinnedMesh vertex weights must sum to 0xFFFF");
            }
        }

        auto meshId = createStaticMesh(StaticMeshUploadDesc{
            .vertexCount = desc.vertexCount,
            .indexCount = desc.indexCount,
            .vertices = desc.vertices,
            .indices = desc.indices,
        });
        if (!meshId)
        {
            return meshId;
        }

        const bgfx::Memory* skinMemory = bgfx::alloc(
            static_cast<u32>(static_cast<usize>(desc.vertexCount) * sizeof(BgfxOpaque3DSkinVertex)));
        auto* skinVertices = reinterpret_cast<BgfxOpaque3DSkinVertex*>(skinMemory->data);
        constexpr float WeightScale = 1.0F / 65535.0F;
        for (usize vertexIndex = 0; vertexIndex < desc.vertexCount; ++vertexIndex)
        {
            BgfxOpaque3DSkinVertex& vertex = skinVertices[vertexIndex];
            for (usize influence = 0; influence < InfluencesPerVertex; ++influence)
            {
                const usize source = vertexIndex * InfluencesPerVertex + influence;
                vertex.jointIndices[influence] = static_cast<u8>(desc.jointIndices[source]);
                vertex.jointWeights[influence] =
                    static_cast<float>(desc.jointWeights[source]) * WeightScale;
            }
        }
        const bgfx::VertexBufferHandle skinBuffer =
            bgfx::createVertexBuffer(skinMemory, opaque3DSkinVertexLayout_);
        if (!bgfx::isValid(skinBuffer))
        {
            (void)destroyGpuMesh(*meshId);
            return Core::failure(RenderErrorCode::InvalidMeshUpload,
                                 "bgfx rejected the SkinnedMesh skin vertex buffer");
        }

        MeshSlot& slot = meshes_[meshId->index];
        slot.skinVertexBuffer = skinBuffer;
        slot.jointCount = desc.jointCount;
        slot.skinned = true;
        ++statistics_.liveResources;
        return meshId;
    }

    [[nodiscard]] Core::Status destroyGpuMesh(GpuMeshId mesh) noexcept override
    {
        FramePin completionPin;
        return retireGpuMesh(mesh, completionPin);
    }

    [[nodiscard]] Core::Status retireGpuMesh(GpuMeshId mesh,
                                             FramePin& completionPin) noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::retireGpuMesh"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        const auto disposition = RetirementDetail::selectRetirementDisposition(
            retirementMarkerSupported_, completionPin.hasValue());
        if (disposition == RetirementDetail::RetirementDisposition::RejectExternalPin)
        {
            return Core::failure(RenderErrorCode::GpuRetirementUnsupported,
                                 "This bgfx backend cannot retain an external retirement pin");
        }
        if (!mesh || mesh.owner != resourceOwnerId() || mesh.index >= meshes_.size())
        {
            return Core::failure(RenderErrorCode::MeshNotFound, "GPU mesh handle is invalid");
        }
        MeshSlot& slot = meshes_[mesh.index];
        if (!slot.live || slot.identity.value() != mesh.generation)
        {
            return Core::failure(RenderErrorCode::MeshNotFound,
                                 "GPU mesh handle is stale or destroyed");
        }
        slot.live = false;
        slot.identity.advanceAfterRelease();
        for (auto it = mesh3DBindings_.begin(); it != mesh3DBindings_.end();)
        {
            if (it->second == mesh)
            {
                it = mesh3DBindings_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        if (disposition == RetirementDetail::RetirementDisposition::DestroyImmediately)
        {
            if (bgfx::isValid(slot.vertexBuffer))
            {
                bgfx::destroy(slot.vertexBuffer);
                slot.vertexBuffer = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(slot.skinVertexBuffer))
            {
                bgfx::destroy(slot.skinVertexBuffer);
                slot.skinVertexBuffer = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(slot.indexBuffer))
            {
                bgfx::destroy(slot.indexBuffer);
                slot.indexBuffer = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            slot.vertexCount = 0;
            slot.indexCount = 0;
            slot.jointCount = 0;
            slot.skinned = false;
            slot.retirementPhase = RetirementPhase::None;
            ++statistics_.completedGpuRetirements;
            return Core::success();
        }

        slot.retirementPhase = RetirementPhase::Queued;
        slot.completionPin = std::move(completionPin);
        retirementTimeline_.queue();
        ++statistics_.pendingGpuRetirements;
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DBinding(u32 meshKey, GpuMeshId mesh) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (meshKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidMeshUpload, "meshKey must be non-zero");
        }
        if (!mesh)
        {
            mesh3DBindings_.erase(meshKey);
            return Core::success();
        }
        if (mesh.owner != resourceOwnerId() || mesh.index >= meshes_.size() ||
            !meshes_[mesh.index].live ||
            meshes_[mesh.index].identity.value() != mesh.generation)
        {
            return Core::failure(RenderErrorCode::MeshNotFound, "GPU mesh handle is invalid");
        }
        mesh3DBindings_[meshKey] = mesh;
        return Core::success();
    }

    [[nodiscard]] bool isLiveTexture(GpuTextureId texture) const noexcept
    {
        return !texture || (texture.owner == resourceOwnerId() && texture.index < textures_.size() &&
                            textures_[texture.index].live &&
                            textures_[texture.index].identity.value() == texture.generation);
    }

    // Claims a reusable slot or grows the table. Shared by uploaded textures and
    // video decode targets: both occupy the same table so that one destroy path,
    // one retirement path and one binding namespace cover them.
    [[nodiscard]] u32 acquireTextureSlot()
    {
        for (u32 slotIndex = 0; slotIndex < static_cast<u32>(textures_.size()); ++slotIndex)
        {
            if (textures_[slotIndex].identity.canReuse(
                    textures_[slotIndex].live,
                    textures_[slotIndex].retirementPhase != RetirementPhase::None))
            {
                // A reused slot carries the previous resource's kind, so clear it here:
                // the caller sets it only when the new resource is a decode target.
                textures_[slotIndex].videoDecode = false;
                return slotIndex;
            }
        }
        const auto index = static_cast<u32>(textures_.size());
        textures_.push_back(TextureSlot{});
        return index;
    }

    [[nodiscard]] bool isLiveEnvironmentMap(GpuEnvironmentMapId environmentMap) const noexcept
    {
        if (!environmentMap || environmentMap.owner != resourceOwnerId() ||
            environmentMap.index >= environmentMaps_.size())
        {
            return false;
        }
        const EnvironmentMapSlot& slot = environmentMaps_[environmentMap.index];
        return slot.live && slot.identity.value() == environmentMap.generation &&
               slot.resources.valid();
    }

    void destroyEnvironmentMapNativeResources(BgfxEnvironmentMapResources& resources) noexcept
    {
        if (!resources.valid())
        {
            std::terminate();
        }
        destroyEnvironmentMapResources(resources);
        statistics_.liveResources -= BgfxEnvironmentMapNativeTextureCount;
    }

    [[nodiscard]] Mesh3DMaterialBindingDesc materialBindingOrDefault(u32 materialKey) const noexcept
    {
        const auto existing = mesh3DMaterialBindings_.find(materialKey);
        return existing == mesh3DMaterialBindings_.end() ? Mesh3DMaterialBindingDesc{} : existing->second;
    }

    [[nodiscard]] Core::Status commitMaterialBinding(
        u32 materialKey, const Mesh3DMaterialBindingDesc& binding) noexcept
    {
        try
        {
            mesh3DMaterialBindings_.insert_or_assign(materialKey, binding);
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

    [[nodiscard]] Core::Status setMesh3DMaterialBinding(
        u32 materialKey, const Mesh3DMaterialBindingDesc& desc) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }
        // Emissive is checked for finite and non-negative but not for an upper bound: it is
        // radiance, on the same scale as light colour, not a [0,1] BRDF parameter (ADR 0043).
        const auto emissiveValid = [](float value) noexcept {
            return std::isfinite(value) && value >= 0.0F;
        };
        if (!(desc.metallicFactor >= 0.0F && desc.metallicFactor <= 1.0F) ||
            !(desc.roughnessFactor >= 0.0F && desc.roughnessFactor <= 1.0F) ||
            !std::isfinite(desc.metallicFactor) || !std::isfinite(desc.roughnessFactor) ||
            !emissiveValid(desc.emissiveFactorR) || !emissiveValid(desc.emissiveFactorG) ||
            !emissiveValid(desc.emissiveFactorB) || !isSupportedMesh3DAlphaMode(desc.alphaMode))
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
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }

        mesh3DMaterialBindings_.erase(materialKey);
        return Core::success();
    }

    // M11-E5: bind baseColor texture for Mesh3D materialKey (0 clears).
    [[nodiscard]] Core::Status setMesh3DMaterialTextureBinding(u32 materialKey, GpuTextureId texture) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }
        if (!texture)
        {
            const auto existing = mesh3DMaterialBindings_.find(materialKey);
            if (existing == mesh3DMaterialBindings_.end())
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

    // RENDER-001: bind optional metallic-roughness texture (G=roughness, B=metallic).
    [[nodiscard]] Core::Status setMesh3DMaterialMetallicRoughnessTextureBinding(
        u32 materialKey, GpuTextureId texture) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }
        if (!texture)
        {
            const auto existing = mesh3DMaterialBindings_.find(materialKey);
            if (existing == mesh3DMaterialBindings_.end())
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

    [[nodiscard]] Core::Status setMesh3DMaterialFactors(u32 materialKey, float metallic,
                                                        float roughness) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
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
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (materialKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "materialKey must be non-zero");
        }
        if (!texture)
        {
            const auto existing = mesh3DMaterialBindings_.find(materialKey);
            if (existing == mesh3DMaterialBindings_.end())
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
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (auto status = validateMesh3DLightingDesc(lighting); !status)
        {
            return status;
        }

        encodeMesh3DLighting(lighting, mesh3DLightDirections_, mesh3DLightColors_,
                             mesh3DPointLightPositionsAndRadii_, mesh3DPointLightColors_,
                             mesh3DSpotLightPositionsAndRadii_, mesh3DSpotLightDirectionsAndInnerCosines_,
                             mesh3DSpotLightColorsAndOuterCosines_,
                             mesh3DAmbientScale_);
        return Core::success();
    }

    [[nodiscard]] Core::Status setMesh3DImageBasedLighting(
        const Mesh3DImageBasedLightingDesc& lighting) noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::setMesh3DImageBasedLighting"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (!std::isfinite(lighting.intensity) || lighting.intensity < 0.0F ||
            !std::isfinite(lighting.rotationRadians))
        {
            return Core::failure(RenderErrorCode::InvalidEnvironmentMapUpload,
                                 "Mesh3D IBL intensity must be non-negative and all scalars finite");
        }
        if (!isLiveEnvironmentMap(lighting.environmentMap))
        {
            return Core::failure(RenderErrorCode::EnvironmentMapNotFound,
                                 "Mesh3D IBL requires a live EnvironmentMap from this device");
        }
        mesh3DImageBasedLighting_ = lighting;
        return Core::success();
    }

    [[nodiscard]] Core::Status clearMesh3DImageBasedLighting() noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::clearMesh3DImageBasedLighting"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        mesh3DImageBasedLighting_.reset();
        return Core::success();
    }

    // M11-D1: capture primary backbuffer after present (frame must not be open).
    [[nodiscard]] Core::Result<Rgba8FrameCapture> capturePrimaryFrameRgba8() override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::capturePrimaryFrameRgba8"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (frameOpen_)
        {
            return Core::failure(RenderErrorCode::FrameAlreadyOpen,
                                 "Cannot capture while a bgfx frame is open; present first");
        }
        if (captureInFlight_)
        {
            return Core::failure(RenderErrorCode::FrameCaptureBusy, "A bgfx frame capture is already in progress");
        }

        captureInFlight_ = true;
        captureCallback_.beginCapture();
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "tina-primary-frame");
        awaitCaptureDelivery();
        captureInFlight_ = false;
        if (!captureCallback_.isReady())
        {
            return Core::failure(RenderErrorCode::FrameCaptureFailed,
                                 "bgfx did not deliver a screenshot within the bounded 120-frame capture wait");
        }
        return captureCallback_.take();
    }

    [[nodiscard]] Core::Status requestPrimaryFrameCaptureOnNextPresent() override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::requestPrimaryFrameCaptureOnNextPresent");
            !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (captureInFlight_)
        {
            return Core::failure(RenderErrorCode::FrameCaptureBusy,
                                 "A bgfx frame capture is already in progress");
        }
        captureArmed_ = true;
        return Core::success();
    }

    [[nodiscard]] Core::Result<Rgba8FrameCapture> collectPrimaryFrameCapture() override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::collectPrimaryFrameCapture"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (captureArmed_)
        {
            captureArmed_ = false;
            return Core::failure(RenderErrorCode::FrameCaptureFailed,
                                 "The armed primary frame capture was never presented");
        }
        if (!captureCallback_.isReady())
        {
            return Core::failure(RenderErrorCode::FrameCaptureFailed,
                                 "No primary frame capture was armed, or bgfx did not deliver it");
        }
        return captureCallback_.take();
    }

    // Pumps bgfx until the screenshot callback publishes, within a bounded budget. The
    // frames it pumps carry no new content, so they cannot change what was captured:
    // the screenshot was already attached to an earlier frame.
    void awaitCaptureDelivery() noexcept
    {
        for (int frameIndex = 0;
             frameIndex < PrimaryFrameCaptureDeliveryFrameBudget && !captureCallback_.isReady();
             ++frameIndex)
        {
            submitRetirementMarkerIfNeeded();
            const u32 currentFrame = bgfx::frame();
            completeRetirementsThrough(currentFrame);
            if (!captureCallback_.isReady())
            {
                // requestScreenShot completes on bgfx's render thread. A short bounded pause
                // prevents a fast owner-thread loop from exhausting the frame budget before
                // a busy Windows compositor/GPU can publish the callback.
                std::this_thread::sleep_for(PrimaryFrameCapturePollDelay);
            }
        }
    }

    [[nodiscard]] Core::Status syncUIGlyphAtlas(
        const std::optional<UIGlyphAtlasPageView>& atlas) noexcept
    {
        if (!atlas.has_value() || atlas->pixels.empty() || atlas->width == 0 || atlas->height == 0)
        {
            return Core::success();
        }
        if (bgfx::isValid(uiGlyphAtlasTexture_)
            && (uiGlyphAtlasPageSize_.width != atlas->width
                || uiGlyphAtlasPageSize_.height != atlas->height))
        {
            bgfx::destroy(uiGlyphAtlasTexture_);
            uiGlyphAtlasTexture_ = BGFX_INVALID_HANDLE;
            uiGlyphAtlasPageSize_ = {};
            uiGlyphAtlasUploadedPageRevision_ = 0;
            --statistics_.liveResources;
        }
        if (!bgfx::isValid(uiGlyphAtlasTexture_))
        {
            auto created =
                createUIGlyphAtlasTexture(atlas->width, atlas->height, atlas->pixels);
            if (!created)
            {
                return Core::failure(std::move(created.error()));
            }
            uiGlyphAtlasTexture_ = *created;
            uiGlyphAtlasPageSize_ =
                UIAtlasPageSize{.width = atlas->width, .height = atlas->height};
            uiGlyphAtlasUploadedPageRevision_ = atlas->pageRevision;
            ++statistics_.liveResources;
            return Core::success();
        }
        // The page is allocated at full size and handed to us every frame, so
        // without this check a full 512x512 R8 copy is queued each frame even
        // when no glyph was rasterized. Revision zero means "unknown".
        if (atlas->pageRevision != 0
            && atlas->pageRevision == uiGlyphAtlasUploadedPageRevision_)
        {
            return Core::success();
        }
        if (auto status = updateUIGlyphAtlasTexture(
                uiGlyphAtlasTexture_, atlas->width, atlas->height, atlas->pixels);
            !status)
        {
            return status;
        }
        uiGlyphAtlasUploadedPageRevision_ = atlas->pageRevision;
        return Core::success();
    }

    [[nodiscard]] Core::Status preflightUIImageBindings(
        UIDisplayListView displayList, FrameResourceTableView resources) const noexcept
    {
        for (const UIDrawCommand& command : displayList.commands())
        {
            if (command.kind != UIDrawCommandKind::ImageQuad)
            {
                continue;
            }
            const FrameResourceDescriptor* descriptor =
                resources.resolve(command.texture, FrameResourceKind::Texture2D);
            if (descriptor == nullptr ||
                descriptor->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
            {
                return Core::failure(RenderErrorCode::InvalidFrameResource,
                                     "A bgfx UI ImageQuad references an invalid Texture2D frame resource");
            }
            const auto binding = texture2DBindings_.find(static_cast<u32>(descriptor->deviceBindingKey));
            if (binding == texture2DBindings_.end() || !binding->second ||
                binding->second.index >= textures_.size())
            {
                return Core::failure(RenderErrorCode::InvalidFrameResource,
                                     "A bgfx UI ImageQuad Texture2D binding is missing");
            }
            const TextureSlot& slot = textures_[binding->second.index];
            if (!slot.live || slot.identity.value() != binding->second.generation ||
                !bgfx::isValid(slot.handle))
            {
                return Core::failure(RenderErrorCode::InvalidFrameResource,
                                     "A bgfx UI ImageQuad Texture2D binding is not live");
            }
        }
        return Core::success();
    }

    void submitUI(const RenderSurfaceState& surface, UIDisplayListView displayList,
                  FrameResourceTableView resources, PreparedUIDisplayList prepared) noexcept
    {
        if (prepared.vertexCount == 0)
        {
            return;
        }

        // DisplayList Glyph commands use atlasPage 0 for the context-owned R8
        // page. Solid quads sample the 1x1 white texture regardless of page.
        BgfxUIAtlasPageTable atlasPages{};
        if (bgfx::isValid(uiGlyphAtlasTexture_))
        {
            atlasPages.pages[0] = uiGlyphAtlasPageSize_;
            atlasPages.pageCount = 1;
        }
        else
        {
            // Solid-only frames: still provide a 1x1 page so accidental Glyph
            // commands fail cleanly rather than divide by zero.
            atlasPages.pages[0] = UIAtlasPageSize{.width = 1, .height = 1};
            atlasPages.pageCount = 1;
        }

        bgfx::TransientVertexBuffer transientVertices{};
        bgfx::TransientIndexBuffer transientIndices{};
        bgfx::allocTransientVertexBuffer(&transientVertices, prepared.vertexCount, uiVertexLayout_);
        bgfx::allocTransientIndexBuffer(&transientIndices, prepared.indexCount, true);

        auto vertices = std::span{reinterpret_cast<BgfxUIDisplayVertex*>(transientVertices.data),
                                  static_cast<usize>(prepared.vertexCount)};
        auto indices =
            std::span{reinterpret_cast<u32*>(transientIndices.data), static_cast<usize>(prepared.indexCount)};
        auto written = writeGeometry(displayList, vertices, indices, atlasPages);
        if (!written || written->vertexCount != prepared.vertexCount || written->indexCount != prepared.indexCount)
        {
            // Preflight validated the same immutable submit-call borrow and exact
            // output extents before surface commit. A mismatch here is an internal
            // invariant violation, not a recoverable frame failure.
            std::terminate();
        }

        for (const UIDrawBatch& batch : displayList.batches())
        {
            if (batch.clip.hasClip())
            {
                const UIPixelRect* const clip = displayList.resolveClip(batch.clip);
                if (clip == nullptr)
                {
                    std::terminate();
                }
                const BgfxScissorRect scissor = clampScissorToSurface(*clip, surface.framebufferExtent);
                if (scissor.empty())
                {
                    continue;
                }
                bgfx::setScissor(scissor.x, scissor.y, scissor.width, scissor.height);
            } else
            {
                bgfx::setScissor();
            }

            bgfx::TextureHandle texture = uiSolidWhiteTexture_;
            bgfx::ProgramHandle program = uiCoverageProgram_;
            u32 samplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIP_POINT;
            if (batch.kind == UIDrawCommandKind::Glyph)
            {
                if (batch.atlasPage == 0 && bgfx::isValid(uiGlyphAtlasTexture_))
                {
                    texture = uiGlyphAtlasTexture_;
                }
                else if (!bgfx::isValid(uiSolidWhiteTexture_))
                {
                    continue;
                }
            }
            else if (batch.kind == UIDrawCommandKind::ImageQuad)
            {
                const FrameResourceDescriptor* descriptor =
                    resources.resolve(batch.texture, FrameResourceKind::Texture2D);
                if (descriptor == nullptr ||
                    descriptor->deviceBindingKey > static_cast<u64>((std::numeric_limits<u32>::max)()))
                {
                    std::terminate();
                }
                const auto binding = texture2DBindings_.find(static_cast<u32>(descriptor->deviceBindingKey));
                if (binding == texture2DBindings_.end() || !binding->second ||
                    binding->second.index >= textures_.size())
                {
                    std::terminate();
                }
                const TextureSlot& slot = textures_[binding->second.index];
                if (!slot.live || slot.identity.value() != binding->second.generation ||
                    !bgfx::isValid(slot.handle))
                {
                    std::terminate();
                }
                texture = slot.handle;
                program = uiImageQuadProgram_;
                if (batch.sampling == UITextureSampling::Nearest)
                {
                    samplerFlags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
                }
            }

            const u32 firstIndex = batch.firstCommand * static_cast<u32>(kIndicesPerSolidQuad);
            const u32 indexCount = batch.commandCount * static_cast<u32>(kIndicesPerSolidQuad);
            bgfx::setState(kUIPremultipliedAlphaState);
            bgfx::setTexture(0, uiTexColorUniform_, texture, samplerFlags);
            bgfx::setVertexBuffer(0, &transientVertices, 0, prepared.vertexCount);
            bgfx::setIndexBuffer(&transientIndices, firstIndex, indexCount);
            bgfx::submit(kUIView, program);
        }
    }

    void submitPrimaryFrame(const RenderSurfaceState& surface, RenderSceneView scene,
                            FrameResourceTableView resources, PreparedOpaque3D preparedOpaque3D,
                            PreparedSprite2D preparedSprite2D, UIDisplayListView displayList,
                            PreparedUIDisplayList preparedUI, const RenderPassSchedule& schedule) noexcept
    {
        bgfx::InstanceDataBuffer opaque3DInstanceBuffer{};
        prepareOpaque3DInstanceBuffer(scene, resources, preparedOpaque3D,
                                      opaque3DInstanceBuffer);

        // Encoded once per frame rather than per pass: only one pass owns the clear, but
        // every candidate is configured, and all of them must agree on the colour.
        const u32 clearRgba = packClearRgba(scene.clearColor());

        for (const RenderPassPlan& pass : schedule.passes())
        {
            const auto requireResource = [&pass](RenderPassResource expected) noexcept {
                if (pass.resource != expected)
                {
                    std::terminate();
                }
            };
            switch (pass.kind)
            {
            case RenderPassKind::Clear:
                requireResource(RenderPassResource::PrimarySurface);
                configureSurfaceClearView(surface, clearRgba);
                break;
            case RenderPassKind::CascadedDirectionalShadowDepth:
            {
                requireResource(RenderPassResource::DirectionalShadowAtlas);
                if (pass.clearColor || !pass.clearDepth ||
                    !preparedOpaque3D.cascadedDirectionalShadow.has_value() ||
                    pass.cascadeIndex >= BgfxCascadedDirectionalShadowCascadeCount)
                {
                    std::terminate();
                }
                const usize cascadeIndex = static_cast<usize>(pass.cascadeIndex);
                configureCascadedDirectionalShadowView(
                    *preparedOpaque3D.cascadedDirectionalShadow,
                    cascadeIndex, pass.clearDepth);
                submitCascadedDirectionalShadowDepth(
                    scene, resources, preparedOpaque3D, opaque3DInstanceBuffer,
                    cascadeIndex);
                break;
            }
            case RenderPassKind::SpotLightShadowDepth:
                requireResource(RenderPassResource::SpotLightShadowMap);
                if (pass.clearColor || !pass.clearDepth ||
                    !preparedOpaque3D.spotLightShadow.has_value())
                {
                    std::terminate();
                }
                configureSpotLightShadowView(
                    *preparedOpaque3D.spotLightShadow, pass.clearDepth);
                submitSpotLightShadowDepth(
                    scene, resources, preparedOpaque3D, opaque3DInstanceBuffer);
                break;
            case RenderPassKind::PointLightShadowDepth:
            {
                requireResource(RenderPassResource::PointLightShadowMap);
                if (pass.clearColor || !pass.clearDepth ||
                    !preparedOpaque3D.pointLightShadow.has_value() ||
                    pass.faceIndex >= BgfxPointLightShadowFaceCount)
                {
                    std::terminate();
                }
                const usize faceIndex = static_cast<usize>(pass.faceIndex);
                configurePointLightShadowView(*preparedOpaque3D.pointLightShadow,
                                              faceIndex, pass.clearDepth);
                submitPointLightShadowDepth(scene, resources, preparedOpaque3D,
                                            opaque3DInstanceBuffer, faceIndex);
                break;
            }
            case RenderPassKind::Opaque3D:
                requireResource(RenderPassResource::PrimarySurface);
                configureMesh3DView(kOpaque3DView, surface, *scene.perspectiveCamera(),
                                    pass.clearColor, pass.clearDepth, clearRgba);
                submitMesh3D(scene, resources, preparedOpaque3D,
                             opaque3DInstanceBuffer, false);
                break;
            case RenderPassKind::Transparent3D:
                requireResource(RenderPassResource::PrimarySurface);
                configureMesh3DView(kTransparent3DView, surface,
                                    *scene.perspectiveCamera(), pass.clearColor,
                                    pass.clearDepth, clearRgba);
                submitMesh3D(scene, resources, preparedOpaque3D,
                             opaque3DInstanceBuffer, true);
                break;
            case RenderPassKind::Sprite2D:
                requireResource(RenderPassResource::PrimarySurface);
                configureSprite2DView(surface, *scene.camera2D(), pass.clearColor, pass.clearDepth,
                                      clearRgba);
                submitSprite2D(scene, resources, preparedSprite2D);
                break;
            case RenderPassKind::UI:
                requireResource(RenderPassResource::PrimarySurface);
                configureUIView(surface, pass.clearColor, pass.clearDepth, clearRgba);
                submitUI(surface, displayList, resources, preparedUI);
                break;
            }
        }
    }

    Detail::RenderSurfaceStateTracker surfaceStateTracker_;
    Integration::NativeWindowSurfaceLease lease_;
    std::thread::id ownerThread_{};
    RenderSurfaceState committedSurfaceState_{};
    ShadowMapExtentConfig shadowMapExtents_{};
    u32 drawCallCapacity_ = RenderDeviceCreateParams::DefaultDrawCallCapacity;
    // Resolved at creation; Count means "let bgfx score it".
    bgfx::RendererType::Enum requestedRenderer_ = bgfx::RendererType::Count;
    u32 resetFlags_ = kDefaultResetFlags;
    // Set when resetFlags_ changed without a geometry change, so the next
    // submitted frame re-applies them at the current extent.
    bool resetFlagsDirty_ = false;
    RenderSurfaceExtent appliedBackbuffer_ = BgfxSurfaceFramePlanner::BootstrapBackbufferExtent;
    RenderStatistics statistics_{};
    u64 nextFrameIndex_ = 0;
    u64 nextSubmissionIndex_ = 0;
    bgfx::VertexLayout transientByteLayout_{};
    bgfx::VertexLayout opaque3DVertexLayout_{};
    bgfx::VertexLayout opaque3DSkinVertexLayout_{};
    bgfx::VertexLayout sprite2DVertexLayout_{};
    bgfx::VertexLayout uiVertexLayout_{};
    bgfx::ProgramHandle opaque3DProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle opaque3DSkinnedProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSkinPaletteUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSkinPaletteLastUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSkinColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle opaque3DCsmDepthProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DCsmAtlasSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DCsmMatricesUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DCsmSplitDepthsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DCsmParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSpotShadowMapSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSpotShadowMatrixUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSpotShadowParamsUniform_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::UniformHandle, BgfxPointLightShadowFaceCount>
        opaque3DPointShadowMapSamplers_ =
            invalidBgfxHandleArray<bgfx::UniformHandle,
                                   BgfxPointLightShadowFaceCount>();
    bgfx::UniformHandle opaque3DPointShadowMatricesUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DPointShadowParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DIblDiffuseSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DIblSpecularSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DIblBrdfSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DIblParamsUniform_ = BGFX_INVALID_HANDLE;
    BgfxCascadedDirectionalShadowResources cascadedDirectionalShadowResources_{};
    BgfxSpotLightShadowResources spotLightShadowResources_{};
    BgfxPointLightShadowResources pointLightShadowResources_{};
    bgfx::TextureHandle opaque3DDefaultShadowTexture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DMrSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DNormalSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DLightDirectionsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DLightColorsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DPointLightPositionsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DPointLightColorsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSpotLightPositionsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSpotLightDirectionsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSpotLightColorsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DMrParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DEmissiveFactorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DNormalParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultMrTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultNormalTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultIblCube_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultIblBrdfLut_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle opaque3DVertexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle opaque3DIndexBuffer_ = BGFX_INVALID_HANDLE;
    // Kept alive for the device's whole lifetime because every custom Mesh3D program links against
    // it, and bgfx frees a shader when the last program referencing it goes away.
    bgfx::ShaderHandle opaque3DMrVertexShader_ = BGFX_INVALID_HANDLE;
    // The skinned sibling of the above. One cooked Mesh3D fragment binary is linked against both, so
    // a custom shader covers rigid and skinned geometry without the author cooking two variants.
    bgfx::ShaderHandle opaque3DSkinnedVertexShader_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle sprite2DProgram_ = BGFX_INVALID_HANDLE;
    // Kept alive for the device's whole lifetime because every custom Sprite2D program links against
    // it, and bgfx frees a shader when the last program referencing it goes away.
    bgfx::ShaderHandle sprite2DVertexShader_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sprite2DSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sprite2DNormalSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sprite2DLightPositionsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sprite2DLightColorsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sprite2DLightParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sprite2DNormalParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sprite2DShadowSegmentsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sprite2DDefaultTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sprite2DDefaultNormalTexture_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle uiCoverageProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle uiImageQuadProgram_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle uiSolidWhiteTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle uiGlyphAtlasTexture_ = BGFX_INVALID_HANDLE;
    UIAtlasPageSize uiGlyphAtlasPageSize_{};
    // Last page revision handed to the GPU. Zero means nothing uploaded yet.
    u64 uiGlyphAtlasUploadedPageRevision_ = 0;
    bgfx::UniformHandle uiTexColorUniform_ = BGFX_INVALID_HANDLE;

    enum class RetirementPhase : u8 {
        None,
        Queued,
        Waiting,
    };

    struct TextureSlot final {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        BgfxTextureResourceSlotGeneration identity{};
        u16 width = 0;
        u16 height = 0;
        bool live = false;
        // A decode target only accepts decode submissions, and an uploaded texture only
        // accepts pixel uploads. Both live in this table, so the slot has to say which it
        // is: handing a decode blob to an ordinary texture would reach bgfx as pixel data.
        bool videoDecode = false;
        RetirementPhase retirementPhase = RetirementPhase::None;
        FramePin completionPin{};
    };
    std::vector<TextureSlot> textures_{};
    std::unordered_map<u32, GpuTextureId> texture2DBindings_{};

    struct ShaderSlot final {
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        // The same fragment binary linked against the skinned vertex stage. Only ever valid for
        // Mesh3D, because Sprite2D has no skinned stage. Built at upload rather than on first skinned
        // draw: reflection needs the fragment handle alive, and a draw cannot report a link failure.
        bgfx::ProgramHandle skinnedProgram = BGFX_INVALID_HANDLE;
        BgfxShaderResourceSlotGeneration identity{};
        // Kept so a Sprite2D binding cannot resolve a Mesh3D program. bgfx would already refuse to
        // link mismatched varyings at upload, but the kind is also what selects the engine vertex
        // stage, so the slot must remember which one it was linked against.
        GpuShaderKind shaderKind = GpuShaderKind::Invalid;
        // Reflected at upload while the fragment shader was still alive. Every batch publishes these
        // by name from the batch's value binding, and any the caller left unset gets zero, so a draw
        // never inherits whatever the previous batch happened to leave in the uniform.
        std::vector<ShaderDetail::CustomShaderUniform> authorUniforms{};
        // Identifies this exact upload for the name-resolution memo a binding table keeps. Device-wide
        // monotonic, so a reused slot never inherits the previous program's resolution even though the
        // slot index is the same, and a stale memo can never accidentally match.
        u64 authorUniformsRevision = 0;
        bool live = false;
        RetirementPhase retirementPhase = RetirementPhase::None;
        FramePin completionPin{};
    };
    std::vector<ShaderSlot> shaders_{};
    std::unordered_map<u32, GpuShaderId> shaderBindings_{};

    // Carries the memo of "which of my values feeds each of that program's author uniforms", so a
    // draw publishes by index instead of searching by name. Without it a draw ran a linear name scan
    // per uniform -- 16 uniforms against 16 values is 256 string compares on every single draw.
    //
    // The memo lives here rather than on ShaderSlot because the many-to-one direction is
    // material-to-program: one uploaded program is drawn under several binding keys in the same frame
    // (that is what a material is), so a per-slot memo would miss on every draw of an interleaved
    // batch sequence and be slower than no memo at all. A key, by contrast, is published once and
    // then read by every draw that names it.
    struct ShaderUniformBindingTable final {
        std::vector<GpuShaderUniformValue> values{};
        // NoValueIndex where this table supplies no value for that uniform, which publishes zero.
        // mutable because every draw path resolves the table through a const accessor; this is a memo
        // of data the table and the slot already own, not observable device state.
        static constexpr u8 NoValueIndex = 0xFFU;
        mutable std::vector<u8> valueIndices{};
        // The ShaderSlot::authorUniformsRevision the memo above was built against. 0 means nothing is
        // cached: no upload ever gets revision 0.
        mutable u64 cachedAuthorUniformsRevision = 0;
    };
    std::unordered_map<u32, ShaderUniformBindingTable> shaderUniformBindings_{};
    // Shared by uploads and binding writes so neither can produce a revision the other already used.
    // Monotonic and never reset, so a reused slot or a rewritten key always invalidates a memo.
    u64 nextShaderUniformRevision_ = 1;

    struct EnvironmentMapSlot final {
        BgfxEnvironmentMapResources resources{};
        BgfxTextureResourceSlotGeneration identity{};
        u16 specularMipCount = 0;
        bool live = false;
        RetirementPhase retirementPhase = RetirementPhase::None;
        FramePin completionPin{};
    };
    std::vector<EnvironmentMapSlot> environmentMaps_{};
    std::optional<Mesh3DImageBasedLightingDesc> mesh3DImageBasedLighting_{};

    struct MeshSlot final {
        bgfx::VertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
        // Valid only for skinned slots; retires together with the other buffers.
        bgfx::VertexBufferHandle skinVertexBuffer = BGFX_INVALID_HANDLE;
        BgfxMeshResourceSlotGeneration identity{};
        u32 vertexCount = 0;
        u32 indexCount = 0;
        u16 jointCount = 0;
        bool skinned = false;
        bool live = false;
        RetirementPhase retirementPhase = RetirementPhase::None;
        FramePin completionPin{};
    };
    std::vector<MeshSlot> meshes_{};
    std::unordered_map<u32, GpuMeshId> mesh3DBindings_{};
    std::unordered_map<u32, Mesh3DMaterialBindingDesc> mesh3DMaterialBindings_{};
    Mesh3DDirectionalLightUniformStorage mesh3DLightDirections_{
        0.4F, 0.85F, 0.35F, 1.0F};
    Mesh3DDirectionalLightUniformStorage mesh3DLightColors_{
        1.0F, 0.98F, 0.92F, 1.0F};
    Mesh3DPointLightUniformStorage mesh3DPointLightPositionsAndRadii_{};
    Mesh3DPointLightUniformStorage mesh3DPointLightColors_{};
    Mesh3DSpotLightUniformStorage mesh3DSpotLightPositionsAndRadii_{};
    Mesh3DSpotLightUniformStorage mesh3DSpotLightDirectionsAndInnerCosines_{};
    Mesh3DSpotLightUniformStorage mesh3DSpotLightColorsAndOuterCosines_{};
    float mesh3DAmbientScale_ = 0.18F;

    bool frameOpen_ = false;
    bool stopped_ = false;
    bool bgfxInitialized_ = false;
    bool captureInFlight_ = false;
    // Set between frames, consumed by present() before it dispatches the frame.
    bool captureArmed_ = false;
    bool retirementMarkerSupported_ = false;
    bgfx::TextureHandle retirementMarkerSource_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle retirementMarkerReadback_ = BGFX_INVALID_HANDLE;
    std::array<u8, 4> retirementMarkerBytes_{};
    RetirementDetail::BgfxRetirementTimeline retirementTimeline_{};
    BgfxCaptureCallback captureCallback_{};
};

[[nodiscard]] Core::Status validateInitialSurfaceForLease(const RenderSurfaceState& initialSurface,
                                                          const Integration::NativeWindowSurfaceLease& lease)
{
    if (initialSurface.surface != toRenderSurfaceId(lease.surface()))
    {
        return Core::failure(RenderErrorCode::InvalidSurfaceState,
                             "The bgfx lease and initial RenderSurfaceState identify different surfaces");
    }
    if (auto status = BgfxSurfaceFramePlanner::validateViewExtent(initialSurface); !status)
    {
        return status;
    }
    return Core::success();
}

// Maps Tina's backend-neutral API selection onto a bgfx renderer, resolving Automatic
// through Tina's platform preference first. Fails for an API this build cannot create
// rather than falling through to bgfx's scoring, so an unsupported request is reported
// as such instead of quietly running on something else.
[[nodiscard]] Core::Result<bgfx::RendererType::Enum> resolveRendererType(RendererApi requested)
{
    RendererApi api = requested;
    if (api == RendererApi::Automatic)
    {
        api = preferredRendererApi();
    }
    switch (api)
    {
    case RendererApi::Automatic:
        // No platform preference: let bgfx score the available renderers.
        return bgfx::RendererType::Count;
    case RendererApi::Vulkan:
        return bgfx::RendererType::Vulkan;
    case RendererApi::Direct3D11:
        return bgfx::RendererType::Direct3D11;
    case RendererApi::Direct3D12:
        return bgfx::RendererType::Direct3D12;
    case RendererApi::Metal:
        return bgfx::RendererType::Metal;
    case RendererApi::OpenGL:
        return bgfx::RendererType::OpenGL;
    case RendererApi::OpenGLES:
        return bgfx::RendererType::OpenGLES;
    }
    return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                         "The requested graphics API is not a supported Render::RendererApi value");
}

} // namespace

Core::Result<std::unique_ptr<IRenderDevice>> createBgfxRenderDevice(const RenderDeviceCreateParams& params,
                                                                    Integration::NativeWindowSurfaceLease lease)
{
    if (auto status = validateShadowMapExtentConfig(params.shadowMapExtents); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (!RenderDeviceCreateParams::isSupportedDrawCallCapacity(
            params.drawCallCapacity) ||
        params.drawCallCapacity > kCompiledBgfxMaximumDrawCalls)
    {
        return Core::failure(
            RenderErrorCode::DeviceInitializationFailed,
            "The bgfx draw-call capacity must be a 1024-entry block within the compiled backend maximum");
    }
    u32 resetFlags = kDefaultResetFlags;
    switch (params.msaaSamples)
    {
    case 0:
        break;
    case 2:
        resetFlags |= BGFX_RESET_MSAA_X2;
        break;
    case 4:
        resetFlags |= BGFX_RESET_MSAA_X4;
        break;
    case 8:
        resetFlags |= BGFX_RESET_MSAA_X8;
        break;
    case 16:
        resetFlags |= BGFX_RESET_MSAA_X16;
        break;
    default:
        return Core::failure(RenderErrorCode::InvalidSurfaceState,
                             "The bgfx render device MSAA sample count must be 0, 2, 4, 8, or 16");
    }
    if (params.vsync)
    {
        resetFlags |= BGFX_RESET_VSYNC;
    }
    if (!params.initialPrimaryWindowSurface.has_value())
    {
        return Core::failure(RenderErrorCode::InvalidSurfaceState,
                             "The bgfx render device requires an initial primary window surface");
    }
    if (!lease)
    {
        return Core::failure(RenderErrorCode::InvalidNativeWindowBinding,
                             "The bgfx render device requires a live native window surface lease");
    }

    auto surfaceStateTracker = Detail::RenderSurfaceStateTracker::create(params.initialPrimaryWindowSurface);
    if (!surfaceStateTracker)
    {
        return Core::failure(std::move(surfaceStateTracker.error()));
    }

    const RenderSurfaceState initialSurface = *params.initialPrimaryWindowSurface;
    if (auto status = validateInitialSurfaceForLease(initialSurface, lease); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    auto nativeBinding = decodeNativeWindowBinding(lease);
    if (!nativeBinding)
    {
        return Core::failure(std::move(nativeBinding.error()));
    }

    auto rendererType = resolveRendererType(params.rendererApi);
    if (!rendererType)
    {
        return Core::failure(std::move(rendererType.error()));
    }

    try
    {
        auto renderDevice = std::unique_ptr<BgfxRenderDevice>(
            new BgfxRenderDevice(std::move(*surfaceStateTracker), std::move(lease), initialSurface,
                                 params.shadowMapExtents, params.drawCallCapacity,
                                 resetFlags, *rendererType));

        if (auto status = renderDevice->initialize(nativeBinding->platformData); !status)
        {
            return Core::failure(std::move(status.error()));
        }

        return std::unique_ptr<IRenderDevice>{std::move(renderDevice)};
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "The bgfx render device allocation failed");
    } catch (...)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "The bgfx render device factory threw unexpectedly");
    }
}

} // namespace Tina::Render::Bgfx
