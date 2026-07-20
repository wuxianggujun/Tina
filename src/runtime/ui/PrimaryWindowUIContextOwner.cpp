#include "PrimaryWindowUIContextOwner.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

#include <cmath>
#include <exception>
#include <string_view>
#include <utility>

namespace Tina::Runtime::Detail {
namespace {

[[nodiscard]] Core::Error lifecycleError(std::string_view operation, std::string_view message,
                                         std::string_view detail = {})
{
    Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation, message};
    error.addContext(operation, detail);
    return error;
}

} // namespace

PrimaryWindowUIContextOwner::PrimaryWindowUIContextOwner(
    UI::UIContextCapacityConfig capacities,
    std::pmr::memory_resource& memoryResource,
    PrimaryWindowUIContextFactory createContext) noexcept
    : capacities_(capacities),
      memoryResource_(&memoryResource),
      createContext_(std::move(createContext)),
      ownerThreadId_(std::this_thread::get_id())
{
}

PrimaryWindowUIContextOwner::~PrimaryWindowUIContextOwner() noexcept
{
    shutdown();
}

Core::Result<UI::UIContext*>
PrimaryWindowUIContextOwner::bindForStartup(const std::optional<Platform::WindowMetricsSnapshot>& initialMetrics)
{
    constexpr std::string_view Operation = "PrimaryWindowUIContextOwner::bindForStartup";
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "PrimaryWindowUIContextOwner may bind startup only from its owner thread");
    }
    if (state_ == State::Stopped)
    {
        return Core::failure(lifecycleError(Operation, "PrimaryWindowUIContextOwner cannot bind after shutdown"));
    }
    if (state_ != State::AwaitingStartup)
    {
        return Core::failure(lifecycleError(Operation, "PrimaryWindowUIContextOwner startup was already bound"));
    }

    if (!initialMetrics.has_value())
    {
        state_ = State::Headless;
        return static_cast<UI::UIContext*>(nullptr);
    }

    const Platform::WindowMetricsSnapshot& metrics = *initialMetrics;
    if (!metrics.window.hasValue() || metrics.revision == 0)
    {
        return Core::failure(
            lifecycleError(Operation, "The startup primary-window metrics have an invalid identity or revision"));
    }
    if (metrics.logicalExtent.width == 0 || metrics.logicalExtent.height == 0)
    {
        return Core::failure(
            lifecycleError(Operation, "The startup primary-window metrics have an invalid logical extent"));
    }
    if (!std::isfinite(metrics.contentScale.x) || !std::isfinite(metrics.contentScale.y) ||
        metrics.contentScale.x <= 0.0F || metrics.contentScale.y <= 0.0F)
    {
        return Core::failure(
            lifecycleError(Operation, "The startup primary-window metrics have an invalid content scale"));
    }

    Core::Result<std::unique_ptr<UI::UIContext>> contextResult =
        createContext_
            ? createContext_(metrics.window, capacities_, *memoryResource_)
            : UI::UIContext::Create(metrics.window, capacities_, *memoryResource_);
    if (!contextResult)
    {
        Core::Error error = std::move(contextResult.error());
        error.addContext(Operation, createContext_ ? "PrimaryWindowUIContextFactory" : "UIContext::Create");
        return Core::failure(std::move(error));
    }
    if (*contextResult == nullptr)
    {
        return Core::failure(lifecycleError(Operation, "UIContext::Create returned success with a null context"));
    }

    context_ = std::move(*contextResult);
    boundWindow_ = metrics.window;
    lastMetricsRevision_ = metrics.revision;
    state_ = State::Bound;
    return context_.get();
}

Core::Result<UI::UIContext*>
PrimaryWindowUIContextOwner::selectForFrame(const Platform::PlatformFrameView& platformFrame)
{
    constexpr std::string_view Operation = "PrimaryWindowUIContextOwner::selectForFrame";
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "PrimaryWindowUIContextOwner may be selected only from its owner thread");
    }
    if (state_ == State::Stopped)
    {
        return Core::failure(
            lifecycleError(Operation, "PrimaryWindowUIContextOwner cannot select a UI context after shutdown"));
    }
    if (state_ == State::AwaitingStartup)
    {
        return Core::failure(
            lifecycleError(Operation, "PrimaryWindowUIContextOwner must be bound before frame selection"));
    }

    const Platform::WindowFrameSnapshot* primaryWindow = platformFrame.primaryWindow();
    if (state_ == State::Headless)
    {
        if (primaryWindow == nullptr)
        {
            return static_cast<UI::UIContext*>(nullptr);
        }
        return Core::failure(
            lifecycleError(Operation, "A primary window appeared after the Runtime UI owner bound Headless startup"));
    }
    if (primaryWindow == nullptr)
    {
        return Core::failure(
            lifecycleError(Operation, "The primary window disappeared after the Runtime UI context was bound"));
    }

    const Platform::WindowId frameWindow = primaryWindow->metrics.window;
    if (!frameWindow.hasValue() || primaryWindow->input.window != frameWindow ||
        primaryWindow->input.sourceMetricsRevision != primaryWindow->metrics.revision)
    {
        return Core::failure(lifecycleError(
            Operation, "The primary Platform snapshot has an invalid or inconsistent identity/revision"));
    }

    if (frameWindow != boundWindow_)
    {
        return Core::failure(
            lifecycleError(Operation, "The primary window identity changed after the Runtime UI context was bound"));
    }
    if (primaryWindow->metrics.revision < lastMetricsRevision_)
    {
        return Core::failure(
            lifecycleError(Operation, "The primary-window metrics revision moved backward after startup"));
    }
    if (context_ == nullptr || context_->ownerWindow() != boundWindow_)
    {
        return Core::failure(
            lifecycleError(Operation, "The bound Runtime UI context no longer matches its primary window identity"));
    }

    lastMetricsRevision_ = primaryWindow->metrics.revision;
    return context_.get();
}

void PrimaryWindowUIContextOwner::shutdown() noexcept
{
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        std::terminate();
    }
    if (state_ == State::Stopped)
    {
        return;
    }

    context_.reset();
    boundWindow_ = {};
    lastMetricsRevision_ = 0;
    state_ = State::Stopped;
}

} // namespace Tina::Runtime::Detail
