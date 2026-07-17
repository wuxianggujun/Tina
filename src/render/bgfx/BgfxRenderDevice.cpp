#include "BgfxRenderDevice.hpp"
#include "BgfxSurfaceFramePlanner.hpp"

#include "../RenderSurfaceStateTracker.hpp"
#include "../../integration/WindowSurfaceLeaseAccess.hpp"

#include <tina/core/error/Error.hpp>
#include <tina/render/RenderErrors.hpp>

#include <bgfx/bgfx.h>

#include <cstdint>
#include <exception>
#include <new>
#include <string_view>
#include <thread>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

constexpr bgfx::ViewId kPrimaryView = 0;
constexpr u32 kDefaultResetFlags = BGFX_RESET_VSYNC;
constexpr u32 kClearRgba = 0x000000ff;
constexpr u16 kClearFlags = BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH;

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

class BgfxRenderDevice final : public IRenderDevice {
  public:
    BgfxRenderDevice(
        Detail::RenderSurfaceStateTracker surfaceStateTracker,
        Integration::NativeWindowSurfaceLease lease,
        RenderSurfaceState initialSurface) noexcept
        : surfaceStateTracker_(std::move(surfaceStateTracker)),
          lease_(std::move(lease)),
          ownerThread_(std::this_thread::get_id()),
          committedSurfaceState_(initialSurface)
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
        auto framePlan = BgfxSurfaceFramePlanner::planFrame(
            committedSurfaceState_, *frame.primaryWindowSurface, appliedBackbuffer_);
        if (!framePlan)
        {
            stopped_ = true;
            return Core::failure(std::move(framePlan.error()));
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

        submitClearOnlyView(committedSurfaceState_);
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
        statistics_.liveResources = 0;

        if (bgfxInitialized_)
        {
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

        Core::Error error{RenderErrorCode::WrongOwnerThread,
                          "The bgfx render device must be used on its owner thread"};
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

    void submitClearOnlyView(const RenderSurfaceState& surface) noexcept
    {
        bgfx::setViewRect(kPrimaryView,
                          0,
                          0,
                          static_cast<u16>(surface.framebufferExtent.width),
                          static_cast<u16>(surface.framebufferExtent.height));
        bgfx::setViewClear(kPrimaryView, kClearFlags, kClearRgba, 1.0F, 0);
        bgfx::touch(kPrimaryView);
    }

    Detail::RenderSurfaceStateTracker surfaceStateTracker_;
    Integration::NativeWindowSurfaceLease lease_;
    std::thread::id ownerThread_{};
    RenderSurfaceState committedSurfaceState_{};
    RenderSurfaceExtent appliedBackbuffer_ = BgfxSurfaceFramePlanner::BootstrapBackbufferExtent;
    RenderStatistics statistics_{};
    u64 nextFrameIndex_ = 0;
    u64 nextSubmissionIndex_ = 0;
    bool frameOpen_ = false;
    bool stopped_ = false;
    bool bgfxInitialized_ = false;
};

[[nodiscard]] Core::Status validateInitialSurfaceForLease(
    const RenderSurfaceState& initialSurface,
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

Core::Result<std::unique_ptr<IRenderDevice>> createBgfxRenderDevice(
    const RenderDeviceCreateParams& params,
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
        auto renderDevice = std::unique_ptr<BgfxRenderDevice>(new BgfxRenderDevice(
            std::move(*surfaceStateTracker),
            std::move(lease),
            initialSurface));

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
        return Core::failure(Core::CoreErrorCode::Internal,
                             "The bgfx render device factory threw unexpectedly");
    }
}

} // namespace Tina::Render::Bgfx
