#include "PrimaryWindowUIContextOwner.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

#include <exception>
#include <string_view>
#include <utility>

namespace Tina::Runtime::Detail {
namespace {

[[nodiscard]] Core::Error lifecycleError(std::string_view message, std::string_view detail = {})
{
    Core::Error error{RuntimeErrorCode::LifecycleInvariantViolation, message};
    error.addContext("PrimaryWindowUIContextOwner::selectForFrame", detail);
    return error;
}

} // namespace

PrimaryWindowUIContextOwner::PrimaryWindowUIContextOwner(UI::UIContextCapacityConfig capacities,
                                                         std::pmr::memory_resource& memoryResource) noexcept
    : capacities_(capacities), memoryResource_(&memoryResource), ownerThreadId_(std::this_thread::get_id())
{
}

PrimaryWindowUIContextOwner::~PrimaryWindowUIContextOwner() noexcept
{
    shutdown();
}

Core::Result<UI::UIContext*>
PrimaryWindowUIContextOwner::selectForFrame(const Platform::PlatformFrameView& platformFrame)
{
    if (std::this_thread::get_id() != ownerThreadId_)
    {
        return Core::failure(RuntimeErrorCode::WrongOwnerThread,
                             "PrimaryWindowUIContextOwner may be selected only from its owner thread");
    }
    if (state_ == State::Stopped)
    {
        return Core::failure(lifecycleError("PrimaryWindowUIContextOwner cannot select a UI context after shutdown"));
    }

    const Platform::WindowFrameSnapshot* primaryWindow = platformFrame.primaryWindow();
    if (primaryWindow == nullptr)
    {
        if (state_ == State::Unbound)
        {
            return static_cast<UI::UIContext*>(nullptr);
        }
        return Core::failure(lifecycleError("The primary window disappeared after the Runtime UI context was bound"));
    }

    const Platform::WindowId frameWindow = primaryWindow->metrics.window;
    if (!frameWindow.hasValue() || primaryWindow->input.window != frameWindow)
    {
        return Core::failure(
            lifecycleError("The primary Platform snapshot has an invalid or inconsistent window identity"));
    }

    if (state_ == State::Bound)
    {
        if (frameWindow != boundWindow_)
        {
            return Core::failure(
                lifecycleError("The primary window identity changed after the Runtime UI context was bound"));
        }
        if (context_ == nullptr || context_->ownerWindow() != boundWindow_)
        {
            return Core::failure(
                lifecycleError("The bound Runtime UI context no longer matches its primary window identity"));
        }
        return context_.get();
    }

    auto contextResult = UI::UIContext::Create(frameWindow, capacities_, *memoryResource_);
    if (!contextResult)
    {
        Core::Error error = std::move(contextResult.error());
        error.addContext("PrimaryWindowUIContextOwner::selectForFrame", "UIContext::Create");
        return Core::failure(std::move(error));
    }
    if (*contextResult == nullptr)
    {
        return Core::failure(lifecycleError("UIContext::Create returned success with a null Runtime UI context"));
    }

    context_ = std::move(*contextResult);
    boundWindow_ = frameWindow;
    state_ = State::Bound;
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
    state_ = State::Stopped;
}

} // namespace Tina::Runtime::Detail
