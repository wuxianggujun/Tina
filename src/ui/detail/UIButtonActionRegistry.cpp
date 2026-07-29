#include "UIButtonActionRegistry.hpp"

#include <tina/core/base/ScopeExit.hpp>
#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <utility>

namespace Tina::UI::Detail {

bool UIButtonActionRegistration::hasValue() const noexcept
{
    return button.hasValue() && actionIndex != InvalidButtonActionIndex && generation != 0;
}

bool UIButtonActionInvocation::hasValue() const noexcept
{
    return button.hasValue() && actionIndex != InvalidButtonActionIndex && generation != 0;
}

UIButtonActionRegistry::UIButtonActionRegistry(usize nodeCapacity, usize actionCapacity,
                                               std::pmr::memory_resource& resource)
    : actionCapacity_(actionCapacity), actionIndexByNodeIndex_(&resource),
      clearRouteSerialByNodeIndex_(&resource), actions_(&resource),
      inactiveActionIndices_(&resource)
{
    actionIndexByNodeIndex_.resize(nodeCapacity, InvalidButtonActionIndex);
    clearRouteSerialByNodeIndex_.resize(nodeCapacity, 0);
    const usize storageCapacity = actionCapacity + 1;
    actions_.resize(storageCapacity);
    for (usize actionIndex = 0; actionIndex < storageCapacity; ++actionIndex)
    {
        actions_[actionIndex].nextFreeIndex =
            actionIndex + 1 < storageCapacity ? static_cast<u32>(actionIndex + 1)
                                              : InvalidButtonActionIndex;
    }
    freeActionHead_ = actions_.empty() ? InvalidButtonActionIndex : 0;
    inactiveActionIndices_.reserve(storageCapacity);
}

Core::Result<UIButtonActionRegistration>
UIButtonActionRegistry::stage(UINodeId button, UIButtonActionCallback&& callback,
                              bool deferReclaim)
{
    if (!button.hasValue() || button.index() >= actionIndexByNodeIndex_.size())
    {
        return Core::failure(UIErrorCode::InvalidNode,
                             "UI Button action requires a valid Button node");
    }
    if (!callback.hasValue())
    {
        return Core::failure(UIErrorCode::InvalidButtonAction,
                             "UI Button action callback is empty");
    }

    const u32 previousActionIndex = actionIndexByNodeIndex_[button.index()];
    const bool replacing = previousActionIndex < actions_.size() &&
                           actions_[previousActionIndex].active &&
                           actions_[previousActionIndex].node == button;
    const u32 previousActionGeneration =
        replacing ? actions_[previousActionIndex].generation : 0;
    if (previousActionIndex != InvalidButtonActionIndex && !replacing)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "UI Button action mapping is inconsistent");
    }
    if (!replacing && activeActionCount_ >= actionCapacity_)
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI Button action capacity has been exhausted");
    }
    if (freeActionHead_ == InvalidButtonActionIndex)
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI Button action transaction storage has been exhausted");
    }
    if (registrationSerial_ == (std::numeric_limits<u64>::max)())
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI Button action registration serial is exhausted");
    }

    const u32 actionIndex = freeActionHead_;
    Record& record = actions_[actionIndex];
    freeActionHead_ = record.nextFreeIndex;
    ++record.generation;
    if (record.generation == 0)
    {
        ++record.generation;
    }
    record.node = button;
    record.nextFreeIndex = InvalidButtonActionIndex;
    record.registrationSerial = 0;
    record.active = false;
    record.queuedForReclaim = false;
    record.invoking = false;
    {
        ++callbackOperationDepth_;
        auto callbackOperation = Core::makeScopeExit([this]() noexcept { --callbackOperationDepth_; });
        record.callback = std::move(callback);
    }
    reclaim(deferReclaim);

    return UIButtonActionRegistration{
        .button = button,
        .actionIndex = actionIndex,
        .previousActionIndex = previousActionIndex,
        .previousActionGeneration = previousActionGeneration,
        .generation = record.generation,
        .replacing = replacing,
    };
}

bool UIButtonActionRegistry::canCommit(const UIButtonActionRegistration& registration) const noexcept
{
    if (!registration.hasValue() || registration.actionIndex >= actions_.size() ||
        registration.button.index() >= actionIndexByNodeIndex_.size() ||
        actionIndexByNodeIndex_[registration.button.index()] != registration.previousActionIndex)
    {
        return false;
    }
    const Record& pending = actions_[registration.actionIndex];
    if (pending.active || pending.node != registration.button ||
        pending.generation != registration.generation || !pending.callback.hasValue())
    {
        return false;
    }
    if (!registration.replacing)
    {
        return registration.previousActionIndex == InvalidButtonActionIndex &&
               registration.previousActionGeneration == 0;
    }
    if (registration.previousActionIndex >= actions_.size())
    {
        return false;
    }
    const Record& previous = actions_[registration.previousActionIndex];
    return previous.active && previous.node == registration.button &&
           previous.generation == registration.previousActionGeneration;
}

Core::Status UIButtonActionRegistry::commit(const UIButtonActionRegistration& registration,
                                            bool deferReclaim) noexcept
{
    if (!canCommit(registration))
    {
        return Core::failure(UIErrorCode::InvalidButtonAction,
                             "UI Button action changed during callback transfer");
    }
    if (registrationSerial_ == (std::numeric_limits<u64>::max)())
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI Button action registration serial is exhausted");
    }

    Record& pending = actions_[registration.actionIndex];
    pending.registrationSerial = ++registrationSerial_;
    pending.active = true;
    actionIndexByNodeIndex_[registration.button.index()] = registration.actionIndex;
    ++activeActionCount_;
    if (registration.replacing)
    {
        deactivate(registration.previousActionIndex, deferReclaim);
    }
    highWater_ = (std::max)(highWater_, activeActionCount_);
    return Core::success();
}

void UIButtonActionRegistry::rollback(const UIButtonActionRegistration& registration,
                                      bool deferReclaim) noexcept
{
    if (!registration.hasValue() || registration.actionIndex >= actions_.size())
    {
        return;
    }
    const Record& record = actions_[registration.actionIndex];
    if (record.active || record.node != registration.button || record.generation != registration.generation)
    {
        return;
    }
    deactivate(registration.actionIndex, deferReclaim);
    reclaim(deferReclaim);
}

void UIButtonActionRegistry::clear(UINodeId button, u64 routeSerial, bool deferReclaim) noexcept
{
    if (!button.hasValue() || button.index() >= actionIndexByNodeIndex_.size())
    {
        return;
    }
    const u32 actionIndex = actionIndexByNodeIndex_[button.index()];
    if (actionIndex >= actions_.size() || actions_[actionIndex].node != button)
    {
        return;
    }
    if (routeSerial != 0)
    {
        clearRouteSerialByNodeIndex_[button.index()] = routeSerial;
    }
    deactivate(actionIndex, deferReclaim);
}

void UIButtonActionRegistry::clearNode(u32 nodeIndex, bool deferReclaim) noexcept
{
    if (nodeIndex >= actionIndexByNodeIndex_.size())
    {
        return;
    }
    clearRouteSerialByNodeIndex_[nodeIndex] = 0;
    const u32 actionIndex = actionIndexByNodeIndex_[nodeIndex];
    actionIndexByNodeIndex_[nodeIndex] = InvalidButtonActionIndex;
    if (actionIndex != InvalidButtonActionIndex)
    {
        deactivate(actionIndex, deferReclaim);
    }
}

UIButtonActionInvocation UIButtonActionRegistry::capture(
    UINodeId button, u64 registrationSerialBoundary) const noexcept
{
    if (!button.hasValue() || button.index() >= actionIndexByNodeIndex_.size())
    {
        return {};
    }
    const u32 actionIndex = actionIndexByNodeIndex_[button.index()];
    if (actionIndex >= actions_.size())
    {
        return {};
    }
    const Record& action = actions_[actionIndex];
    if (!action.active || action.node != button || action.registrationSerial == 0 ||
        action.registrationSerial > registrationSerialBoundary || !action.callback.hasValue())
    {
        return {};
    }
    return UIButtonActionInvocation{
        .button = button,
        .actionIndex = actionIndex,
        .generation = action.generation,
    };
}

void UIButtonActionRegistry::invoke(UIButtonActionInvocation invocation,
                                    const UIButtonActionEvent& event, u64 routeSerial,
                                    bool deferReclaim) noexcept
{
    if (!invocation.hasValue() || invocation.actionIndex >= actions_.size() ||
        invocation.button.index() >= clearRouteSerialByNodeIndex_.size() ||
        (routeSerial != 0 && clearRouteSerialByNodeIndex_[invocation.button.index()] == routeSerial))
    {
        return;
    }
    Record& action = actions_[invocation.actionIndex];
    if (action.generation != invocation.generation || action.node != invocation.button ||
        !action.callback.hasValue())
    {
        return;
    }

    action.invoking = true;
    ++callbackOperationDepth_;
    action.callback(event);
    --callbackOperationDepth_;
    if (invocation.actionIndex < actions_.size())
    {
        Record& current = actions_[invocation.actionIndex];
        if (current.generation == invocation.generation)
        {
            current.invoking = false;
        }
    }
    reclaim(deferReclaim);
}

void UIButtonActionRegistry::reclaim(bool deferReclaim) noexcept
{
    if (deferReclaim || callbackOperationDepth_ != 0 || reclaimingInactiveActions_)
    {
        return;
    }

    reclaimingInactiveActions_ = true;
    auto reclaimGuard = Core::makeScopeExit([this]() noexcept { reclaimingInactiveActions_ = false; });
    while (!inactiveActionIndices_.empty())
    {
        const u32 actionIndex = inactiveActionIndices_.back();
        inactiveActionIndices_.pop_back();
        if (actionIndex >= actions_.size())
        {
            continue;
        }
        Record& action = actions_[actionIndex];
        action.queuedForReclaim = false;
        if (!action.active && !action.invoking && action.node.hasValue())
        {
            recycle(actionIndex);
        }
    }
}

usize UIButtonActionRegistry::activeCount() const noexcept
{
    return activeActionCount_;
}

usize UIButtonActionRegistry::capacity() const noexcept
{
    return actionCapacity_;
}

usize UIButtonActionRegistry::highWater() const noexcept
{
    return highWater_;
}

u64 UIButtonActionRegistry::registrationSerial() const noexcept
{
    return registrationSerial_;
}

bool UIButtonActionRegistry::operationInProgress() const noexcept
{
    return callbackOperationDepth_ != 0;
}

void UIButtonActionRegistry::deactivate(u32 actionIndex, bool deferReclaim) noexcept
{
    if (actionIndex >= actions_.size())
    {
        return;
    }
    Record& action = actions_[actionIndex];
    if (!action.node.hasValue())
    {
        return;
    }
    if (action.node.index() < actionIndexByNodeIndex_.size() &&
        actionIndexByNodeIndex_[action.node.index()] == actionIndex)
    {
        actionIndexByNodeIndex_[action.node.index()] = InvalidButtonActionIndex;
    }
    if (action.active)
    {
        action.active = false;
        if (activeActionCount_ > 0)
        {
            --activeActionCount_;
        }
    }
    if (deferReclaim || callbackOperationDepth_ != 0 || action.invoking || reclaimingInactiveActions_)
    {
        if (!action.queuedForReclaim)
        {
            action.queuedForReclaim = true;
            inactiveActionIndices_.push_back(actionIndex);
        }
        return;
    }
    recycle(actionIndex);
    reclaim(deferReclaim);
}

void UIButtonActionRegistry::recycle(u32 actionIndex) noexcept
{
    if (actionIndex >= actions_.size())
    {
        return;
    }
    Record& action = actions_[actionIndex];
    if (action.node.hasValue() && action.node.index() < actionIndexByNodeIndex_.size() &&
        actionIndexByNodeIndex_[action.node.index()] == actionIndex)
    {
        actionIndexByNodeIndex_[action.node.index()] = InvalidButtonActionIndex;
    }

    action.node = {};
    action.registrationSerial = 0;
    action.active = false;
    action.queuedForReclaim = false;
    action.invoking = false;
    action.nextFreeIndex = InvalidButtonActionIndex;

    ++callbackOperationDepth_;
    auto callbackOperation = Core::makeScopeExit([this]() noexcept { --callbackOperationDepth_; });
    UIButtonActionCallback detachedCallback(std::move(action.callback));

    action.nextFreeIndex = freeActionHead_;
    freeActionHead_ = actionIndex;
    detachedCallback.reset();
}

} // namespace Tina::UI::Detail
