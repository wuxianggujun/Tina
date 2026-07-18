#include "BgfxRenderDevice.hpp"
#include "BgfxSurfaceFramePlanner.hpp"
#include "BgfxUIDisplayGeometry.hpp"
#include "BgfxUISolidQuadShader.hpp"

#include "../../integration/WindowSurfaceLeaseAccess.hpp"
#include "../RenderSurfaceStateTracker.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/render/RenderErrors.hpp>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

constexpr bgfx::ViewId kPrimaryView = 0;
constexpr u32 kDefaultResetFlags = BGFX_RESET_VSYNC;
constexpr u32 kClearRgba = 0x102a43ff;
constexpr u16 kClearFlags = BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH;
constexpr usize kIndicesPerSolidQuad = 6;
constexpr u64 kUIPremultipliedAlphaState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

static_assert(std::is_standard_layout_v<BgfxUIDisplayVertex>);
static_assert(sizeof(BgfxUIDisplayVertex) == sizeof(float) * 2U + sizeof(u32));
static_assert(offsetof(BgfxUIDisplayVertex, x) == 0U);
static_assert(offsetof(BgfxUIDisplayVertex, y) == sizeof(float));
static_assert(offsetof(BgfxUIDisplayVertex, abgr) == sizeof(float) * 2U);

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
        if (batch.kind != UIDrawCommandKind::SolidQuad || batch.commandCount == 0)
        {
            return Core::failure(RenderErrorCode::InvalidDrawCommand,
                                 "A bgfx UI draw batch is empty or has an unsupported command kind");
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

[[nodiscard]] Core::Result<PreparedUIDisplayList>
preflightUIDisplayList(UIDisplayListView displayList, const bgfx::VertexLayout& vertexLayout)
{
    auto requirements = checkedGeometryRequirements(displayList);
    if (!requirements)
    {
        if (requirements.error().code == Core::CoreErrorCode::CapacityExceeded)
        {
            return Core::failure(transientBufferCapacityError(
                "UI DisplayList geometry exceeds the bgfx transient buffer count limits"));
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
        return Core::failure(transientBufferCapacityError(
            "UI DisplayList geometry exceeds bgfx's u32 transient buffer count API"));
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
        return Core::failure(transientBufferCapacityError(
            "The active bgfx renderer does not support 32-bit UI transient indices"));
    }
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, vertexLayout) != vertexCount ||
        bgfx::getAvailTransientIndexBuffer(indexCount, true) != indexCount)
    {
        return Core::failure(transientBufferCapacityError(
            "The bgfx transient buffers cannot hold this frame's UI DisplayList"));
    }

    return PreparedUIDisplayList{
        .vertexCount = vertexCount,
        .indexCount = indexCount,
    };
}

[[nodiscard]] BgfxScissorRect clampScissorToSurface(const UIPixelRect& clip,
                                                    RenderSurfaceExtent surfaceExtent) noexcept
{
    const i64 surfaceRight = static_cast<i64>(surfaceExtent.width);
    const i64 surfaceBottom = static_cast<i64>(surfaceExtent.height);
    const i64 clipLeft = std::clamp(static_cast<i64>(clip.x), i64{0}, surfaceRight);
    const i64 clipTop = std::clamp(static_cast<i64>(clip.y), i64{0}, surfaceBottom);
    const i64 clipRight = std::clamp(static_cast<i64>(clip.x) + static_cast<i64>(clip.width), i64{0}, surfaceRight);
    const i64 clipBottom =
        std::clamp(static_cast<i64>(clip.y) + static_cast<i64>(clip.height), i64{0}, surfaceBottom);
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

        if (!bgfx::init(init))
        {
            return bgfxInitFailed();
        }

        bgfxInitialized_ = true;
        appliedBackbuffer_ = initialBackbuffer;

        uiVertexLayout_.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();

        auto uiProgram = ShaderDetail::createUISolidQuadProgram();
        if (!uiProgram)
        {
            return Core::failure(std::move(uiProgram.error()));
        }
        uiSolidQuadProgram_ = *uiProgram;
        statistics_.liveResources = 1;
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

        PreparedUIDisplayList preparedUI{};
        if (framePlan->shouldSubmit())
        {
            auto preflight = preflightUIDisplayList(frame.primaryWindowUIDisplayList, uiVertexLayout_);
            if (!preflight)
            {
                return Core::failure(std::move(preflight.error()));
            }
            preparedUI = *preflight;
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

        submitPrimaryView(committedSurfaceState_, frame.primaryWindowUIDisplayList, preparedUI);
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

        bgfx::frame();
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
            if (bgfx::isValid(uiSolidQuadProgram_))
            {
                bgfx::destroy(uiSolidQuadProgram_);
                uiSolidQuadProgram_ = BGFX_INVALID_HANDLE;
            }
            statistics_.liveResources = 0;
            bgfx::shutdown();
            bgfxInitialized_ = false;
        }
        statistics_.liveResources = 0;

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

    void configurePrimaryView(const RenderSurfaceState& surface) noexcept
    {
        bgfx::setViewRect(kPrimaryView, 0, 0, static_cast<u16>(surface.framebufferExtent.width),
                          static_cast<u16>(surface.framebufferExtent.height));
        bgfx::setViewClear(kPrimaryView, kClearFlags, kClearRgba, 1.0F, 0);
        bgfx::setViewMode(kPrimaryView, bgfx::ViewMode::Sequential);

        float projection[16]{};
        const bgfx::Caps* const caps = bgfx::getCaps();
        bx::mtxOrtho(projection, 0.0F, static_cast<float>(surface.framebufferExtent.width),
                     static_cast<float>(surface.framebufferExtent.height), 0.0F, 0.0F, 1000.0F, 0.0F,
                     caps != nullptr && caps->homogeneousDepth);
        bgfx::setViewTransform(kPrimaryView, nullptr, projection);
        bgfx::touch(kPrimaryView);
    }

    void submitPrimaryView(const RenderSurfaceState& surface, UIDisplayListView displayList,
                           PreparedUIDisplayList prepared) noexcept
    {
        configurePrimaryView(surface);
        if (prepared.vertexCount == 0)
        {
            return;
        }

        bgfx::TransientVertexBuffer transientVertices{};
        bgfx::TransientIndexBuffer transientIndices{};
        bgfx::allocTransientVertexBuffer(&transientVertices, prepared.vertexCount, uiVertexLayout_);
        bgfx::allocTransientIndexBuffer(&transientIndices, prepared.indexCount, true);

        auto vertices = std::span{reinterpret_cast<BgfxUIDisplayVertex*>(transientVertices.data),
                                  static_cast<usize>(prepared.vertexCount)};
        auto indices = std::span{reinterpret_cast<u32*>(transientIndices.data),
                                 static_cast<usize>(prepared.indexCount)};
        auto written = writeGeometry(displayList, vertices, indices);
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

            const u32 firstIndex = batch.firstCommand * static_cast<u32>(kIndicesPerSolidQuad);
            const u32 indexCount = batch.commandCount * static_cast<u32>(kIndicesPerSolidQuad);
            bgfx::setState(kUIPremultipliedAlphaState);
            bgfx::setVertexBuffer(0, &transientVertices, 0, prepared.vertexCount);
            bgfx::setIndexBuffer(&transientIndices, firstIndex, indexCount);
            bgfx::submit(kPrimaryView, uiSolidQuadProgram_);
        }
    }

    Detail::RenderSurfaceStateTracker surfaceStateTracker_;
    Integration::NativeWindowSurfaceLease lease_;
    std::thread::id ownerThread_{};
    RenderSurfaceState committedSurfaceState_{};
    RenderSurfaceExtent appliedBackbuffer_ = BgfxSurfaceFramePlanner::BootstrapBackbufferExtent;
    RenderStatistics statistics_{};
    u64 nextFrameIndex_ = 0;
    u64 nextSubmissionIndex_ = 0;
    bgfx::VertexLayout uiVertexLayout_{};
    bgfx::ProgramHandle uiSolidQuadProgram_ = BGFX_INVALID_HANDLE;
    bool frameOpen_ = false;
    bool stopped_ = false;
    bool bgfxInitialized_ = false;
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
