#include "UIBehaviorStateStorage.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <utility>

namespace Tina::UI::Detail {

UIBehaviorStateStorage::UIBehaviorStateStorage(usize nodeCapacity, usize activateCapacity, usize toggleCapacity,
                                               std::pmr::memory_resource& resource)
    : activateSlotByNodeIndex_(&resource), toggleSlotByNodeIndex_(&resource), activateSlots_(&resource),
      toggleSlots_(&resource)
{
    activateSlotByNodeIndex_.resize(nodeCapacity, InvalidSlot);
    toggleSlotByNodeIndex_.resize(nodeCapacity, InvalidSlot);
    activateSlots_.resize(activateCapacity);
    toggleSlots_.resize(toggleCapacity);

    for (usize index = 0; index < activateCapacity; ++index)
    {
        activateSlots_[index].next = index + 1U < activateCapacity ? static_cast<u32>(index + 1U) : InvalidSlot;
    }
    for (usize index = 0; index < toggleCapacity; ++index)
    {
        toggleSlots_[index].next = index + 1U < toggleCapacity ? static_cast<u32>(index + 1U) : InvalidSlot;
    }
    activateFreeHead_ = activateCapacity == 0 ? InvalidSlot : 0U;
    toggleFreeHead_ = toggleCapacity == 0 ? InvalidSlot : 0U;
}

Core::Status UIBehaviorStateStorage::publish(u32 nodeIndex, UIElementBehavior behaviors)
{
    if (nodeIndex >= activateSlotByNodeIndex_.size() || nodeIndex >= toggleSlotByNodeIndex_.size())
    {
        return Core::failure(Core::CoreErrorCode::Internal, "UI behavior state index is out of range");
    }
    if (activateSlotByNodeIndex_[nodeIndex] != InvalidSlot || toggleSlotByNodeIndex_[nodeIndex] != InvalidSlot)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "UI node already owns behavior state storage");
    }

    const bool requiresActivate = hasBehavior(behaviors, UIElementBehavior::Activate);
    const bool requiresToggle = hasBehavior(behaviors, UIElementBehavior::Toggle);
    if (requiresActivate && (activateFreeHead_ == InvalidSlot || activateFreeHead_ >= activateSlots_.size()))
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI Activate behavior capacity has been exhausted");
    }
    if (requiresToggle && (toggleFreeHead_ == InvalidSlot || toggleFreeHead_ >= toggleSlots_.size()))
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI Toggle behavior capacity has been exhausted");
    }

    if (requiresActivate)
    {
        const u32 slotIndex = activateFreeHead_;
        ActivateSlot& slot = activateSlots_[slotIndex];
        activateFreeHead_ = slot.next;
        slot.next = InvalidSlot;
        slot.active = true;
        activateSlotByNodeIndex_[nodeIndex] = slotIndex;
        ++activeActivateCount_;
        activateHighWater_ = (std::max)(activateHighWater_, activeActivateCount_);
    }
    if (requiresToggle)
    {
        const u32 slotIndex = toggleFreeHead_;
        ToggleSlot& slot = toggleSlots_[slotIndex];
        toggleFreeHead_ = slot.next;
        slot.next = InvalidSlot;
        slot.value = 0;
        slot.active = true;
        toggleSlotByNodeIndex_[nodeIndex] = slotIndex;
        ++activeToggleCount_;
        toggleHighWater_ = (std::max)(toggleHighWater_, activeToggleCount_);
    }
    return Core::success();
}

void UIBehaviorStateStorage::release(u32 nodeIndex) noexcept
{
    if (nodeIndex >= activateSlotByNodeIndex_.size() || nodeIndex >= toggleSlotByNodeIndex_.size())
    {
        return;
    }

    const u32 activateSlotIndex = activateSlotByNodeIndex_[nodeIndex];
    if (activateSlotIndex != InvalidSlot && activateSlotIndex < activateSlots_.size())
    {
        ActivateSlot& slot = activateSlots_[activateSlotIndex];
        if (slot.active)
        {
            slot = {};
            slot.next = activateFreeHead_;
            activateFreeHead_ = activateSlotIndex;
            --activeActivateCount_;
        }
    }
    activateSlotByNodeIndex_[nodeIndex] = InvalidSlot;

    const u32 toggleSlotIndex = toggleSlotByNodeIndex_[nodeIndex];
    if (toggleSlotIndex != InvalidSlot && toggleSlotIndex < toggleSlots_.size())
    {
        ToggleSlot& slot = toggleSlots_[toggleSlotIndex];
        if (slot.active)
        {
            slot = {};
            slot.next = toggleFreeHead_;
            toggleFreeHead_ = toggleSlotIndex;
            --activeToggleCount_;
        }
    }
    toggleSlotByNodeIndex_[nodeIndex] = InvalidSlot;
}

bool UIBehaviorStateStorage::hasActivate(u32 nodeIndex) const noexcept
{
    if (nodeIndex >= activateSlotByNodeIndex_.size())
    {
        return false;
    }
    const u32 slotIndex = activateSlotByNodeIndex_[nodeIndex];
    return slotIndex < activateSlots_.size() && activateSlots_[slotIndex].active;
}

bool UIBehaviorStateStorage::hasToggle(u32 nodeIndex) const noexcept
{
    return tryToggleValue(nodeIndex) != nullptr;
}

u8* UIBehaviorStateStorage::tryToggleValue(u32 nodeIndex) noexcept
{
    return const_cast<u8*>(std::as_const(*this).tryToggleValue(nodeIndex));
}

const u8* UIBehaviorStateStorage::tryToggleValue(u32 nodeIndex) const noexcept
{
    if (nodeIndex >= toggleSlotByNodeIndex_.size())
    {
        return nullptr;
    }
    const u32 slotIndex = toggleSlotByNodeIndex_[nodeIndex];
    if (slotIndex >= toggleSlots_.size() || !toggleSlots_[slotIndex].active)
    {
        return nullptr;
    }
    return &toggleSlots_[slotIndex].value;
}

usize UIBehaviorStateStorage::activateCapacity() const noexcept
{
    return activateSlots_.size();
}

usize UIBehaviorStateStorage::activeActivateCount() const noexcept
{
    return activeActivateCount_;
}

usize UIBehaviorStateStorage::activateHighWater() const noexcept
{
    return activateHighWater_;
}

usize UIBehaviorStateStorage::toggleCapacity() const noexcept
{
    return toggleSlots_.size();
}

usize UIBehaviorStateStorage::activeToggleCount() const noexcept
{
    return activeToggleCount_;
}

usize UIBehaviorStateStorage::toggleHighWater() const noexcept
{
    return toggleHighWater_;
}

} // namespace Tina::UI::Detail
