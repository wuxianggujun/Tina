#include "UIRoutedPointerListenerRegistry.hpp"

#include <tina/core/base/ScopeExit.hpp>
#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <utility>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] bool isValidEventKind(UIRoutedPointerEventKind kind) noexcept
{
    switch (kind)
    {
    case UIRoutedPointerEventKind::Move:
    case UIRoutedPointerEventKind::ButtonDown:
    case UIRoutedPointerEventKind::ButtonUp:
    case UIRoutedPointerEventKind::Wheel:
    case UIRoutedPointerEventKind::PointerCancel:
        return true;
    }
    return false;
}

[[nodiscard]] bool isValidPhaseMask(UIEventPhaseMask phases) noexcept
{
    const u8 value = eventPhaseMaskValue(phases);
    return value != 0 && (value & ~eventPhaseMaskValue(UIEventPhaseMask::All)) == 0;
}

} // namespace

bool UIRoutedPointerListenerRegistration::hasValue() const noexcept
{
    return node.hasValue() && listenerIndex != InvalidRoutedPointerListenerIndex && generation != 0;
}

void UIRoutedPointerListenerStatePublisher::operator()(u32 slot, u32 generation, bool active) const noexcept
{
    if (publish != nullptr)
    {
        publish(context, slot, generation, active);
    }
}

UIRoutedPointerListenerRegistry::UIRoutedPointerListenerRegistry(
    usize nodeCapacity, usize listenerCapacity, std::pmr::memory_resource& resource)
    : listenerCapacity_(listenerCapacity), headByNodeIndex_(&resource), tailByNodeIndex_(&resource),
      listeners_(&resource), inactiveListenerIndices_(&resource)
{
    headByNodeIndex_.resize(nodeCapacity, InvalidRoutedPointerListenerIndex);
    tailByNodeIndex_.resize(nodeCapacity, InvalidRoutedPointerListenerIndex);
    listeners_.resize(listenerCapacity);
    for (usize listenerIndex = 0; listenerIndex < listenerCapacity; ++listenerIndex)
    {
        listeners_[listenerIndex].nextFreeIndex =
            listenerIndex + 1 < listenerCapacity ? static_cast<u32>(listenerIndex + 1)
                                                 : InvalidRoutedPointerListenerIndex;
    }
    freeListenerHead_ = listeners_.empty() ? InvalidRoutedPointerListenerIndex : 0;
    inactiveListenerIndices_.reserve(listenerCapacity);
}

Core::Result<UIRoutedPointerListenerRegistration>
UIRoutedPointerListenerRegistry::stage(UIRoutedPointerListenerDesc descriptor,
                                       UIRoutedPointerCallback&& callback, bool deferReclaim)
{
    if (!descriptor.node.hasValue() || descriptor.node.index() >= headByNodeIndex_.size())
    {
        return Core::failure(UIErrorCode::InvalidNode,
                             "UI routed pointer listener requires a valid node");
    }
    if (!isValidEventKind(descriptor.kind) || !isValidPhaseMask(descriptor.phases) ||
        !callback.hasValue())
    {
        return Core::failure(UIErrorCode::InvalidRoutedPointerListener,
                             "UI routed pointer listener descriptor or callback is invalid");
    }
    if (activeListenerCount_ >= listenerCapacity_ ||
        freeListenerHead_ == InvalidRoutedPointerListenerIndex)
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI routed pointer listener capacity has been exhausted");
    }
    if (registrationSerial_ == (std::numeric_limits<u64>::max)())
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI routed pointer listener registration serial is exhausted");
    }

    const u32 listenerIndex = freeListenerHead_;
    Record& listener = listeners_[listenerIndex];
    freeListenerHead_ = listener.nextFreeIndex;
    ++listener.generation;
    if (listener.generation == 0)
    {
        ++listener.generation;
    }
    listener.node = descriptor.node;
    listener.kind = descriptor.kind;
    listener.phases = descriptor.phases;
    listener.previousNodeListenerIndex = InvalidRoutedPointerListenerIndex;
    listener.nextNodeListenerIndex = InvalidRoutedPointerListenerIndex;
    listener.nextFreeIndex = InvalidRoutedPointerListenerIndex;
    listener.registrationSerial = 0;
    listener.active = false;
    listener.queuedForReclaim = false;
    {
        ++callbackOperationDepth_;
        auto callbackOperation = Core::makeScopeExit([this]() noexcept { --callbackOperationDepth_; });
        listener.callback = std::move(callback);
    }
    reclaim(deferReclaim);

    return UIRoutedPointerListenerRegistration{
        .node = descriptor.node,
        .listenerIndex = listenerIndex,
        .generation = listener.generation,
    };
}

bool UIRoutedPointerListenerRegistry::canCommit(
    const UIRoutedPointerListenerRegistration& registration) const noexcept
{
    if (!registration.hasValue() || registration.listenerIndex >= listeners_.size() ||
        registration.node.index() >= headByNodeIndex_.size())
    {
        return false;
    }
    const Record& listener = listeners_[registration.listenerIndex];
    return !listener.active && listener.node == registration.node &&
           listener.generation == registration.generation && listener.callback.hasValue();
}

Core::Status UIRoutedPointerListenerRegistry::commit(
    const UIRoutedPointerListenerRegistration& registration,
    UIRoutedPointerListenerStatePublisher statePublisher, bool deferReclaim) noexcept
{
    if (!canCommit(registration))
    {
        return Core::failure(UIErrorCode::InvalidRoutedPointerListener,
                             "UI routed pointer listener changed during callback transfer");
    }
    if (registrationSerial_ == (std::numeric_limits<u64>::max)())
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI routed pointer listener registration serial is exhausted");
    }

    Record& listener = listeners_[registration.listenerIndex];
    listener.previousNodeListenerIndex = tailByNodeIndex_[registration.node.index()];
    listener.registrationSerial = ++registrationSerial_;
    listener.active = true;
    if (listener.previousNodeListenerIndex != InvalidRoutedPointerListenerIndex)
    {
        listeners_[listener.previousNodeListenerIndex].nextNodeListenerIndex =
            registration.listenerIndex;
    } else
    {
        headByNodeIndex_[registration.node.index()] = registration.listenerIndex;
    }
    tailByNodeIndex_[registration.node.index()] = registration.listenerIndex;
    ++activeListenerCount_;
    highWater_ = (std::max)(highWater_, activeListenerCount_);
    statePublisher(registration.listenerIndex, registration.generation, true);
    reclaim(deferReclaim);
    return Core::success();
}

void UIRoutedPointerListenerRegistry::rollback(
    const UIRoutedPointerListenerRegistration& registration, bool deferReclaim) noexcept
{
    if (!registration.hasValue() || registration.listenerIndex >= listeners_.size())
    {
        return;
    }
    const Record& listener = listeners_[registration.listenerIndex];
    if (listener.active || listener.node != registration.node ||
        listener.generation != registration.generation)
    {
        return;
    }
    recycle(registration.listenerIndex);
    reclaim(deferReclaim);
}

bool UIRoutedPointerListenerRegistry::deactivate(
    u32 listenerIndex, u32 generation, UIRoutedPointerListenerStatePublisher statePublisher,
    bool deferReclaim) noexcept
{
    if (listenerIndex >= listeners_.size())
    {
        return false;
    }
    Record& listener = listeners_[listenerIndex];
    if (!listener.active || listener.generation != generation)
    {
        return false;
    }

    listener.active = false;
    if (activeListenerCount_ > 0)
    {
        --activeListenerCount_;
    }
    statePublisher(listenerIndex, generation, false);
    if (deferReclaim || dispatchDepth_ != 0 || callbackOperationDepth_ != 0 ||
        reclaimingInactiveListeners_)
    {
        if (!listener.queuedForReclaim)
        {
            listener.queuedForReclaim = true;
            inactiveListenerIndices_.push_back(listenerIndex);
        }
        return true;
    }
    recycle(listenerIndex);
    reclaim(deferReclaim);
    return true;
}

void UIRoutedPointerListenerRegistry::clearNode(
    u32 nodeIndex, UIRoutedPointerListenerStatePublisher statePublisher) noexcept
{
    if (nodeIndex >= headByNodeIndex_.size())
    {
        return;
    }
    u32 listenerIndex = headByNodeIndex_[nodeIndex];
    while (listenerIndex != InvalidRoutedPointerListenerIndex)
    {
        if (listenerIndex >= listeners_.size())
        {
            break;
        }
        Record& listener = listeners_[listenerIndex];
        const u32 nextListenerIndex = listener.nextNodeListenerIndex;
        if (listener.active)
        {
            static_cast<void>(deactivate(listenerIndex, listener.generation, statePublisher, true));
        }
        listenerIndex = nextListenerIndex;
    }
    headByNodeIndex_[nodeIndex] = InvalidRoutedPointerListenerIndex;
    tailByNodeIndex_[nodeIndex] = InvalidRoutedPointerListenerIndex;
}

void UIRoutedPointerListenerRegistry::resetNodeSlot(u32 nodeIndex) noexcept
{
    if (nodeIndex >= headByNodeIndex_.size())
    {
        return;
    }
    headByNodeIndex_[nodeIndex] = InvalidRoutedPointerListenerIndex;
    tailByNodeIndex_[nodeIndex] = InvalidRoutedPointerListenerIndex;
}

usize UIRoutedPointerListenerRegistry::dispatch(
    UINodeId node, UIRoutedPointerEventKind kind, UIEventPhaseMask requiredPhase,
    u64 registrationSerialBoundary, UIRoutedPointerEvent& event) noexcept
{
    if (!node.hasValue() || node.index() >= headByNodeIndex_.size())
    {
        return 0;
    }

    ++dispatchDepth_;
    auto dispatchGuard = Core::makeScopeExit([this]() noexcept { --dispatchDepth_; });
    usize invocationCount = 0;
    u32 listenerIndex = headByNodeIndex_[node.index()];
    while (listenerIndex != InvalidRoutedPointerListenerIndex)
    {
        if (listenerIndex >= listeners_.size())
        {
            break;
        }
        Record& listener = listeners_[listenerIndex];
        const u32 nextListenerIndex = listener.nextNodeListenerIndex;
        if (listener.active && listener.node == node && listener.kind == kind &&
            listener.registrationSerial != 0 &&
            listener.registrationSerial <= registrationSerialBoundary &&
            hasEventPhase(listener.phases, requiredPhase))
        {
            ++invocationCount;
            listener.callback(event);
            if (event.isImmediatePropagationStopped())
            {
                break;
            }
        }
        listenerIndex = nextListenerIndex;
    }
    return invocationCount;
}

void UIRoutedPointerListenerRegistry::reclaim(bool deferReclaim) noexcept
{
    if (deferReclaim || dispatchDepth_ != 0 || callbackOperationDepth_ != 0 ||
        reclaimingInactiveListeners_)
    {
        return;
    }

    reclaimingInactiveListeners_ = true;
    auto reclaimGuard = Core::makeScopeExit([this]() noexcept { reclaimingInactiveListeners_ = false; });
    while (!inactiveListenerIndices_.empty())
    {
        const u32 listenerIndex = inactiveListenerIndices_.back();
        inactiveListenerIndices_.pop_back();
        if (listenerIndex >= listeners_.size())
        {
            continue;
        }
        Record& listener = listeners_[listenerIndex];
        listener.queuedForReclaim = false;
        if (!listener.active && listener.node.hasValue())
        {
            recycle(listenerIndex);
        }
    }
}

usize UIRoutedPointerListenerRegistry::activeCount() const noexcept
{
    return activeListenerCount_;
}

usize UIRoutedPointerListenerRegistry::capacity() const noexcept
{
    return listenerCapacity_;
}

usize UIRoutedPointerListenerRegistry::highWater() const noexcept
{
    return highWater_;
}

u64 UIRoutedPointerListenerRegistry::registrationSerial() const noexcept
{
    return registrationSerial_;
}

bool UIRoutedPointerListenerRegistry::operationInProgress() const noexcept
{
    return dispatchDepth_ != 0 || callbackOperationDepth_ != 0;
}

void UIRoutedPointerListenerRegistry::unlink(u32 listenerIndex) noexcept
{
    if (listenerIndex >= listeners_.size())
    {
        return;
    }
    Record& listener = listeners_[listenerIndex];
    const u32 nodeIndex = listener.node.index();
    if (nodeIndex >= headByNodeIndex_.size())
    {
        return;
    }

    if (listener.previousNodeListenerIndex != InvalidRoutedPointerListenerIndex)
    {
        listeners_[listener.previousNodeListenerIndex].nextNodeListenerIndex =
            listener.nextNodeListenerIndex;
    } else if (headByNodeIndex_[nodeIndex] == listenerIndex)
    {
        headByNodeIndex_[nodeIndex] = listener.nextNodeListenerIndex;
    }
    if (listener.nextNodeListenerIndex != InvalidRoutedPointerListenerIndex)
    {
        listeners_[listener.nextNodeListenerIndex].previousNodeListenerIndex =
            listener.previousNodeListenerIndex;
    } else if (tailByNodeIndex_[nodeIndex] == listenerIndex)
    {
        tailByNodeIndex_[nodeIndex] = listener.previousNodeListenerIndex;
    }
    listener.previousNodeListenerIndex = InvalidRoutedPointerListenerIndex;
    listener.nextNodeListenerIndex = InvalidRoutedPointerListenerIndex;
}

void UIRoutedPointerListenerRegistry::recycle(u32 listenerIndex) noexcept
{
    if (listenerIndex >= listeners_.size())
    {
        return;
    }
    Record& listener = listeners_[listenerIndex];
    unlink(listenerIndex);

    listener.node = {};
    listener.kind = UIRoutedPointerEventKind::Move;
    listener.phases = UIEventPhaseMask::None;
    listener.registrationSerial = 0;
    listener.active = false;
    listener.queuedForReclaim = false;
    listener.previousNodeListenerIndex = InvalidRoutedPointerListenerIndex;
    listener.nextNodeListenerIndex = InvalidRoutedPointerListenerIndex;
    listener.nextFreeIndex = InvalidRoutedPointerListenerIndex;

    ++callbackOperationDepth_;
    auto callbackOperation = Core::makeScopeExit([this]() noexcept { --callbackOperationDepth_; });
    UIRoutedPointerCallback detachedCallback(std::move(listener.callback));

    listener.nextFreeIndex = freeListenerHead_;
    freeListenerHead_ = listenerIndex;
    detachedCallback.reset();
}

} // namespace Tina::UI::Detail
