#include "UISliderChangeCallbackRegistry.hpp"

#include <tina/core/base/ScopeExit.hpp>
#include <tina/ui/UIErrors.hpp>

#include <utility>

namespace Tina::UI::Detail {

bool UISliderChangeCallbackRegistration::hasValue() const noexcept
{
    return slider.hasValue() && callbackIndex != InvalidSliderChangeCallbackIndex && generation != 0;
}

bool UISliderChangeCallbackInvocation::hasValue() const noexcept
{
    return slider.hasValue() && callbackIndex != InvalidSliderChangeCallbackIndex && generation != 0;
}

UISliderChangeCallbackRegistry::UISliderChangeCallbackRegistry(usize nodeCapacity,
                                                               std::pmr::memory_resource& resource)
    : nodeCapacity_(nodeCapacity), callbackIndexByNodeIndex_(&resource), callbacks_(&resource),
      inactiveCallbackIndices_(&resource)
{
    callbackIndexByNodeIndex_.resize(nodeCapacity, InvalidSliderChangeCallbackIndex);
    const usize storageCapacity = nodeCapacity + 1;
    callbacks_.resize(storageCapacity);
    for (usize callbackIndex = 0; callbackIndex < storageCapacity; ++callbackIndex)
    {
        callbacks_[callbackIndex].nextFreeIndex =
            callbackIndex + 1 < storageCapacity ? static_cast<u32>(callbackIndex + 1)
                                                : InvalidSliderChangeCallbackIndex;
    }
    freeCallbackHead_ = callbacks_.empty() ? InvalidSliderChangeCallbackIndex : 0;
    inactiveCallbackIndices_.reserve(storageCapacity);
}

Core::Result<UISliderChangeCallbackRegistration>
UISliderChangeCallbackRegistry::stage(UINodeId slider, UISliderChangeCallback&& callback, bool deferReclaim)
{
    if (!slider.hasValue() || slider.index() >= callbackIndexByNodeIndex_.size())
    {
        return Core::failure(UIErrorCode::InvalidNode, "UI Slider change callback requires a valid Slider node");
    }
    if (!callback.hasValue())
    {
        return Core::failure(UIErrorCode::InvalidButtonAction, "UI Slider change callback is empty");
    }

    const u32 previousCallbackIndex = callbackIndexByNodeIndex_[slider.index()];
    const bool replacing = previousCallbackIndex < callbacks_.size() &&
                           callbacks_[previousCallbackIndex].active &&
                           callbacks_[previousCallbackIndex].node == slider;
    const u32 previousCallbackGeneration =
        replacing ? callbacks_[previousCallbackIndex].generation : 0;
    if (previousCallbackIndex != InvalidSliderChangeCallbackIndex && !replacing)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "UI Slider change callback mapping is inconsistent");
    }
    if (!replacing && activeCallbackCount_ >= nodeCapacity_)
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI Slider change callback capacity has been exhausted");
    }
    if (freeCallbackHead_ == InvalidSliderChangeCallbackIndex)
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI Slider change callback transaction storage has been exhausted");
    }

    const u32 callbackIndex = freeCallbackHead_;
    Record& record = callbacks_[callbackIndex];
    freeCallbackHead_ = record.nextFreeIndex;
    ++record.generation;
    if (record.generation == 0)
    {
        ++record.generation;
    }
    record.node = slider;
    record.nextFreeIndex = InvalidSliderChangeCallbackIndex;
    record.active = false;
    record.queuedForReclaim = false;
    record.invoking = false;
    {
        ++callbackOperationDepth_;
        auto callbackOperation = Core::makeScopeExit([this]() noexcept { --callbackOperationDepth_; });
        record.callback = std::move(callback);
    }
    reclaim(deferReclaim);

    return UISliderChangeCallbackRegistration{
        .slider = slider,
        .callbackIndex = callbackIndex,
        .previousCallbackIndex = previousCallbackIndex,
        .previousCallbackGeneration = previousCallbackGeneration,
        .generation = record.generation,
        .replacing = replacing,
    };
}

bool UISliderChangeCallbackRegistry::canCommit(
    const UISliderChangeCallbackRegistration& registration) const noexcept
{
    if (!registration.hasValue() || registration.callbackIndex >= callbacks_.size() ||
        registration.slider.index() >= callbackIndexByNodeIndex_.size() ||
        callbackIndexByNodeIndex_[registration.slider.index()] != registration.previousCallbackIndex)
    {
        return false;
    }
    const Record& pending = callbacks_[registration.callbackIndex];
    if (pending.active || pending.node != registration.slider ||
        pending.generation != registration.generation || !pending.callback.hasValue())
    {
        return false;
    }
    if (!registration.replacing)
    {
        return registration.previousCallbackIndex == InvalidSliderChangeCallbackIndex &&
               registration.previousCallbackGeneration == 0;
    }
    if (registration.previousCallbackIndex >= callbacks_.size())
    {
        return false;
    }
    const Record& previous = callbacks_[registration.previousCallbackIndex];
    return previous.active && previous.node == registration.slider &&
           previous.generation == registration.previousCallbackGeneration;
}

void UISliderChangeCallbackRegistry::commit(const UISliderChangeCallbackRegistration& registration,
                                            bool deferReclaim) noexcept
{
    if (!canCommit(registration))
    {
        return;
    }
    Record& pending = callbacks_[registration.callbackIndex];
    pending.active = true;
    callbackIndexByNodeIndex_[registration.slider.index()] = registration.callbackIndex;
    ++activeCallbackCount_;
    if (registration.replacing)
    {
        deactivate(registration.previousCallbackIndex, deferReclaim);
    }
}

void UISliderChangeCallbackRegistry::rollback(const UISliderChangeCallbackRegistration& registration,
                                              bool deferReclaim) noexcept
{
    if (!registration.hasValue() || registration.callbackIndex >= callbacks_.size())
    {
        return;
    }
    const Record& record = callbacks_[registration.callbackIndex];
    if (record.active || record.node != registration.slider ||
        record.generation != registration.generation)
    {
        return;
    }
    deactivate(registration.callbackIndex, deferReclaim);
    reclaim(deferReclaim);
}

void UISliderChangeCallbackRegistry::clear(UINodeId slider, bool deferReclaim) noexcept
{
    if (!slider.hasValue() || slider.index() >= callbackIndexByNodeIndex_.size())
    {
        return;
    }
    const u32 callbackIndex = callbackIndexByNodeIndex_[slider.index()];
    if (callbackIndex >= callbacks_.size() || callbacks_[callbackIndex].node != slider)
    {
        return;
    }
    deactivate(callbackIndex, deferReclaim);
}

void UISliderChangeCallbackRegistry::clearNode(u32 nodeIndex, bool deferReclaim) noexcept
{
    if (nodeIndex >= callbackIndexByNodeIndex_.size())
    {
        return;
    }
    const u32 callbackIndex = callbackIndexByNodeIndex_[nodeIndex];
    callbackIndexByNodeIndex_[nodeIndex] = InvalidSliderChangeCallbackIndex;
    if (callbackIndex != InvalidSliderChangeCallbackIndex)
    {
        deactivate(callbackIndex, deferReclaim);
    }
}

UISliderChangeCallbackInvocation UISliderChangeCallbackRegistry::capture(UINodeId slider) const noexcept
{
    if (!slider.hasValue() || slider.index() >= callbackIndexByNodeIndex_.size())
    {
        return {};
    }
    const u32 callbackIndex = callbackIndexByNodeIndex_[slider.index()];
    if (callbackIndex >= callbacks_.size())
    {
        return {};
    }
    const Record& callback = callbacks_[callbackIndex];
    if (!callback.active || callback.node != slider || !callback.callback.hasValue())
    {
        return {};
    }
    return UISliderChangeCallbackInvocation{
        .slider = slider,
        .callbackIndex = callbackIndex,
        .generation = callback.generation,
    };
}

void UISliderChangeCallbackRegistry::invoke(UISliderChangeCallbackInvocation invocation,
                                            const UISliderChangeEvent& event, bool deferReclaim) noexcept
{
    if (!invocation.hasValue() || invocation.callbackIndex >= callbacks_.size())
    {
        return;
    }
    Record& callback = callbacks_[invocation.callbackIndex];
    if (!callback.active || callback.generation != invocation.generation || callback.node != invocation.slider ||
        !callback.callback.hasValue())
    {
        return;
    }

    callback.invoking = true;
    ++callbackOperationDepth_;
    callback.callback(event);
    --callbackOperationDepth_;
    if (invocation.callbackIndex < callbacks_.size())
    {
        Record& current = callbacks_[invocation.callbackIndex];
        if (current.generation == invocation.generation)
        {
            current.invoking = false;
        }
    }
    reclaim(deferReclaim);
}

void UISliderChangeCallbackRegistry::reclaim(bool deferReclaim) noexcept
{
    if (deferReclaim || callbackOperationDepth_ != 0 || reclaimingInactiveCallbacks_)
    {
        return;
    }

    reclaimingInactiveCallbacks_ = true;
    auto reclaimGuard = Core::makeScopeExit([this]() noexcept { reclaimingInactiveCallbacks_ = false; });
    while (!inactiveCallbackIndices_.empty())
    {
        const u32 callbackIndex = inactiveCallbackIndices_.back();
        inactiveCallbackIndices_.pop_back();
        if (callbackIndex >= callbacks_.size())
        {
            continue;
        }
        Record& callback = callbacks_[callbackIndex];
        callback.queuedForReclaim = false;
        if (!callback.active && !callback.invoking && callback.node.hasValue())
        {
            recycle(callbackIndex);
        }
    }
}

usize UISliderChangeCallbackRegistry::activeCount() const noexcept
{
    return activeCallbackCount_;
}

usize UISliderChangeCallbackRegistry::capacity() const noexcept
{
    return nodeCapacity_;
}

bool UISliderChangeCallbackRegistry::operationInProgress() const noexcept
{
    return callbackOperationDepth_ != 0;
}

void UISliderChangeCallbackRegistry::deactivate(u32 callbackIndex, bool deferReclaim) noexcept
{
    if (callbackIndex >= callbacks_.size())
    {
        return;
    }
    Record& callback = callbacks_[callbackIndex];
    if (!callback.node.hasValue())
    {
        return;
    }
    if (callback.node.index() < callbackIndexByNodeIndex_.size() &&
        callbackIndexByNodeIndex_[callback.node.index()] == callbackIndex)
    {
        callbackIndexByNodeIndex_[callback.node.index()] = InvalidSliderChangeCallbackIndex;
    }
    if (callback.active)
    {
        callback.active = false;
        if (activeCallbackCount_ > 0)
        {
            --activeCallbackCount_;
        }
    }
    if (deferReclaim || callbackOperationDepth_ != 0 || callback.invoking || reclaimingInactiveCallbacks_)
    {
        if (!callback.queuedForReclaim)
        {
            callback.queuedForReclaim = true;
            inactiveCallbackIndices_.push_back(callbackIndex);
        }
        return;
    }
    recycle(callbackIndex);
    reclaim(deferReclaim);
}

void UISliderChangeCallbackRegistry::recycle(u32 callbackIndex) noexcept
{
    if (callbackIndex >= callbacks_.size())
    {
        return;
    }
    Record& callback = callbacks_[callbackIndex];
    if (callback.node.hasValue() && callback.node.index() < callbackIndexByNodeIndex_.size() &&
        callbackIndexByNodeIndex_[callback.node.index()] == callbackIndex)
    {
        callbackIndexByNodeIndex_[callback.node.index()] = InvalidSliderChangeCallbackIndex;
    }

    callback.node = {};
    callback.active = false;
    callback.queuedForReclaim = false;
    callback.invoking = false;
    callback.nextFreeIndex = InvalidSliderChangeCallbackIndex;

    ++callbackOperationDepth_;
    auto callbackOperation = Core::makeScopeExit([this]() noexcept { --callbackOperationDepth_; });
    UISliderChangeCallback detachedCallback(std::move(callback.callback));

    callback.nextFreeIndex = freeCallbackHead_;
    freeCallbackHead_ = callbackIndex;
    detachedCallback.reset();
}

} // namespace Tina::UI::Detail
