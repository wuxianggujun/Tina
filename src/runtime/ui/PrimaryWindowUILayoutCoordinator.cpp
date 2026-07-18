#include "PrimaryWindowUILayoutCoordinator.hpp"

#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIContext.hpp>

#include <string_view>
#include <utility>

namespace Tina::Runtime::Detail {
namespace {

[[nodiscard]] Core::Status lifecycleFailure(std::string_view message, std::string_view detail = {})
{
    Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation, message};
    error.addContext("PrimaryWindowUILayoutCoordinator::commitForFrame", detail);
    return Core::failure(std::move(error));
}

} // namespace

PrimaryWindowUILayoutCoordinator::PrimaryWindowUILayoutCoordinator() noexcept
    : ownerThreadId_(std::this_thread::get_id())
{
}

Core::Status PrimaryWindowUILayoutCoordinator::commitForFrame(UI::UIContext* context,
                                                              const Platform::PlatformFrameView& platformFrame)
{
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "PrimaryWindowUILayoutCoordinator may commit only from its owner thread");
    }

    const Platform::PlatformFrameId frameId = platformFrame.id();
    if (!frameId.hasValue())
    {
        return lifecycleFailure("A UI layout commit requires a valid PlatformFrameId");
    }
    if (lastAttemptedFrame_.hasValue() && frameId <= lastAttemptedFrame_)
    {
        return lifecycleFailure("A UI layout commit requires a strictly newer PlatformFrameId");
    }
    lastAttemptedFrame_ = frameId;

    const Platform::WindowFrameSnapshot* primaryWindow = platformFrame.primaryWindow();
    if (primaryWindow == nullptr && context == nullptr)
    {
        return Core::success();
    }
    if (primaryWindow == nullptr || context == nullptr)
    {
        return lifecycleFailure("The primary window and Runtime UI context must either both exist or both be absent");
    }

    const Platform::WindowId frameWindow = primaryWindow->metrics.window;
    if (!frameWindow.hasValue() || primaryWindow->input.window != frameWindow)
    {
        return lifecycleFailure("The primary Platform snapshot has an invalid or inconsistent window identity");
    }
    if (context->ownerWindow() != frameWindow)
    {
        return lifecycleFailure("The Runtime UI context does not belong to the primary window");
    }

    const Platform::LogicalExtent logicalExtent = primaryWindow->metrics.logicalExtent;
    Core::Status commitStatus = context->commitLayout({
        .width = static_cast<float>(logicalExtent.width),
        .height = static_cast<float>(logicalExtent.height),
    });
    if (!commitStatus)
    {
        Core::Error error = std::move(commitStatus.error());
        error.addContext("PrimaryWindowUILayoutCoordinator::commitForFrame", "UIContext::commitLayout");
        return Core::failure(std::move(error));
    }
    return Core::success();
}

} // namespace Tina::Runtime::Detail
