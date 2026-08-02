#include "UIBehaviorStateStorage.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <utility>

namespace Tina::UI::Detail {

UIBehaviorStateStorage::UIBehaviorStateStorage(usize nodeCapacity, usize activateCapacity, usize toggleCapacity,
                                               usize rangeInputCapacity, usize textInputCapacity,
                                               usize scrollCapacity, usize selectCapacity,
                                               std::pmr::memory_resource& resource)
    : activateSlotByNodeIndex_(&resource), toggleSlotByNodeIndex_(&resource), rangeInputSlotByNodeIndex_(&resource),
      textInputSlotByNodeIndex_(&resource), scrollSlotByNodeIndex_(&resource), selectSlotByNodeIndex_(&resource),
      activateSlots_(&resource), toggleSlots_(&resource), rangeInputSlots_(&resource), textInputSlots_(&resource),
      scrollSlots_(&resource), selectSlots_(&resource)
{
    activateSlotByNodeIndex_.resize(nodeCapacity, InvalidSlot);
    toggleSlotByNodeIndex_.resize(nodeCapacity, InvalidSlot);
    rangeInputSlotByNodeIndex_.resize(nodeCapacity, InvalidSlot);
    textInputSlotByNodeIndex_.resize(nodeCapacity, InvalidSlot);
    scrollSlotByNodeIndex_.resize(nodeCapacity, InvalidSlot);
    selectSlotByNodeIndex_.resize(nodeCapacity, InvalidSlot);
    activateSlots_.resize(activateCapacity);
    toggleSlots_.resize(toggleCapacity);
    rangeInputSlots_.resize(rangeInputCapacity);
    textInputSlots_.resize(textInputCapacity);
    scrollSlots_.resize(scrollCapacity);
    selectSlots_.resize(selectCapacity);

    for (usize index = 0; index < activateCapacity; ++index)
    {
        activateSlots_[index].next = index + 1U < activateCapacity ? static_cast<u32>(index + 1U) : InvalidSlot;
    }
    for (usize index = 0; index < toggleCapacity; ++index)
    {
        toggleSlots_[index].next = index + 1U < toggleCapacity ? static_cast<u32>(index + 1U) : InvalidSlot;
    }
    for (usize index = 0; index < rangeInputCapacity; ++index)
    {
        rangeInputSlots_[index].next = index + 1U < rangeInputCapacity ? static_cast<u32>(index + 1U) : InvalidSlot;
    }
    for (usize index = 0; index < textInputCapacity; ++index)
    {
        textInputSlots_[index].next = index + 1U < textInputCapacity ? static_cast<u32>(index + 1U) : InvalidSlot;
    }
    for (usize index = 0; index < scrollCapacity; ++index)
    {
        scrollSlots_[index].next = index + 1U < scrollCapacity ? static_cast<u32>(index + 1U) : InvalidSlot;
    }
    for (usize index = 0; index < selectCapacity; ++index)
    {
        selectSlots_[index].next = index + 1U < selectCapacity ? static_cast<u32>(index + 1U) : InvalidSlot;
    }
    activateFreeHead_ = activateCapacity == 0 ? InvalidSlot : 0U;
    toggleFreeHead_ = toggleCapacity == 0 ? InvalidSlot : 0U;
    rangeInputFreeHead_ = rangeInputCapacity == 0 ? InvalidSlot : 0U;
    textInputFreeHead_ = textInputCapacity == 0 ? InvalidSlot : 0U;
    scrollFreeHead_ = scrollCapacity == 0 ? InvalidSlot : 0U;
    selectFreeHead_ = selectCapacity == 0 ? InvalidSlot : 0U;
}

Core::Status UIBehaviorStateStorage::publish(u32 nodeIndex, UIElementBehavior behaviors)
{
    if (nodeIndex >= activateSlotByNodeIndex_.size() || nodeIndex >= toggleSlotByNodeIndex_.size() ||
        nodeIndex >= rangeInputSlotByNodeIndex_.size() || nodeIndex >= textInputSlotByNodeIndex_.size() ||
        nodeIndex >= scrollSlotByNodeIndex_.size() || nodeIndex >= selectSlotByNodeIndex_.size())
    {
        return Core::failure(Core::CoreErrorCode::Internal, "UI behavior state index is out of range");
    }
    if (activateSlotByNodeIndex_[nodeIndex] != InvalidSlot || toggleSlotByNodeIndex_[nodeIndex] != InvalidSlot ||
        rangeInputSlotByNodeIndex_[nodeIndex] != InvalidSlot || textInputSlotByNodeIndex_[nodeIndex] != InvalidSlot ||
        scrollSlotByNodeIndex_[nodeIndex] != InvalidSlot || selectSlotByNodeIndex_[nodeIndex] != InvalidSlot)
    {
        return Core::failure(Core::CoreErrorCode::Internal, "UI node already owns behavior state storage");
    }

    const bool requiresActivate = hasBehavior(behaviors, UIElementBehavior::Activate);
    const bool requiresToggle = hasBehavior(behaviors, UIElementBehavior::Toggle);
    const bool requiresRangeInput = hasBehavior(behaviors, UIElementBehavior::RangeInput);
    const bool requiresTextInput = hasBehavior(behaviors, UIElementBehavior::TextInput);
    const bool requiresScroll = hasBehavior(behaviors, UIElementBehavior::Scroll);
    const bool requiresSelect = hasBehavior(behaviors, UIElementBehavior::Select);
    if (requiresActivate && (activateFreeHead_ == InvalidSlot || activateFreeHead_ >= activateSlots_.size()))
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI Activate behavior capacity has been exhausted");
    }
    if (requiresToggle && (toggleFreeHead_ == InvalidSlot || toggleFreeHead_ >= toggleSlots_.size()))
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI Toggle behavior capacity has been exhausted");
    }
    if (requiresRangeInput && (rangeInputFreeHead_ == InvalidSlot || rangeInputFreeHead_ >= rangeInputSlots_.size()))
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI RangeInput behavior capacity has been exhausted");
    }
    if (requiresTextInput && (textInputFreeHead_ == InvalidSlot || textInputFreeHead_ >= textInputSlots_.size()))
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI TextInput behavior capacity has been exhausted");
    }
    if (requiresScroll && (scrollFreeHead_ == InvalidSlot || scrollFreeHead_ >= scrollSlots_.size()))
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI Scroll behavior capacity has been exhausted");
    }
    if (requiresSelect && (selectFreeHead_ == InvalidSlot || selectFreeHead_ >= selectSlots_.size()))
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI Select behavior capacity has been exhausted");
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
    if (requiresRangeInput)
    {
        const u32 slotIndex = rangeInputFreeHead_;
        RangeInputSlot& slot = rangeInputSlots_[slotIndex];
        rangeInputFreeHead_ = slot.next;
        slot.next = InvalidSlot;
        slot.state = {};
        slot.active = true;
        rangeInputSlotByNodeIndex_[nodeIndex] = slotIndex;
        ++activeRangeInputCount_;
        rangeInputHighWater_ = (std::max)(rangeInputHighWater_, activeRangeInputCount_);
    }
    if (requiresTextInput)
    {
        const u32 slotIndex = textInputFreeHead_;
        TextInputSlot& slot = textInputSlots_[slotIndex];
        textInputFreeHead_ = slot.next;
        slot.next = InvalidSlot;
        slot.state = {};
        slot.active = true;
        textInputSlotByNodeIndex_[nodeIndex] = slotIndex;
        ++activeTextInputCount_;
        textInputHighWater_ = (std::max)(textInputHighWater_, activeTextInputCount_);
    }
    if (requiresScroll)
    {
        const u32 slotIndex = scrollFreeHead_;
        ScrollSlot& slot = scrollSlots_[slotIndex];
        scrollFreeHead_ = slot.next;
        slot.next = InvalidSlot;
        slot.state = {};
        slot.active = true;
        scrollSlotByNodeIndex_[nodeIndex] = slotIndex;
        ++activeScrollCount_;
        scrollHighWater_ = (std::max)(scrollHighWater_, activeScrollCount_);
    }
    if (requiresSelect)
    {
        const u32 slotIndex = selectFreeHead_;
        SelectSlot& slot = selectSlots_[slotIndex];
        selectFreeHead_ = slot.next;
        slot.next = InvalidSlot;
        slot.state = {};
        slot.active = true;
        selectSlotByNodeIndex_[nodeIndex] = slotIndex;
        ++activeSelectCount_;
        selectHighWater_ = (std::max)(selectHighWater_, activeSelectCount_);
    }
    return Core::success();
}

void UIBehaviorStateStorage::release(u32 nodeIndex) noexcept
{
    if (nodeIndex >= activateSlotByNodeIndex_.size() || nodeIndex >= toggleSlotByNodeIndex_.size() ||
        nodeIndex >= rangeInputSlotByNodeIndex_.size() || nodeIndex >= textInputSlotByNodeIndex_.size() ||
        nodeIndex >= scrollSlotByNodeIndex_.size() || nodeIndex >= selectSlotByNodeIndex_.size())
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

    const u32 rangeInputSlotIndex = rangeInputSlotByNodeIndex_[nodeIndex];
    if (rangeInputSlotIndex != InvalidSlot && rangeInputSlotIndex < rangeInputSlots_.size())
    {
        RangeInputSlot& slot = rangeInputSlots_[rangeInputSlotIndex];
        if (slot.active)
        {
            slot = {};
            slot.next = rangeInputFreeHead_;
            rangeInputFreeHead_ = rangeInputSlotIndex;
            --activeRangeInputCount_;
        }
    }
    rangeInputSlotByNodeIndex_[nodeIndex] = InvalidSlot;

    const u32 textInputSlotIndex = textInputSlotByNodeIndex_[nodeIndex];
    if (textInputSlotIndex != InvalidSlot && textInputSlotIndex < textInputSlots_.size())
    {
        TextInputSlot& slot = textInputSlots_[textInputSlotIndex];
        if (slot.active)
        {
            slot = {};
            slot.next = textInputFreeHead_;
            textInputFreeHead_ = textInputSlotIndex;
            --activeTextInputCount_;
        }
    }
    textInputSlotByNodeIndex_[nodeIndex] = InvalidSlot;

    const u32 scrollSlotIndex = scrollSlotByNodeIndex_[nodeIndex];
    if (scrollSlotIndex != InvalidSlot && scrollSlotIndex < scrollSlots_.size())
    {
        ScrollSlot& slot = scrollSlots_[scrollSlotIndex];
        if (slot.active)
        {
            slot = {};
            slot.next = scrollFreeHead_;
            scrollFreeHead_ = scrollSlotIndex;
            --activeScrollCount_;
        }
    }
    scrollSlotByNodeIndex_[nodeIndex] = InvalidSlot;

    const u32 selectSlotIndex = selectSlotByNodeIndex_[nodeIndex];
    if (selectSlotIndex != InvalidSlot && selectSlotIndex < selectSlots_.size())
    {
        SelectSlot& slot = selectSlots_[selectSlotIndex];
        if (slot.active)
        {
            slot = {};
            slot.next = selectFreeHead_;
            selectFreeHead_ = selectSlotIndex;
            --activeSelectCount_;
        }
    }
    selectSlotByNodeIndex_[nodeIndex] = InvalidSlot;
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

UIRangeInputState* UIBehaviorStateStorage::tryRangeInputState(u32 nodeIndex) noexcept
{
    return const_cast<UIRangeInputState*>(std::as_const(*this).tryRangeInputState(nodeIndex));
}

const UIRangeInputState* UIBehaviorStateStorage::tryRangeInputState(u32 nodeIndex) const noexcept
{
    if (nodeIndex >= rangeInputSlotByNodeIndex_.size())
    {
        return nullptr;
    }
    const u32 slotIndex = rangeInputSlotByNodeIndex_[nodeIndex];
    if (slotIndex >= rangeInputSlots_.size() || !rangeInputSlots_[slotIndex].active)
    {
        return nullptr;
    }
    return &rangeInputSlots_[slotIndex].state;
}

UITextInputState* UIBehaviorStateStorage::tryTextInputState(u32 nodeIndex) noexcept
{
    return const_cast<UITextInputState*>(std::as_const(*this).tryTextInputState(nodeIndex));
}

const UITextInputState* UIBehaviorStateStorage::tryTextInputState(u32 nodeIndex) const noexcept
{
    if (nodeIndex >= textInputSlotByNodeIndex_.size())
    {
        return nullptr;
    }
    const u32 slotIndex = textInputSlotByNodeIndex_[nodeIndex];
    if (slotIndex >= textInputSlots_.size() || !textInputSlots_[slotIndex].active)
    {
        return nullptr;
    }
    return &textInputSlots_[slotIndex].state;
}

UIScrollBehaviorState* UIBehaviorStateStorage::tryScrollState(u32 nodeIndex) noexcept
{
    return const_cast<UIScrollBehaviorState*>(std::as_const(*this).tryScrollState(nodeIndex));
}

const UIScrollBehaviorState* UIBehaviorStateStorage::tryScrollState(u32 nodeIndex) const noexcept
{
    if (nodeIndex >= scrollSlotByNodeIndex_.size())
    {
        return nullptr;
    }
    const u32 slotIndex = scrollSlotByNodeIndex_[nodeIndex];
    if (slotIndex >= scrollSlots_.size() || !scrollSlots_[slotIndex].active)
    {
        return nullptr;
    }
    return &scrollSlots_[slotIndex].state;
}

UISelectBehaviorState* UIBehaviorStateStorage::trySelectState(u32 nodeIndex) noexcept
{
    return const_cast<UISelectBehaviorState*>(std::as_const(*this).trySelectState(nodeIndex));
}

const UISelectBehaviorState* UIBehaviorStateStorage::trySelectState(u32 nodeIndex) const noexcept
{
    if (nodeIndex >= selectSlotByNodeIndex_.size())
    {
        return nullptr;
    }
    const u32 slotIndex = selectSlotByNodeIndex_[nodeIndex];
    if (slotIndex >= selectSlots_.size() || !selectSlots_[slotIndex].active)
    {
        return nullptr;
    }
    return &selectSlots_[slotIndex].state;
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

usize UIBehaviorStateStorage::rangeInputCapacity() const noexcept
{
    return rangeInputSlots_.size();
}

usize UIBehaviorStateStorage::activeRangeInputCount() const noexcept
{
    return activeRangeInputCount_;
}

usize UIBehaviorStateStorage::rangeInputHighWater() const noexcept
{
    return rangeInputHighWater_;
}

usize UIBehaviorStateStorage::textInputCapacity() const noexcept
{
    return textInputSlots_.size();
}

usize UIBehaviorStateStorage::activeTextInputCount() const noexcept
{
    return activeTextInputCount_;
}

usize UIBehaviorStateStorage::textInputHighWater() const noexcept
{
    return textInputHighWater_;
}

usize UIBehaviorStateStorage::scrollCapacity() const noexcept
{
    return scrollSlots_.size();
}

usize UIBehaviorStateStorage::activeScrollCount() const noexcept
{
    return activeScrollCount_;
}

usize UIBehaviorStateStorage::scrollHighWater() const noexcept
{
    return scrollHighWater_;
}

usize UIBehaviorStateStorage::selectCapacity() const noexcept
{
    return selectSlots_.size();
}

usize UIBehaviorStateStorage::activeSelectCount() const noexcept
{
    return activeSelectCount_;
}

usize UIBehaviorStateStorage::selectHighWater() const noexcept
{
    return selectHighWater_;
}

} // namespace Tina::UI::Detail
