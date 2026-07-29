#include "UIContextLifetimeControl.hpp"

#include <exception>

namespace Tina::UI::Detail {

UIContextLifetimeControl::UIContextLifetimeControl(
    std::thread::id ownerThreadId, usize rootCapacity,
    usize routedPointerListenerCapacity)
    : ownerThreadId_(ownerThreadId),
      routedPointerListenerStates_(routedPointerListenerCapacity)
{
    deferredRootDestroys_.reserve(rootCapacity);
    deferredRoutedPointerListenerReleases_.reserve(
        routedPointerListenerCapacity);
}

void UIContextLifetimeControl::attach(UIContext& context) noexcept
{
    const std::scoped_lock lock(mutex_);
    context_ = &context;
}

void UIContextLifetimeControl::detach(UIContext& context) noexcept
{
    const std::scoped_lock lock(mutex_);
    if (context_ != &context)
    {
        return;
    }
    context_ = nullptr;
    deferredRootDestroys_.clear();
    hasDeferredRootDestroys_.store(false, std::memory_order_release);
    deferredRoutedPointerListenerReleases_.clear();
    hasDeferredRoutedPointerListenerReleases_.store(
        false, std::memory_order_release);
    for (RoutedPointerListenerTokenState& state :
         routedPointerListenerStates_)
    {
        state.active = false;
    }
}

UIContext* UIContextLifetimeControl::attachedContext() const noexcept
{
    const std::scoped_lock lock(mutex_);
    return context_;
}

void UIContextLifetimeControl::publishRoutedPointerListenerState(
    u32 slot, u32 generation, bool active) noexcept
{
    const std::scoped_lock lock(mutex_);
    if (slot >= routedPointerListenerStates_.size())
    {
        return;
    }
    RoutedPointerListenerTokenState& state =
        routedPointerListenerStates_[slot];
    if (!active && state.generation != generation)
    {
        return;
    }
    state = RoutedPointerListenerTokenState{
        .generation = generation,
        .active = active,
    };
}

UIContext* UIContextLifetimeControl::releaseRoutedPointerListener(
    u32 slot, u32 generation) noexcept
{
    const std::scoped_lock lock(mutex_);
    if (slot >= routedPointerListenerStates_.size())
    {
        return nullptr;
    }
    RoutedPointerListenerTokenState& state =
        routedPointerListenerStates_[slot];
    if (!state.active || state.generation != generation)
    {
        return nullptr;
    }

    state.active = false;
    if (context_ == nullptr)
    {
        return nullptr;
    }
    if (std::this_thread::get_id() == ownerThreadId_)
    {
        return context_;
    }
    if (deferredRoutedPointerListenerReleases_.size() ==
        deferredRoutedPointerListenerReleases_.capacity())
    {
        std::terminate();
    }
    deferredRoutedPointerListenerReleases_.push_back(
        DeferredRoutedPointerListenerRelease{
            .slot = slot,
            .generation = generation,
        });
    hasDeferredRoutedPointerListenerReleases_.store(
        true, std::memory_order_release);
    return nullptr;
}

bool UIContextLifetimeControl::isRoutedPointerListenerActive(
    u32 slot, u32 generation) const noexcept
{
    const std::scoped_lock lock(mutex_);
    if (context_ == nullptr || slot >= routedPointerListenerStates_.size())
    {
        return false;
    }
    const RoutedPointerListenerTokenState& state =
        routedPointerListenerStates_[slot];
    return state.active && state.generation == generation;
}

UIContext* UIContextLifetimeControl::releaseRoot(UINodeId root) noexcept
{
    if (!root.hasValue())
    {
        return nullptr;
    }
    const std::scoped_lock lock(mutex_);
    if (context_ == nullptr)
    {
        return nullptr;
    }
    if (std::this_thread::get_id() == ownerThreadId_)
    {
        return context_;
    }
    // One move-only owner exists per live root, so rootCapacity bounds the
    // maximum number of pending releases before the owner thread drains them.
    if (deferredRootDestroys_.size() == deferredRootDestroys_.capacity())
    {
        std::terminate();
    }
    deferredRootDestroys_.push_back(root);
    hasDeferredRootDestroys_.store(true, std::memory_order_release);
    return nullptr;
}

void UIContextLifetimeControl::takeDeferredRootDestroys(
    std::vector<UINodeId>& output) noexcept
{
    output.clear();
    if (!hasDeferredRootDestroys_.load(std::memory_order_acquire))
    {
        return;
    }
    const std::scoped_lock lock(mutex_);
    if (output.capacity() < deferredRootDestroys_.size())
    {
        std::terminate();
    }
    output.insert(output.end(), deferredRootDestroys_.begin(),
                  deferredRootDestroys_.end());
    deferredRootDestroys_.clear();
    hasDeferredRootDestroys_.store(false, std::memory_order_release);
}

void UIContextLifetimeControl::takeDeferredRoutedPointerListenerReleases(
    std::vector<DeferredRoutedPointerListenerRelease>& output) noexcept
{
    output.clear();
    if (!hasDeferredRoutedPointerListenerReleases_.load(
            std::memory_order_acquire))
    {
        return;
    }
    const std::scoped_lock lock(mutex_);
    if (output.capacity() < deferredRoutedPointerListenerReleases_.size())
    {
        std::terminate();
    }
    output.insert(output.end(),
                  deferredRoutedPointerListenerReleases_.begin(),
                  deferredRoutedPointerListenerReleases_.end());
    deferredRoutedPointerListenerReleases_.clear();
    hasDeferredRoutedPointerListenerReleases_.store(
        false, std::memory_order_release);
}

} // namespace Tina::UI::Detail
