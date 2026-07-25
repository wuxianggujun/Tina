#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::Render::Bgfx::RetirementDetail {

enum class RetirementDisposition : Core::u8 {
    QueueMarker,
    DestroyImmediately,
    RejectExternalPin,
};

[[nodiscard]] constexpr RetirementDisposition
selectRetirementDisposition(bool markerSupported, bool hasCompletionPin) noexcept
{
    if (markerSupported)
    {
        return RetirementDisposition::QueueMarker;
    }
    return hasCompletionPin ? RetirementDisposition::RejectExternalPin
                            : RetirementDisposition::DestroyImmediately;
}

// CPU-side accounting for one readTexture completion marker at a time.
// The ready frame is supplied by bgfx::readTexture(); ordinary bgfx frame
// numbers are only compared against that proven completion point.
class BgfxRetirementTimeline final {
  public:
    void queue(Core::u32 count = 1) noexcept
    {
        queued_ += count;
    }

    [[nodiscard]] bool needsMarker() const noexcept
    {
        return queued_ != 0 && !markerInFlight_;
    }

    [[nodiscard]] bool beginMarker(Core::u32 readyFrame) noexcept
    {
        if (!needsMarker())
        {
            return false;
        }
        waiting_ = queued_;
        queued_ = 0;
        readyFrame_ = readyFrame;
        markerInFlight_ = true;
        return true;
    }

    // Returns the number of retirements covered by the completed marker.
    [[nodiscard]] Core::u32 completeThrough(Core::u32 currentFrame) noexcept
    {
        if (!markerInFlight_ || !frameReached(currentFrame, readyFrame_))
        {
            return 0;
        }
        const Core::u32 completed = waiting_;
        waiting_ = 0;
        readyFrame_ = 0;
        markerInFlight_ = false;
        return completed;
    }

    void reset() noexcept
    {
        queued_ = 0;
        waiting_ = 0;
        readyFrame_ = 0;
        markerInFlight_ = false;
    }

    [[nodiscard]] Core::u32 queuedCount() const noexcept { return queued_; }
    [[nodiscard]] Core::u32 waitingCount() const noexcept { return waiting_; }
    [[nodiscard]] Core::u32 pendingCount() const noexcept { return queued_ + waiting_; }
    [[nodiscard]] bool markerInFlight() const noexcept { return markerInFlight_; }
    [[nodiscard]] Core::u32 readyFrame() const noexcept { return readyFrame_; }

    // Valid when producer/consumer frame distance is below half the uint32
    // range, which is guaranteed for bgfx readTexture's short ready horizon.
    [[nodiscard]] static constexpr bool frameReached(Core::u32 currentFrame,
                                                     Core::u32 targetFrame) noexcept
    {
        return currentFrame == targetFrame ||
               static_cast<Core::u32>(currentFrame - targetFrame) < 0x80000000U;
    }

  private:
    Core::u32 queued_ = 0;
    Core::u32 waiting_ = 0;
    Core::u32 readyFrame_ = 0;
    bool markerInFlight_ = false;
};

} // namespace Tina::Render::Bgfx::RetirementDetail
