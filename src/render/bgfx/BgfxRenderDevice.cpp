#include "BgfxRenderDevice.hpp"
#include "BgfxOpaque3DGeometry.hpp"
#include "BgfxOpaque3DShader.hpp"
#include "BgfxSprite2DGeometry.hpp"
#include "BgfxSprite2DShader.hpp"
#include "BgfxSurfaceFramePlanner.hpp"
#include "BgfxTransientFrameBudget.hpp"
#include "BgfxUIDisplayGeometry.hpp"
#include "BgfxUIAtlasTexture.hpp"
#include "BgfxUISolidQuadShader.hpp"
#include "BgfxUITexturedShader.hpp"

#include "../../integration/WindowSurfaceLeaseAccess.hpp"
#include "../RenderSurfaceStateTracker.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/render/RenderErrors.hpp>

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
#include <span>
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
constexpr bgfx::ViewId kOpaque3DView = 1;
constexpr bgfx::ViewId kSprite2DView = 2;
constexpr bgfx::ViewId kUIView = 3;
constexpr u32 kDefaultResetFlags = BGFX_RESET_VSYNC;
constexpr u32 kClearRgba = 0x102a43ff;
constexpr usize kIndicesPerSolidQuad = 6;
constexpr u64 kOpaque3DState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
constexpr u64 kSprite2DPremultipliedAlphaState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
constexpr u64 kUIPremultipliedAlphaState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                           BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

static_assert(std::is_standard_layout_v<BgfxUIDisplayVertex>);
static_assert(sizeof(BgfxUIDisplayVertex) == sizeof(float) * 4U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, x) == 0U);
static_assert(offsetof(BgfxUIDisplayVertex, y) == sizeof(float));
static_assert(offsetof(BgfxUIDisplayVertex, abgr) == sizeof(float) * 2U);
static_assert(offsetof(BgfxUIDisplayVertex, u) == sizeof(float) * 2U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, v) == sizeof(float) * 3U + sizeof(u32));
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
};

struct PreparedSprite2D final {
    BgfxSprite2DFrameRequirements requirements{};
};

struct PreparedUIDisplayList final {
    u32 vertexCount = 0;
    u32 indexCount = 0;
};

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
        if (batch.kind != UIDrawCommandKind::SolidQuad
            && batch.kind != UIDrawCommandKind::Glyph)
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
            if (command.kind != batch.kind || command.clip != batch.clip)
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

[[nodiscard]] Core::Result<PreparedUIDisplayList> preflightUIDisplayList(UIDisplayListView displayList)
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

[[nodiscard]] Core::Result<PreparedOpaque3D> preflightOpaque3D(RenderSceneView scene)
{
    auto requirements = checkedOpaque3DFrame(scene);
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
    return PreparedOpaque3D{.requirements = *requirements};
}

[[nodiscard]] Core::Result<PreparedSprite2D> preflightSprite2D(RenderSceneView scene)
{
    auto requirements = checkedSprite2DFrame(scene);
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
                     RenderSurfaceState initialSurface) noexcept
        : surfaceStateTracker_(std::move(surfaceStateTracker)), lease_(std::move(lease)),
          ownerThread_(std::this_thread::get_id()), committedSurfaceState_(initialSurface)
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
        init.resolution.reset = kDefaultResetFlags;
        // M11-D1: required for requestScreenShot / product pixel evidence.
        init.callback = &captureCallback_;

        if (!bgfx::init(init))
        {
            return bgfxInitFailed();
        }

        bgfxInitialized_ = true;
        appliedBackbuffer_ = initialBackbuffer;

        uiVertexLayout_.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
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
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();

        auto uiProgram = ShaderDetail::createUITexturedQuadProgram();
        if (!uiProgram)
        {
            return Core::failure(std::move(uiProgram.error()));
        }
        uiSolidQuadProgram_ = *uiProgram;
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

        // RENDER-001: product Opaque3D uses experimental metallic-roughness hybrid.
        auto opaque3DProgram = ShaderDetail::createOpaque3DMrProgram();
        if (!opaque3DProgram)
        {
            return Core::failure(std::move(opaque3DProgram.error()));
        }
        opaque3DProgram_ = *opaque3DProgram;
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

        opaque3DLightDirUniform_ = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DLightDirUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D light direction uniform");
        }
        ++statistics_.liveResources;

        opaque3DLightColorUniform_ = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
        if (!bgfx::isValid(opaque3DLightColorUniform_))
        {
            return Core::failure(RenderErrorCode::DeviceInitializationFailed,
                                 "bgfx rejected the Opaque3D light color uniform");
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
        if (framePlan->shouldSubmit())
        {
            auto opaque3DPreflight = preflightOpaque3D(frame.primaryWorldScene);
            if (!opaque3DPreflight)
            {
                return Core::failure(std::move(opaque3DPreflight.error()));
            }
            preparedOpaque3D = *opaque3DPreflight;

            auto sprite2DPreflight = preflightSprite2D(frame.primaryWorldScene);
            if (!sprite2DPreflight)
            {
                return Core::failure(std::move(sprite2DPreflight.error()));
            }
            preparedSprite2D = *sprite2DPreflight;

            auto preflight = preflightUIDisplayList(frame.primaryWindowUIDisplayList);
            if (!preflight)
            {
                return Core::failure(std::move(preflight.error()));
            }
            preparedUI = *preflight;

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
            return RenderFrameSubmission::SkippedSuspendedSurface();
        }

        if (framePlan->resetBackbuffer)
        {
            resetBackbuffer(framePlan->targetExtent);
        }

        submitPrimaryFrame(committedSurfaceState_, frame.primaryWorldScene, preparedOpaque3D, preparedSprite2D,
                           frame.primaryWindowUIDisplayList, preparedUI);
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

        // bgfx::frame() advances the multi-buffered GPU timeline; token is the
        // frame number for FrameDeferred pin lag (not a true fence object).
        lastPresentFrameToken_ = bgfx::frame();
        frameOpen_ = false;
        ++statistics_.presented;
        return Core::success();
    }

    [[nodiscard]] std::optional<u64> lastPresentFrameToken() const noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        return lastPresentFrameToken_;
    }

    [[nodiscard]] RenderStatistics statistics() const noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        return statistics_;
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
            mesh3DBindings_.clear();
            mesh3DMaterialTextureBindings_.clear();
            mesh3DMaterialMetallicRoughnessTextureBindings_.clear();
            mesh3DMaterialNormalTextureBindings_.clear();
            mesh3DMaterialFactors_.clear();
            for (MeshSlot& slot : meshes_)
            {
                if (slot.live)
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
                    slot.live = false;
                    ++slot.generation;
                }
            }
            meshes_.clear();
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
            if (bgfx::isValid(opaque3DLightDirUniform_))
            {
                bgfx::destroy(opaque3DLightDirUniform_);
                opaque3DLightDirUniform_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            if (bgfx::isValid(opaque3DLightColorUniform_))
            {
                bgfx::destroy(opaque3DLightColorUniform_);
                opaque3DLightColorUniform_ = BGFX_INVALID_HANDLE;
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
            mesh3DMaterialTextureBindings_.clear();
            mesh3DMaterialMetallicRoughnessTextureBindings_.clear();
            mesh3DMaterialNormalTextureBindings_.clear();
            mesh3DMaterialFactors_.clear();
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
            if (bgfx::isValid(sprite2DSampler_))
            {
                bgfx::destroy(sprite2DSampler_);
                sprite2DSampler_ = BGFX_INVALID_HANDLE;
                --statistics_.liveResources;
            }
            for (TextureSlot& slot : textures_)
            {
                if (slot.live && bgfx::isValid(slot.handle))
                {
                    bgfx::destroy(slot.handle);
                    slot.handle = BGFX_INVALID_HANDLE;
                    slot.live = false;
                    --statistics_.liveResources;
                }
            }
            textures_.clear();
            spriteTextureBindings_.clear();
            if (bgfx::isValid(uiSolidQuadProgram_))
            {
                bgfx::destroy(uiSolidQuadProgram_);
                uiSolidQuadProgram_ = BGFX_INVALID_HANDLE;
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
            // Device-owned init resources (must match ++ in initialize):
            // uiSolidQuadProgram, uiSolidWhiteTexture, uiTexColorUniform,
            // sprite2DProgram, sprite2DSampler, sprite2DDefaultTexture,
            // opaque3DProgram, opaque3DSampler, opaque3DMrSampler,
            // opaque3DLightDir/Color/MrParams uniforms, opaque3DDefaultTexture,
            // opaque3DDefaultMrTexture, opaque3DVertexBuffer, opaque3DIndexBuffer
            // (+ dynamic mesh/texture slots).
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
                    std::fprintf(file, "BgfxRenderDevice liveResources imbalance at shutdown: %u\n",
                                 statistics_.liveResources);
                    std::fflush(file);
                    std::fclose(file);
                }
                std::fprintf(stderr, "BgfxRenderDevice liveResources imbalance at shutdown: %u\n",
                             statistics_.liveResources);
                std::fflush(stderr);
                std::_Exit(3);
            }
            bgfx::shutdown();
            bgfxInitialized_ = false;
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
        bgfx::reset(extent.width, extent.height, kDefaultResetFlags);
        appliedBackbuffer_ = extent;
    }

    void configureSurfaceClearView(const RenderSurfaceState& surface) noexcept
    {
        bgfx::setViewRect(kSurfaceClearView, 0, 0, static_cast<u16>(surface.framebufferExtent.width),
                          static_cast<u16>(surface.framebufferExtent.height));
        bgfx::setViewClear(kSurfaceClearView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kClearRgba, 1.0F, 0);
        bgfx::setViewMode(kSurfaceClearView, bgfx::ViewMode::Sequential);
        bgfx::touch(kSurfaceClearView);
    }

    void configureOpaque3DView(const RenderSurfaceState& surface, const RenderPerspectiveCamera& camera) noexcept
    {
        const BgfxViewRect rect = viewportRect(surface, camera.normalizedViewport);
        if (rect.width == 0 || rect.height == 0)
        {
            std::terminate();
        }
        bgfx::setViewRect(kOpaque3DView, rect.x, rect.y, rect.width, rect.height);
        bgfx::setViewClear(kOpaque3DView, BGFX_CLEAR_NONE, kClearRgba, 1.0F, 0);
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

    void configureSprite2DView(const RenderSurfaceState& surface, const RenderCamera2D& camera) noexcept
    {
        const BgfxViewRect rect = viewportRect(surface, camera.normalizedViewport);
        if (rect.width == 0 || rect.height == 0)
        {
            std::terminate();
        }
        bgfx::setViewRect(kSprite2DView, rect.x, rect.y, rect.width, rect.height);
        bgfx::setViewClear(kSprite2DView, BGFX_CLEAR_NONE, kClearRgba, 1.0F, 0);
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

    void configureUIView(const RenderSurfaceState& surface) noexcept
    {
        bgfx::setViewRect(kUIView, 0, 0, static_cast<u16>(surface.framebufferExtent.width),
                          static_cast<u16>(surface.framebufferExtent.height));
        bgfx::setViewClear(kUIView, BGFX_CLEAR_NONE, kClearRgba, 1.0F, 0);
        bgfx::setViewMode(kUIView, bgfx::ViewMode::Sequential);

        float projection[16]{};
        const bgfx::Caps* const caps = bgfx::getCaps();
        bx::mtxOrtho(projection, 0.0F, static_cast<float>(surface.framebufferExtent.width),
                     static_cast<float>(surface.framebufferExtent.height), 0.0F, 0.0F, 1000.0F, 0.0F,
                     caps != nullptr && caps->homogeneousDepth);
        bgfx::setViewTransform(kUIView, nullptr, projection);
        bgfx::touch(kUIView);
    }

    void submitOpaque3D(RenderSceneView scene, PreparedOpaque3D prepared) noexcept
    {
        if (prepared.requirements.instanceCount == 0)
        {
            return;
        }

        constexpr u16 InstanceStride = static_cast<u16>(sizeof(BgfxOpaque3DInstanceData));
        bgfx::InstanceDataBuffer instanceBuffer{};
        bgfx::allocInstanceDataBuffer(&instanceBuffer, prepared.requirements.instanceCount, InstanceStride);
        auto instances = std::span{
            reinterpret_cast<BgfxOpaque3DInstanceData*>(instanceBuffer.data),
            static_cast<usize>(prepared.requirements.instanceCount),
        };
        auto written = writeOpaque3DInstanceData(scene, instances);
        if (!written || written->instanceCount != prepared.requirements.instanceCount ||
            written->batchCount != prepared.requirements.batchCount)
        {
            std::terminate();
        }

        bgfx::setScissor();
        for (const RenderMesh3DBatch& batch : scene.mesh3DBatches())
        {
            const u64 renderState = kOpaque3DState | (batch.doubleSided ? 0 : BGFX_STATE_CULL_CW);
            bgfx::setState(renderState);

            bgfx::VertexBufferHandle vb = opaque3DVertexBuffer_;
            bgfx::IndexBufferHandle ib = opaque3DIndexBuffer_;
            // meshKey=1 defaults to built-in cube; other keys require setMesh3DBinding.
            if (const auto binding = mesh3DBindings_.find(batch.meshKey); binding != mesh3DBindings_.end())
            {
                const GpuMeshId id = binding->second;
                if (id.index < meshes_.size())
                {
                    const MeshSlot& slot = meshes_[id.index];
                    if (slot.live && slot.generation == id.generation && bgfx::isValid(slot.vertexBuffer) &&
                        bgfx::isValid(slot.indexBuffer))
                    {
                        vb = slot.vertexBuffer;
                        ib = slot.indexBuffer;
                    }
                }
            }
            else if (batch.meshKey != Opaque3DmeshKey)
            {
                // Unbound non-fixture meshKey: skip submit rather than draw wrong geometry.
                continue;
            }

            bgfx::TextureHandle materialTexture = opaque3DDefaultTexture_;
            if (const auto matBinding = mesh3DMaterialTextureBindings_.find(batch.materialKey);
                matBinding != mesh3DMaterialTextureBindings_.end())
            {
                const GpuTextureId id = matBinding->second;
                if (id.index < textures_.size())
                {
                    const TextureSlot& slot = textures_[id.index];
                    if (slot.live && slot.generation == id.generation && bgfx::isValid(slot.handle))
                    {
                        materialTexture = slot.handle;
                    }
                }
            }

            bgfx::TextureHandle mrTexture = opaque3DDefaultMrTexture_;
            float mrMapBound = 0.0F;
            if (const auto mrBinding =
                    mesh3DMaterialMetallicRoughnessTextureBindings_.find(batch.materialKey);
                mrBinding != mesh3DMaterialMetallicRoughnessTextureBindings_.end())
            {
                const GpuTextureId id = mrBinding->second;
                if (id.index < textures_.size())
                {
                    const TextureSlot& slot = textures_[id.index];
                    if (slot.live && slot.generation == id.generation && bgfx::isValid(slot.handle))
                    {
                        mrTexture = slot.handle;
                        mrMapBound = 1.0F;
                    }
                }
            }

            bgfx::TextureHandle normalTexture = opaque3DDefaultNormalTexture_;
            float normalMapBound = 0.0F;
            if (const auto normalBinding = mesh3DMaterialNormalTextureBindings_.find(batch.materialKey);
                normalBinding != mesh3DMaterialNormalTextureBindings_.end())
            {
                const GpuTextureId id = normalBinding->second;
                if (id.index < textures_.size())
                {
                    const TextureSlot& slot = textures_[id.index];
                    if (slot.live && slot.generation == id.generation && bgfx::isValid(slot.handle))
                    {
                        normalTexture = slot.handle;
                        normalMapBound = 1.0F;
                    }
                }
            }

            // Fixed product light: no light system yet. Direction toward light in world space.
            constexpr std::array<float, 4> kDefaultLightDir{0.4F, 0.85F, 0.35F, 0.0F};
            constexpr std::array<float, 4> kDefaultLightColor{1.0F, 0.98F, 0.92F, 1.0F};
            // Factors from setMesh3DMaterialFactors (Cooked Material v2); defaults metallic=0, roughness=1.
            float metallic = 0.0F;
            float roughness = 1.0F;
            if (const auto factors = mesh3DMaterialFactors_.find(batch.materialKey);
                factors != mesh3DMaterialFactors_.end())
            {
                metallic = factors->second.metallic;
                roughness = factors->second.roughness;
            }
            // ambient=0.18; w = MR map bound flag.
            const std::array<float, 4> mrParams{metallic, roughness, 0.18F, mrMapBound};
            const std::array<float, 4> normalParams{normalMapBound, 0.0F, 0.0F, 0.0F};

            bgfx::setVertexBuffer(0, vb);
            bgfx::setIndexBuffer(ib);
            bgfx::setInstanceDataBuffer(&instanceBuffer, batch.firstItem, batch.itemCount);
            bgfx::setTexture(0, opaque3DSampler_, materialTexture);
            bgfx::setTexture(1, opaque3DMrSampler_, mrTexture);
            bgfx::setTexture(2, opaque3DNormalSampler_, normalTexture);
            bgfx::setUniform(opaque3DLightDirUniform_, kDefaultLightDir.data());
            bgfx::setUniform(opaque3DLightColorUniform_, kDefaultLightColor.data());
            bgfx::setUniform(opaque3DMrParamsUniform_, mrParams.data());
            bgfx::setUniform(opaque3DNormalParamsUniform_, normalParams.data());
            bgfx::submit(kOpaque3DView, opaque3DProgram_);
        }
    }

    void submitSprite2D(RenderSceneView scene, PreparedSprite2D prepared) noexcept
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
        auto written = writeSprite2DGeometry(scene, vertices, indices);
        if (!written || written->spriteCount != prepared.requirements.spriteCount ||
            written->vertexCount != prepared.requirements.vertexCount ||
            written->indexCount != prepared.requirements.indexCount ||
            written->batchCount != prepared.requirements.batchCount)
        {
            std::terminate();
        }

        bgfx::setScissor();
        bgfx::setState(kSprite2DPremultipliedAlphaState);
        bgfx::setVertexBuffer(0, &transientVertices, 0, prepared.requirements.vertexCount);

        // One submit per contiguous spriteKey batch so product textures can bind per key.
        const auto sprites = scene.sprites2D();
        u32 batchBegin = 0;
        while (batchBegin < prepared.requirements.spriteCount)
        {
            const u32 batchKey = sprites[batchBegin].spriteKey;
            u32 batchEnd = batchBegin + 1U;
            while (batchEnd < prepared.requirements.spriteCount && sprites[batchEnd].spriteKey == batchKey)
            {
                ++batchEnd;
            }

            bgfx::TextureHandle texture = sprite2DDefaultTexture_;
            if (const auto binding = spriteTextureBindings_.find(batchKey); binding != spriteTextureBindings_.end())
            {
                const GpuTextureId id = binding->second;
                if (id.index < textures_.size())
                {
                    const TextureSlot& slot = textures_[id.index];
                    if (slot.live && slot.generation == id.generation && bgfx::isValid(slot.handle))
                    {
                        texture = slot.handle;
                    }
                }
            }
            bgfx::setTexture(0, sprite2DSampler_, texture);
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
            if (!textures_[slotIndex].live)
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
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }
        slot.handle = handle;
        slot.width = desc.width;
        slot.height = desc.height;
        slot.live = true;
        ++statistics_.liveResources;
        return GpuTextureId{.index = index, .generation = slot.generation};
    }

    [[nodiscard]] Core::Status destroyTexture2D(GpuTextureId texture) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (!texture || texture.index >= textures_.size())
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        TextureSlot& slot = textures_[texture.index];
        if (!slot.live || slot.generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is stale or destroyed");
        }
        if (bgfx::isValid(slot.handle))
        {
            bgfx::destroy(slot.handle);
            slot.handle = BGFX_INVALID_HANDLE;
            --statistics_.liveResources;
        }
        slot.live = false;
        ++slot.generation;
        for (auto it = spriteTextureBindings_.begin(); it != spriteTextureBindings_.end();)
        {
            if (it->second == texture)
            {
                it = spriteTextureBindings_.erase(it);
            } else
            {
                ++it;
            }
        }
        for (auto it = mesh3DMaterialTextureBindings_.begin(); it != mesh3DMaterialTextureBindings_.end();)
        {
            if (it->second == texture)
            {
                it = mesh3DMaterialTextureBindings_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = mesh3DMaterialMetallicRoughnessTextureBindings_.begin();
             it != mesh3DMaterialMetallicRoughnessTextureBindings_.end();)
        {
            if (it->second == texture)
            {
                it = mesh3DMaterialMetallicRoughnessTextureBindings_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = mesh3DMaterialNormalTextureBindings_.begin();
             it != mesh3DMaterialNormalTextureBindings_.end();)
        {
            if (it->second == texture)
            {
                it = mesh3DMaterialNormalTextureBindings_.erase(it);
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
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (spriteKey == 0)
        {
            return Core::failure(RenderErrorCode::InvalidTextureUpload, "spriteKey must be non-zero");
        }
        if (!texture)
        {
            spriteTextureBindings_.erase(spriteKey);
            return Core::success();
        }
        if (texture.index >= textures_.size() || !textures_[texture.index].live ||
            textures_[texture.index].generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        spriteTextureBindings_[spriteKey] = texture;
        return Core::success();
    }

    // M11-E2: upload cooked/product StaticMesh (P3_N3_UV2 + U16) into private VB/IB.
    [[nodiscard]] Core::Result<GpuMeshId> createStaticMeshP3N3UV2(const StaticMeshUploadDesc& desc) override
    {
        if (auto status = validateApiThread("BgfxRenderDevice::createStaticMeshP3N3UV2"); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (desc.vertexCount == 0 || desc.indexCount == 0 || (desc.indexCount % 3U) != 0U ||
            desc.vertices.size() != static_cast<usize>(desc.vertexCount) * 8U ||
            desc.indices.size() != desc.indexCount)
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
        for (const u16 index : desc.indices)
        {
            if (static_cast<u32>(index) >= desc.vertexCount)
            {
                return Core::failure(RenderErrorCode::InvalidMeshUpload, "StaticMesh index out of range");
            }
        }

        const u32 vertexBytes = desc.vertexCount * static_cast<u32>(sizeof(float) * 8U);
        const u32 indexBytes = desc.indexCount * static_cast<u32>(sizeof(u16));
        const bgfx::Memory* vertexMemory = bgfx::copy(desc.vertices.data(), vertexBytes);
        const bgfx::VertexBufferHandle vb =
            bgfx::createVertexBuffer(vertexMemory, opaque3DVertexLayout_);
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
        for (u32 i = 0; i < static_cast<u32>(meshes_.size()); ++i)
        {
            if (!meshes_[i].live)
            {
                slotIndex = i;
                break;
            }
        }
        if (slotIndex == (std::numeric_limits<u32>::max)())
        {
            slotIndex = static_cast<u32>(meshes_.size());
            meshes_.push_back(MeshSlot{});
        }
        MeshSlot& slot = meshes_[slotIndex];
        slot.vertexBuffer = vb;
        slot.indexBuffer = ib;
        slot.vertexCount = desc.vertexCount;
        slot.indexCount = desc.indexCount;
        slot.live = true;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }
        ++statistics_.liveResources;
        ++statistics_.liveResources;
        return GpuMeshId{.index = slotIndex, .generation = slot.generation};
    }

    [[nodiscard]] Core::Status destroyStaticMesh(GpuMeshId mesh) noexcept override
    {
        if (std::this_thread::get_id() != ownerThread_)
        {
            std::terminate();
        }
        if (stopped_ || !bgfxInitialized_)
        {
            return Core::failure(RenderErrorCode::DeviceStopped, "The bgfx render device is stopped");
        }
        if (!mesh || mesh.index >= meshes_.size())
        {
            return Core::failure(RenderErrorCode::MeshNotFound, "StaticMesh handle is invalid");
        }
        MeshSlot& slot = meshes_[mesh.index];
        if (!slot.live || slot.generation != mesh.generation)
        {
            return Core::failure(RenderErrorCode::MeshNotFound, "StaticMesh handle is stale or destroyed");
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
        slot.live = false;
        ++slot.generation;
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
        if (mesh.index >= meshes_.size() || !meshes_[mesh.index].live ||
            meshes_[mesh.index].generation != mesh.generation)
        {
            return Core::failure(RenderErrorCode::MeshNotFound, "StaticMesh handle is invalid");
        }
        mesh3DBindings_[meshKey] = mesh;
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
            mesh3DMaterialTextureBindings_.erase(materialKey);
            return Core::success();
        }
        if (texture.index >= textures_.size() || !textures_[texture.index].live ||
            textures_[texture.index].generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        mesh3DMaterialTextureBindings_[materialKey] = texture;
        return Core::success();
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
            mesh3DMaterialMetallicRoughnessTextureBindings_.erase(materialKey);
            return Core::success();
        }
        if (texture.index >= textures_.size() || !textures_[texture.index].live ||
            textures_[texture.index].generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        mesh3DMaterialMetallicRoughnessTextureBindings_[materialKey] = texture;
        return Core::success();
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
        mesh3DMaterialFactors_[materialKey] = MaterialFactors{.metallic = metallic, .roughness = roughness};
        return Core::success();
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
            mesh3DMaterialNormalTextureBindings_.erase(materialKey);
            return Core::success();
        }
        if (texture.index >= textures_.size() || !textures_[texture.index].live ||
            textures_[texture.index].generation != texture.generation)
        {
            return Core::failure(RenderErrorCode::TextureNotFound, "Texture2D handle is invalid");
        }
        mesh3DMaterialNormalTextureBindings_[materialKey] = texture;
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
            bgfx::frame();
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

    void submitUI(const RenderSurfaceState& surface, UIDisplayListView displayList,
                  PreparedUIDisplayList prepared) noexcept
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

            const u32 firstIndex = batch.firstCommand * static_cast<u32>(kIndicesPerSolidQuad);
            const u32 indexCount = batch.commandCount * static_cast<u32>(kIndicesPerSolidQuad);
            bgfx::setState(kUIPremultipliedAlphaState);
            bgfx::setTexture(0, uiTexColorUniform_, texture);
            bgfx::setVertexBuffer(0, &transientVertices, 0, prepared.vertexCount);
            bgfx::setIndexBuffer(&transientIndices, firstIndex, indexCount);
            bgfx::submit(kUIView, uiSolidQuadProgram_);
        }
    }

    void submitPrimaryFrame(const RenderSurfaceState& surface, RenderSceneView scene, PreparedOpaque3D preparedOpaque3D,
                            PreparedSprite2D preparedSprite2D, UIDisplayListView displayList,
                            PreparedUIDisplayList preparedUI) noexcept
    {
        configureSurfaceClearView(surface);
        if (scene.perspectiveCamera().has_value())
        {
            configureOpaque3DView(surface, *scene.perspectiveCamera());
            submitOpaque3D(scene, preparedOpaque3D);
        }
        if (scene.camera2D().has_value())
        {
            configureSprite2DView(surface, *scene.camera2D());
            submitSprite2D(scene, preparedSprite2D);
        }
        configureUIView(surface);
        submitUI(surface, displayList, preparedUI);
    }

    Detail::RenderSurfaceStateTracker surfaceStateTracker_;
    Integration::NativeWindowSurfaceLease lease_;
    std::thread::id ownerThread_{};
    RenderSurfaceState committedSurfaceState_{};
    RenderSurfaceExtent appliedBackbuffer_ = BgfxSurfaceFramePlanner::BootstrapBackbufferExtent;
    RenderStatistics statistics_{};
    std::optional<u64> lastPresentFrameToken_{};
    u64 nextFrameIndex_ = 0;
    u64 nextSubmissionIndex_ = 0;
    bgfx::VertexLayout transientByteLayout_{};
    bgfx::VertexLayout opaque3DVertexLayout_{};
    bgfx::VertexLayout sprite2DVertexLayout_{};
    bgfx::VertexLayout uiVertexLayout_{};
    bgfx::ProgramHandle opaque3DProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DMrSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DNormalSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DLightDirUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DLightColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DMrParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle opaque3DNormalParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultMrTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle opaque3DDefaultNormalTexture_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle opaque3DVertexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle opaque3DIndexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle sprite2DProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sprite2DSampler_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sprite2DDefaultTexture_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle uiSolidQuadProgram_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle uiSolidWhiteTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle uiGlyphAtlasTexture_ = BGFX_INVALID_HANDLE;
    UIAtlasPageSize uiGlyphAtlasPageSize_{};
    bgfx::UniformHandle uiTexColorUniform_ = BGFX_INVALID_HANDLE;

    struct TextureSlot final {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        u32 generation = 1;
        u16 width = 0;
        u16 height = 0;
        bool live = false;
    };
    std::vector<TextureSlot> textures_{};
    std::unordered_map<u32, GpuTextureId> spriteTextureBindings_{};

    struct MeshSlot final {
        bgfx::VertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
        u32 generation = 1;
        u32 vertexCount = 0;
        u32 indexCount = 0;
        bool live = false;
    };
    std::vector<MeshSlot> meshes_{};
    std::unordered_map<u32, GpuMeshId> mesh3DBindings_{};
    std::unordered_map<u32, GpuTextureId> mesh3DMaterialTextureBindings_{};
    std::unordered_map<u32, GpuTextureId> mesh3DMaterialMetallicRoughnessTextureBindings_{};
    std::unordered_map<u32, GpuTextureId> mesh3DMaterialNormalTextureBindings_{};
    struct MaterialFactors final {
        float metallic = 0.0F;
        float roughness = 1.0F;
    };
    std::unordered_map<u32, MaterialFactors> mesh3DMaterialFactors_{};

    bool frameOpen_ = false;
    bool stopped_ = false;
    bool bgfxInitialized_ = false;
    bool captureInFlight_ = false;
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
            new BgfxRenderDevice(std::move(*surfaceStateTracker), std::move(lease), initialSurface));

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
