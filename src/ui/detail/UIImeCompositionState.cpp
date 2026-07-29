#include "UIImeCompositionState.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace Tina::UI::Detail {

Core::Status UIImeCompositionState::validateCapacity(std::string_view preeditUtf8)
{
    if (preeditUtf8.size() > MaximumPreeditBytes)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI IME preedit exceeds the fixed context buffer");
    }
    return Core::success();
}

void UIImeCompositionState::assign(std::string_view preeditUtf8, u32 cursorCodepoint, u32 codepointCount) noexcept
{
    assert(preeditUtf8.size() <= MaximumPreeditBytes);
    if (preeditUtf8.size() > MaximumPreeditBytes)
    {
        return;
    }
    if (!preeditUtf8.empty())
    {
        std::memcpy(preeditBytes_.data(), preeditUtf8.data(), preeditUtf8.size());
    }
    preeditSize_ = preeditUtf8.size();
    cursorCodepoint_ = (std::min)(cursorCodepoint, codepointCount);
    active_ = true;
}

void UIImeCompositionState::reset() noexcept
{
    preeditSize_ = 0;
    cursorCodepoint_ = 0;
    active_ = false;
}

bool UIImeCompositionState::active() const noexcept
{
    return active_;
}

std::string_view UIImeCompositionState::preeditUtf8() const noexcept
{
    if (!active_)
    {
        return {};
    }
    return std::string_view(preeditBytes_.data(), preeditSize_);
}

u32 UIImeCompositionState::cursorCodepoint() const noexcept
{
    return active_ ? cursorCodepoint_ : 0U;
}

} // namespace Tina::UI::Detail
