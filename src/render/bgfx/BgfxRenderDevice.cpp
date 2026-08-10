#include "BgfxRenderDevice.hpp"
#include "BgfxCascadedDirectionalShadowMath.hpp"
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
#include "BgfxTransientFrameBudget.hpp"
#include "BgfxUIDisplayGeometry.hpp"
#include "BgfxUIAtlasTexture.hpp"
#include "BgfxUIImageShader.hpp"
#include "BgfxUITexturedShader.hpp"

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

    void traceVargs(const char* /*filePath*/, uint16_t /*line*/, const char* /*format*/,
                    va_list /*argList*/) override
    {
    }

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
constexpr bgfx::ViewId kSprite2DView = 13;
constexpr bgfx::ViewId kUIView = 14;
// Must remain after every view that can reference a retireable resource.
constexpr bgfx::ViewId kRetirementMarkerView = 15;
constexpr u32 kDefaultResetFlags = BGFX_RESET_VSYNC;
constexpr u32 kClearRgba = 0x102a43ff;
constexpr usize kIndicesPerSolidQuad = 6;
constexpr u64 kOpaque3DState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
constexpr u64 kOpaque3DShadowDepthState =
    BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
constexpr u64 kSprite2DPremultipliedAlphaState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
constexpr u64 kUIPremultipliedAlphaState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

static_assert(std::is_standard_layout_v<BgfxUIDisplayVertex>);
static_assert(sizeof(BgfxUIDisplayVertex) == sizeof(float) * 7U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, x) == 0U);
static_assert(offsetof(BgfxUIDisplayVertex, y) == sizeof(float));
static_assert(offsetof(BgfxUIDisplayVertex, abgr) == sizeof(float) * 2U);
static_assert(offsetof(BgfxUIDisplayVertex, u) == sizeof(float) * 2U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, v) == sizeof(float) * 3U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, shapeWidth) == sizeof(float) * 4U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, shapeHeight) == sizeof(float) * 5U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, shapeParameter) == sizeof(float) * 6U + sizeof(u32));
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
    default:
        return Core::failure(RenderErrorCode::InvalidNativeWindowBinding,
                             "The native window binding kind is not supported by the bgfx backend");
    }

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
preflightOpaque3D(RenderSceneView scene, FrameResourceTableView resources)
{
    auto requirements = checkedOpaque3DFrame(scene, resources);
    if (!requirements)
    {
        return Core::failure(std::move(requirements.error()));
    }
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
                     u32 resetFlags) noexcept
        : surfaceStateTracker_(std::move(surfaceStateTracker)), lease_(std::move(lease)),
          ownerThread_(std::this_thread::get_id()), committedSurfaceState_(initialSurface),
          shadowMapExtents_(shadowMapExtents), resetFlags_(resetFlags)
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
        init.type = bgfx::RendererType::Count;
        init.platformData = platformData;
        init.resolution.width = initialBackbuffer.width;
        init.resolution.height = initialBackbuffer.height;
        init.resolution.reset = resetFlags_;
        // M11-D1: required for requestScreenShot / product pixel evidence.
        init.callback = &captureCallback_;

        if (!bgfx::init(init))
        {
            return bgfxInitFailed();
        }

        bgfxInitialized_ = true;
        appliedBackbuffer_ = initialBackbuffer;

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

        auto cascadedDirectionalShadowResources =
            createCascadedDirectionalShadowResources(
                shadowMapExtents_.directionalCascadeTileExtent);
        if (!cascadedDirectionalShadowResources)
        {
            return Core::failure(std::move(cascadedDirectionalShadowResources.error()));
        }
        cascadedDirectionalShadowResources_ = *cascadedDirectionalShadowResources;
        statistics_.liveResources += 2U;

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

        auto spotLightShadowResources =
            createSpotLightShadowResources(shadowMapExtents_.spotLightMapExtent);
        if (!spotLightShadowResources)
        {
            return Core::failure(std::move(spotLightShadowResources.error()));
        }
        spotLightShadowResources_ = *spotLightShadowResources;
        statistics_.liveResources += 2U;

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

        auto pointLightShadowResources =
            createPointLightShadowResources(shadowMapExtents_.pointLightFaceExtent);
        if (!pointLightShadowResources)
        {
            return Core::failure(std::move(pointLightShadowResources.error()));
        }
        pointLightShadowResources_ = *pointLightShadowResources;
        statistics_.liveResources +=
            static_cast<u64>(BgfxPointLightShadowFaceCount * 2U);

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
        if (auto status = validateSprite2DFrameResources(frame.primaryWorldScene, frame.resources); !status)
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
                preflightOpaque3D(frame.primaryWorldScene, frame.resources);
            if (!opaque3DPreflight)
            {
                return Core::failure(std::move(opaque3DPreflight.error()));
            }
            preparedOpaque3D = *opaque3DPreflight;

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
        }
        if (auto status = surfaceStateTracker_.validateAndCommit(frame.primaryWindowSurface); !status)
        {
            return Core::failure(std::move(status.error()));
        }

        committedSurfaceState_ = *frame.primaryWindowSurface;
        ++nextFrameIndex_;

        if (!framePlan->shouldSubmit())
        {
            ++statistics_.skippedSuspendedSurfaceFrames;
            pumpRetirementOnlyFrameIfNeeded();
            return RenderFrameSubmission::SkippedSuspendedSurface();
        }

        if (framePlan->resetBackbuffer)
        {
            resetBackbuffer(framePlan->targetExtent);
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

        submitRetirementMarkerIfNeeded();
        const u32 currentFrame = bgfx::frame();
        completeRetirementsThrough(currentFrame);
        frameOpen_ = false;
        ++statistics_.presented;
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
            // directional/spot shadow depth textures/framebuffers, opaque3DMrSampler,
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
            if (bgfx::isValid(slot.indexBuffer))
            {
                bgfx::destroy(slot.indexBuffer);
                slot.indexBuffer = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            slot.vertexCount = 0;
            slot.indexCount = 0;
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

    void configureSurfaceClearView(const RenderSurfaceState& surface) noexcept
    {
        bgfx::setViewRect(kSurfaceClearView, 0, 0, static_cast<u16>(surface.framebufferExtent.width),
                          static_cast<u16>(surface.framebufferExtent.height));
        bgfx::setViewClear(kSurfaceClearView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kClearRgba, 1.0F, 0);
        bgfx::setViewMode(kSurfaceClearView, bgfx::ViewMode::Sequential);
        bgfx::touch(kSurfaceClearView);
    }

    void configureCascadedDirectionalShadowView(
        const PreparedOpaque3D::CascadedDirectionalShadow& shadow,
        usize cascadeIndex,
        bool clearDepth) noexcept
    {
        if (cascadeIndex >= BgfxCascadedDirectionalShadowCascadeCount)
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

    void configureOpaque3DView(const RenderSurfaceState& surface, const RenderPerspectiveCamera& camera,
                               bool clearColor, bool clearDepth) noexcept
    {
        const BgfxViewRect rect = viewportRect(surface, camera.normalizedViewport);
        if (rect.width == 0 || rect.height == 0)
        {
            std::terminate();
        }
        bgfx::setViewRect(kOpaque3DView, rect.x, rect.y, rect.width, rect.height);
        const u16 clearFlags = static_cast<u16>((clearColor ? BGFX_CLEAR_COLOR : 0U) |
                                                (clearDepth ? BGFX_CLEAR_DEPTH : 0U));
        bgfx::setViewClear(kOpaque3DView, clearFlags, kClearRgba, 1.0F, 0);
        bgfx::setViewMode(kOpaque3DView, bgfx::ViewMode::Sequential);

        const bx::Vec3 eye{camera.positionX, camera.positionY, camera.positionZ};
        const bx::Vec3 target{
            camera.positionX + camera.forwardX,
            camera.positionY + camera.forwardY,
            camera.positionZ + camera.forwardZ,
        };
        const bx::Vec3 up{camera.upX, camera.upY, camera.upZ};
        float view[16]{};
        bx::mtxLookAt(view, eye, target, up, bx::Handedness::Right);

        float projection[16]{};
        const bgfx::Caps* const caps = bgfx::getCaps();
        bx::mtxProj(projection, camera.verticalFovDegrees, camera.aspectRatio, camera.nearPlaneMeters,
                    camera.farPlaneMeters, caps != nullptr && caps->homogeneousDepth, bx::Handedness::Right);
        bgfx::setViewTransform(kOpaque3DView, view, projection);
        bgfx::touch(kOpaque3DView);
    }

    void configureSprite2DView(const RenderSurfaceState& surface, const RenderCamera2D& camera,
                               bool clearColor, bool clearDepth) noexcept
    {
        const BgfxViewRect rect = viewportRect(surface, camera.normalizedViewport);
        if (rect.width == 0 || rect.height == 0)
        {
            std::terminate();
        }
        bgfx::setViewRect(kSprite2DView, rect.x, rect.y, rect.width, rect.height);
        const u16 clearFlags = static_cast<u16>((clearColor ? BGFX_CLEAR_COLOR : 0U) |
                                                (clearDepth ? BGFX_CLEAR_DEPTH : 0U));
        bgfx::setViewClear(kSprite2DView, clearFlags, kClearRgba, 1.0F, 0);
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

    void configureUIView(const RenderSurfaceState& surface, bool clearColor, bool clearDepth) noexcept
    {
        bgfx::setViewRect(kUIView, 0, 0, static_cast<u16>(surface.framebufferExtent.width),
                          static_cast<u16>(surface.framebufferExtent.height));
        const u16 clearFlags = static_cast<u16>((clearColor ? BGFX_CLEAR_COLOR : 0U) |
                                                (clearDepth ? BGFX_CLEAR_DEPTH : 0U));
        bgfx::setViewClear(kUIView, clearFlags, kClearRgba, 1.0F, 0);
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
        if (prepared.requirements.instanceCount == 0)
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

    void submitOpaque3D(RenderSceneView scene, FrameResourceTableView resources,
                        const PreparedOpaque3D& prepared,
                        bgfx::InstanceDataBuffer& instanceBuffer) noexcept
    {
        if (prepared.requirements.instanceCount == 0)
        {
            return;
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

        bgfx::setScissor();
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

            bgfx::setVertexBuffer(0, geometry->vertexBuffer);
            bgfx::setIndexBuffer(geometry->indexBuffer);
            bgfx::setInstanceDataBuffer(&instanceBuffer, batch.firstItem, batch.itemCount);
            bgfx::setTexture(0, opaque3DSampler_, materialTexture);
            bgfx::setTexture(1, opaque3DMrSampler_, mrTexture);
            bgfx::setTexture(2, opaque3DNormalSampler_, normalTexture);
            bgfx::setTexture(3, opaque3DCsmAtlasSampler_,
                             cascadedDirectionalShadowResources_.depthAtlas);
            bgfx::setTexture(4, opaque3DIblDiffuseSampler_, iblDiffuse);
            bgfx::setTexture(5, opaque3DIblSpecularSampler_, iblSpecular);
            bgfx::setTexture(6, opaque3DIblBrdfSampler_, iblBrdf);
            bgfx::setTexture(7, opaque3DSpotShadowMapSampler_,
                             spotLightShadowResources_.depthMap);
            for (usize faceIndex = 0; faceIndex < opaque3DPointShadowMapSamplers_.size();
                 ++faceIndex)
            {
                bgfx::setTexture(static_cast<u8>(8U + faceIndex),
                                 opaque3DPointShadowMapSamplers_[faceIndex],
                                 pointLightShadowResources_.depthMaps[faceIndex]);
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
            bgfx::submit(kOpaque3DView, opaque3DProgram_);
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
            u32 batchEnd = batchBegin + 1U;
            while (batchEnd < prepared.requirements.spriteCount &&
                   sprites[batchEnd].texture == batchTexture &&
                   sprites[batchEnd].normalTexture == batchNormalTexture)
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
            const u32 firstIndex = batchBegin * 6U;
            const u32 indexCount = (batchEnd - batchBegin) * 6U;
            bgfx::setIndexBuffer(&transientIndices, firstIndex, indexCount);
            bgfx::submit(kSprite2DView, sprite2DProgram_);
            batchBegin = batchEnd;
        }
    }

    [[nodiscard]] Core::Result<GpuTextureId> createTexture2DRgba8(const Texture2DUploadDesc& desc) override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::createTexture2DRgba8"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        const usize expectedBytes =
            static_cast<usize>(desc.width) * static_cast<usize>(desc.height) * 4U;
        if (desc.width == 0 || desc.height == 0 || desc.rgba8Pixels.size() != expectedBytes)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "invalid Texture2D RGBA8 upload descriptor");
        }

        const bgfx::Memory* memory =
            bgfx::copy(desc.rgba8Pixels.data(), static_cast<u32>(desc.rgba8Pixels.size()));
        const bgfx::TextureHandle handle =
            bgfx::createTexture2D(desc.width, desc.height, false, 1, bgfx::TextureFormat::RGBA8,
                                  BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, memory);
        if (!bgfx::isValid(handle))
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "bgfx rejected Texture2D create");
        }

        u32 index = (std::numeric_limits<u32>::max)();
        for (u32 slotIndex = 0; slotIndex < static_cast<u32>(textures_.size()); ++slotIndex)
        {
            if (textures_[slotIndex].identity.canReuse(
                    textures_[slotIndex].live,
                    textures_[slotIndex].retirementPhase != RetirementPhase::None))
            {
                index = slotIndex;
                break;
            }
        }
        if (index == (std::numeric_limits<u32>::max)())
        {
            index = static_cast<u32>(textures_.size());
            textures_.push_back(TextureSlot{});
        }
        TextureSlot& slot = textures_[index];
        slot.handle = handle;
        slot.width = desc.width;
        slot.height = desc.height;
        slot.live = true;
        slot.retirementPhase = RetirementPhase::None;
        ++statistics_.liveResources;
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
        slot.vertexCount = desc.vertexCount;
        slot.indexCount = desc.indexCount;
        slot.live = true;
        slot.retirementPhase = RetirementPhase::None;
        ++statistics_.liveResources;
        ++statistics_.liveResources;
        return GpuMeshId{resourceOwnerId(), slotIndex, slot.identity.value()};
    }

    [[nodiscard]] Core::Status destroyStaticMesh(GpuMeshId mesh) noexcept override
    {
        FramePin completionPin;
        return retireStaticMesh(mesh, completionPin);
    }

    [[nodiscard]] Core::Status retireStaticMesh(GpuMeshId mesh,
                                                FramePin& completionPin) noexcept override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::retireStaticMesh"); !status)
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
            return Core::failure(RenderErrorCode::MeshNotFound, "StaticMesh handle is invalid");
        }
        MeshSlot& slot = meshes_[mesh.index];
        if (!slot.live || slot.identity.value() != mesh.generation)
        {
            return Core::failure(RenderErrorCode::MeshNotFound, "StaticMesh handle is stale or destroyed");
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
            if (bgfx::isValid(slot.indexBuffer))
            {
                bgfx::destroy(slot.indexBuffer);
                slot.indexBuffer = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            slot.vertexCount = 0;
            slot.indexCount = 0;
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
            return Core::failure(RenderErrorCode::MeshNotFound, "StaticMesh handle is invalid");
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
        constexpr int kMaxFrames = 8;
        for (int i = 0; i < kMaxFrames && !captureCallback_.isReady(); ++i)
        {
            submitRetirementMarkerIfNeeded();
            const u32 currentFrame = bgfx::frame();
            completeRetirementsThrough(currentFrame);
        }
        captureInFlight_ = false;
        if (!captureCallback_.isReady())
        {
            return Core::failure(RenderErrorCode::FrameCaptureFailed,
                                 "bgfx did not deliver a screenshot within the capture budget");
        }
        return captureCallback_.take();
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
            ++statistics_.liveResources;
            return Core::success();
        }
        return updateUIGlyphAtlasTexture(
            uiGlyphAtlasTexture_, atlas->width, atlas->height, atlas->pixels);
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
                configureSurfaceClearView(surface);
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
                configureOpaque3DView(surface, *scene.perspectiveCamera(), pass.clearColor, pass.clearDepth);
                submitOpaque3D(scene, resources, preparedOpaque3D,
                               opaque3DInstanceBuffer);
                break;
            case RenderPassKind::Sprite2D:
                requireResource(RenderPassResource::PrimarySurface);
                configureSprite2DView(surface, *scene.camera2D(), pass.clearColor, pass.clearDepth);
                submitSprite2D(scene, resources, preparedSprite2D);
                break;
            case RenderPassKind::UI:
                requireResource(RenderPassResource::PrimarySurface);
                configureUIView(surface, pass.clearColor, pass.clearDepth);
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
    u32 resetFlags_ = kDefaultResetFlags;
    RenderSurfaceExtent appliedBackbuffer_ = BgfxSurfaceFramePlanner::BootstrapBackbufferExtent;
    RenderStatistics statistics_{};
    u64 nextFrameIndex_ = 0;
    u64 nextSubmissionIndex_ = 0;
    bgfx::VertexLayout transientByteLayout_{};
    bgfx::VertexLayout opaque3DVertexLayout_{};
    bgfx::VertexLayout sprite2DVertexLayout_{};
    bgfx::VertexLayout uiVertexLayout_{};
    bgfx::ProgramHandle opaque3DProgram_ = BGFX_INVALID_HANDLE;
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
    bgfx::UniformHandle opaque3DNormalParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultMrTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultNormalTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultIblCube_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultIblBrdfLut_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle opaque3DVertexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle opaque3DIndexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle sprite2DProgram_ = BGFX_INVALID_HANDLE;
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
        RetirementPhase retirementPhase = RetirementPhase::None;
        FramePin completionPin{};
    };
    std::vector<TextureSlot> textures_{};
    std::unordered_map<u32, GpuTextureId> texture2DBindings_{};

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
        BgfxMeshResourceSlotGeneration identity{};
        u32 vertexCount = 0;
        u32 indexCount = 0;
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

} // namespace

Core::Result<std::unique_ptr<IRenderDevice>> createBgfxRenderDevice(const RenderDeviceCreateParams& params,
                                                                    Integration::NativeWindowSurfaceLease lease)
{
    if (auto status = validateShadowMapExtentConfig(params.shadowMapExtents); !status)
    {
        return Core::failure(std::move(status.error()));
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

    try
    {
        auto renderDevice = std::unique_ptr<BgfxRenderDevice>(
            new BgfxRenderDevice(std::move(*surfaceStateTracker), std::move(lease), initialSurface,
                                 params.shadowMapExtents, resetFlags));

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
