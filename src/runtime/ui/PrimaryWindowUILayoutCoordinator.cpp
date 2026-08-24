#include "PrimaryWindowUILayoutCoordinator.hpp"

#include <tina/runtime/RuntimeErrors.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIPublicationPipeline.hpp>

#include <string_view>
#include <utility>

namespace Tina::Runtime::Detail {
namespace {

[[nodiscard]] Core::Status lifecycleFailure(std::string_view operation, std::string_view message,
                                            std::string_view detail = {})
{
    Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation, message};
    error.addContext(operation, detail);
    return Core::failure(std::move(error));
}

[[nodiscard]] Core::Status commitLayout(UI::UIContext& context, Platform::LogicalExtent logicalExtent,
                                        std::string_view operation)
{
    Core::Status commitStatus = context.publication().commitLayout({
        .width = static_cast<float>(logicalExtent.width),
        .height = static_cast<float>(logicalExtent.height),
    });
    if (!commitStatus)
    {
        Core::Error error = std::move(commitStatus.error());
        error.addContext(operation, "UIPublicationPipeline::commitLayout");
        return Core::failure(std::move(error));
    }
    return Core::success();
}

} // namespace

PrimaryWindowUILayoutCoordinator::PrimaryWindowUILayoutCoordinator() noexcept
    : ownerThreadId_(std::this_thread::get_id())
{
}

Core::Status
PrimaryWindowUILayoutCoordinator::commitForStartup(UI::UIContext* context,
                                                   const std::optional<Platform::WindowMetricsSnapshot>& initialMetrics)
{
    constexpr std::string_view Operation = "PrimaryWindowUILayoutCoordinator::commitForStartup";
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "PrimaryWindowUILayoutCoordinator may commit startup only from its owner thread");
    }
    if (startupAttempted_)
    {
        return lifecycleFailure(Operation, "The startup UI layout commit may be attempted only once");
    }
    startupAttempted_ = true;

    if (context == nullptr && !initialMetrics.has_value())
    {
        return Core::success();
    }
    if (context == nullptr || !initialMetrics.has_value())
    {
        return lifecycleFailure(Operation,
                                "The startup primary-window metrics and UI context must both exist or both be absent");
    }
    if (!initialMetrics->window.hasValue() || initialMetrics->revision == 0)
    {
        return lifecycleFailure(Operation, "The startup primary-window metrics have an invalid identity or revision");
    }
    if (context->ownerWindow() != initialMetrics->window)
    {
        return lifecycleFailure(Operation, "The Runtime UI context does not belong to the startup primary window");
    }
    return commitLayout(*context, initialMetrics->logicalExtent, Operation);
}

Core::Status PrimaryWindowUILayoutCoordinator::commitForFrame(UI::UIContext* context,
                                                              const Platform::PlatformFrameView& platformFrame)
{
    constexpr std::string_view Operation = "PrimaryWindowUILayoutCoordinator::commitForFrame";
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "PrimaryWindowUILayoutCoordinator may commit only from its owner thread");
    }
    if (!startupAttempted_)
    {
        return lifecycleFailure(Operation, "The startup UI layout commit must precede frame layout commits");
    }

    const Platform::PlatformFrameId frameId = platformFrame.id();
    if (!frameId.hasValue())
    {
        return lifecycleFailure(Operation, "A UI layout commit requires a valid PlatformFrameId");
    }
    if (lastAttemptedFrame_.hasValue() && frameId <= lastAttemptedFrame_)
    {
        return lifecycleFailure(Operation, "A UI layout commit requires a strictly newer PlatformFrameId");
    }
    lastAttemptedFrame_ = frameId;

    const Platform::WindowFrameSnapshot* primaryWindow = platformFrame.primaryWindow();
    if (primaryWindow == nullptr && context == nullptr)
    {
        return Core::success();
    }
    if (primaryWindow == nullptr || context == nullptr)
    {
        return lifecycleFailure(Operation,
                                "The primary window and Runtime UI context must either both exist or both be absent");
    }

    const Platform::WindowId frameWindow = primaryWindow->metrics.window;
    if (!frameWindow.hasValue() || primaryWindow->input.window != frameWindow ||
        primaryWindow->input.sourceMetricsRevision != primaryWindow->metrics.revision)
    {
        return lifecycleFailure(Operation,
                                "The primary Platform snapshot has an invalid or inconsistent identity/revision");
    }
    if (context->ownerWindow() != frameWindow)
    {
        return lifecycleFailure(Operation, "The Runtime UI context does not belong to the primary window");
    }

    return commitLayout(*context, primaryWindow->metrics.logicalExtent, Operation);
}

} // namespace Tina::Runtime::Detail
