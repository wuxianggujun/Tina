#pragma once

#include <tina/platform/SoftKeyboard.hpp>

namespace Tina::Platform::Detail {

// iOS implementation of soft keyboard capability.
//
// Wraps the pending request state that the ObjC host reads and the occlusion
// height that the host reports back from UIKeyboardFrameEndUserInfoKey. This
// class does not make a UIView the first responder itself: only UIKit code
// can do that, so the request is latched intent rather than an immediate
// action.
class IosSoftKeyboard final : public ISoftKeyboard {
  public:
    IosSoftKeyboard() noexcept = default;

    [[nodiscard]] bool pendingShowRequest() const noexcept override
    {
        return pendingShow_;
    }

    void acknowledgeShowRequest() noexcept override
    {
        if (pendingShow_)
        {
            pendingShow_ = false;
        }
    }

    [[nodiscard]] float occludedLogicalHeight() const noexcept override
    {
        return occludedLogicalHeight_;
    }

    // Called by the backend when Runtime requests show/hide.
    void requestShow() noexcept override
    {
        pendingShow_ = true;
    }

    void requestHide() noexcept override
    {
        pendingShow_ = false;
    }

    // Called by the backend when the host reports keyboard occlusion change.
    void setOccludedLogicalHeight(float height) noexcept
    {
        occludedLogicalHeight_ = height;
    }

  private:
    bool pendingShow_ = false;
    float occludedLogicalHeight_ = 0.0F;
};

} // namespace Tina::Platform::Detail
