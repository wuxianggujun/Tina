#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderFrame.hpp>

#include <functional>
#include <memory>
#include <optional>

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
};

using RenderDeviceFactory =
    std::move_only_function<Core::Result<std::unique_ptr<IRenderDevice>>(const RenderDeviceCreateParams&)>;

} // namespace Tina::Render
