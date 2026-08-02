#include "UIBehaviorStateStorage.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <string_view>
#include <utility>

namespace Tina::UI::Detail {
namespace {

using CapacityFailureMessages = std::array<std::string_view, 6>;

constexpr CapacityFailureMessages ReserveCapacityFailureMessages{
    "UI Activate behavior capacity could not be reserved",
    "UI Toggle behavior capacity could not be reserved",
    "UI RangeInput behavior capacity could not be reserved",
    "UI TextInput behavior capacity could not be reserved",
    "UI Scroll behavior capacity could not be reserved",
    "UI Select behavior capacity could not be reserved",
};

constexpr CapacityFailureMessages PublishCapacityFailureMessages{
    "UI Activate behavior capacity has been exhausted",
    "UI Toggle behavior capacity has been exhausted",
    "UI RangeInput behavior capacity has been exhausted",
    "UI TextInput behavior capacity has been exhausted",
    "UI Scroll behavior capacity has been exhausted",
    "UI Select behavior capacity has been exhausted",
};

constexpr CapacityFailureMessages ReservationQuotaFailureMessages{
    "UI Activate behavior reservation has been exhausted",
    "UI Toggle behavior reservation has been exhausted",
    "UI RangeInput behavior reservation has been exhausted",
    "UI TextInput behavior reservation has been exhausted",
    "UI Scroll behavior reservation has been exhausted",
    "UI Select behavior reservation has been exhausted",
};

[[nodiscard]] UIBehaviorStateSlotCounts requiredSlotCounts(UIElementBehavior behaviors) noexcept
{
    return {
        .activate = hasBehavior(behaviors, UIElementBehavior::Activate) ? 1U : 0U,
        .toggle = hasBehavior(behaviors, UIElementBehavior::Toggle) ? 1U : 0U,
        .range = hasBehavior(behaviors, UIElementBehavior::RangeInput) ? 1U : 0U,
        .textInput = hasBehavior(behaviors, UIElementBehavior::TextInput) ? 1U : 0U,
        .scroll = hasBehavior(behaviors, UIElementBehavior::Scroll) ? 1U : 0U,
        .selection = hasBehavior(behaviors, UIElementBehavior::Select) ? 1U : 0U,
    };
}

void addCounts(UIBehaviorStateSlotCounts& destination, const UIBehaviorStateSlotCounts& source) noexcept
{
    destination.activate += source.activate;
    destination.toggle += source.toggle;
    destination.range += source.range;
    destination.textInput += source.textInput;
    destination.scroll += source.scroll;
    destination.selection += source.selection;
}

[[nodiscard]] bool hasCounts(const UIBehaviorStateSlotCounts& counts) noexcept
{
    return counts.activate != 0 || counts.toggle != 0 || counts.range != 0 || counts.textInput != 0 ||
           counts.scroll != 0 || counts.selection != 0;
}

[[nodiscard]] constexpr usize countFailure(bool failed) noexcept
{
    return failed ? 1U : 0U;
}

[[nodiscard]] Core::Status capacityFailure(const UIBehaviorStateSlotCounts& failures,
                                           const CapacityFailureMessages& messages)
{
    if (failures.activate != 0)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, messages[0]);
    }
    if (failures.toggle != 0)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, messages[1]);
    }
    if (failures.range != 0)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, messages[2]);
    }
    if (failures.textInput != 0)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, messages[3]);
    }
    if (failures.scroll != 0)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, messages[4]);
    }
    assert(failures.selection != 0);
    return Core::failure(UIErrorCode::CapacityExceeded, messages[5]);
}

[[nodiscard]] bool hasCapacity(usize capacity, usize active, usize reserved, usize requested) noexcept
{
    return active <= capacity && reserved <= capacity - active && requested <= capacity - active - reserved;
}

[[nodiscard]] bool containsCounts(const UIBehaviorStateSlotCounts& available,
                                  const UIBehaviorStateSlotCounts& required) noexcept
{
    return required.activate <= available.activate && required.toggle <= available.toggle &&
           required.range <= available.range && required.textInput <= available.textInput &&
           required.scroll <= available.scroll && required.selection <= available.selection;
}

void subtractCounts(UIBehaviorStateSlotCounts& destination, const UIBehaviorStateSlotCounts& source) noexcept
{
    assert(containsCounts(destination, source));
    destination.activate -= source.activate;
    destination.toggle -= source.toggle;
    destination.range -= source.range;
    destination.textInput -= source.textInput;
    destination.scroll -= source.scroll;
    destination.selection -= source.selection;
}

} // namespace

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

Core::Status UIBehaviorStateStorage::validatePublishTarget(u32 nodeIndex) const
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
    return Core::success();
}

Core::Status UIBehaviorStateStorage::reserve(const UIBehaviorStateSlotCounts& requested)
{
    addCounts(counters_.requested, requested);

    const UIBehaviorStateSlotCounts& outstanding = counters_.outstandingReservations;
    const UIBehaviorStateSlotCounts failures{
        .activate = countFailure(
            !hasCapacity(activateSlots_.size(), activeActivateCount_, outstanding.activate, requested.activate)),
        .toggle = countFailure(
            !hasCapacity(toggleSlots_.size(), activeToggleCount_, outstanding.toggle, requested.toggle)),
        .range = countFailure(
            !hasCapacity(rangeInputSlots_.size(), activeRangeInputCount_, outstanding.range, requested.range)),
        .textInput = countFailure(
            !hasCapacity(textInputSlots_.size(), activeTextInputCount_, outstanding.textInput, requested.textInput)),
        .scroll = countFailure(
            !hasCapacity(scrollSlots_.size(), activeScrollCount_, outstanding.scroll, requested.scroll)),
        .selection = countFailure(
            !hasCapacity(selectSlots_.size(), activeSelectCount_, outstanding.selection, requested.selection)),
    };

    if (hasCounts(failures))
    {
        addCounts(counters_.capacityFailures, failures);
        return capacityFailure(failures, ReserveCapacityFailureMessages);
    }

    addCounts(counters_.reserved, requested);
    addCounts(counters_.outstandingReservations, requested);
    return Core::success();
}

Core::Status UIBehaviorStateStorage::publish(u32 nodeIndex, UIElementBehavior behaviors)
{
    if (Core::Status target = validatePublishTarget(nodeIndex); !target)
    {
        return target;
    }

    const UIBehaviorStateSlotCounts required = requiredSlotCounts(behaviors);
    const UIBehaviorStateSlotCounts& outstanding = counters_.outstandingReservations;
    const UIBehaviorStateSlotCounts failures{
        .activate = countFailure(
            !hasCapacity(activateSlots_.size(), activeActivateCount_, outstanding.activate, required.activate) ||
            (required.activate != 0 &&
             (activateFreeHead_ == InvalidSlot || activateFreeHead_ >= activateSlots_.size()))),
        .toggle = countFailure(
            !hasCapacity(toggleSlots_.size(), activeToggleCount_, outstanding.toggle, required.toggle) ||
            (required.toggle != 0 &&
             (toggleFreeHead_ == InvalidSlot || toggleFreeHead_ >= toggleSlots_.size()))),
        .range = countFailure(
            !hasCapacity(rangeInputSlots_.size(), activeRangeInputCount_, outstanding.range, required.range) ||
            (required.range != 0 &&
             (rangeInputFreeHead_ == InvalidSlot || rangeInputFreeHead_ >= rangeInputSlots_.size()))),
        .textInput = countFailure(
            !hasCapacity(textInputSlots_.size(), activeTextInputCount_, outstanding.textInput, required.textInput) ||
            (required.textInput != 0 &&
             (textInputFreeHead_ == InvalidSlot || textInputFreeHead_ >= textInputSlots_.size()))),
        .scroll = countFailure(
            !hasCapacity(scrollSlots_.size(), activeScrollCount_, outstanding.scroll, required.scroll) ||
            (required.scroll != 0 &&
             (scrollFreeHead_ == InvalidSlot || scrollFreeHead_ >= scrollSlots_.size()))),
        .selection = countFailure(
            !hasCapacity(selectSlots_.size(), activeSelectCount_, outstanding.selection, required.selection) ||
            (required.selection != 0 &&
             (selectFreeHead_ == InvalidSlot || selectFreeHead_ >= selectSlots_.size()))),
    };

    if (hasCounts(failures))
    {
        addCounts(counters_.capacityFailures, failures);
        return capacityFailure(failures, PublishCapacityFailureMessages);
    }

    publishPreflighted(nodeIndex, behaviors);
    addCounts(counters_.published, required);
    return Core::success();
}

Core::Status UIBehaviorStateStorage::publishReserved(u32 nodeIndex, UIElementBehavior behaviors,
                                                     UIBehaviorStateSlotCounts& remainingReservation)
{
    if (Core::Status target = validatePublishTarget(nodeIndex); !target)
    {
        return target;
    }

    const UIBehaviorStateSlotCounts required = requiredSlotCounts(behaviors);
    const UIBehaviorStateSlotCounts failures{
        .activate = countFailure(required.activate > remainingReservation.activate),
        .toggle = countFailure(required.toggle > remainingReservation.toggle),
        .range = countFailure(required.range > remainingReservation.range),
        .textInput = countFailure(required.textInput > remainingReservation.textInput),
        .scroll = countFailure(required.scroll > remainingReservation.scroll),
        .selection = countFailure(required.selection > remainingReservation.selection),
    };
    if (hasCounts(failures))
    {
        addCounts(counters_.capacityFailures, failures);
        return capacityFailure(failures, ReservationQuotaFailureMessages);
    }
    if (!containsCounts(counters_.outstandingReservations, required))
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "UI behavior reservation is not owned by this storage");
    }

    publishPreflighted(nodeIndex, behaviors);
    subtractCounts(remainingReservation, required);
    subtractCounts(counters_.outstandingReservations, required);
    addCounts(counters_.published, required);
    return Core::success();
}

void UIBehaviorStateStorage::releaseReservation(UIBehaviorStateSlotCounts& remainingReservation) noexcept
{
    subtractCounts(counters_.outstandingReservations, remainingReservation);
    remainingReservation = {};
}

void UIBehaviorStateStorage::publishPreflighted(u32 nodeIndex, UIElementBehavior behaviors) noexcept
{
    const bool requiresActivate = hasBehavior(behaviors, UIElementBehavior::Activate);
    const bool requiresToggle = hasBehavior(behaviors, UIElementBehavior::Toggle);
    const bool requiresRangeInput = hasBehavior(behaviors, UIElementBehavior::RangeInput);
    const bool requiresTextInput = hasBehavior(behaviors, UIElementBehavior::TextInput);
    const bool requiresScroll = hasBehavior(behaviors, UIElementBehavior::Scroll);
    const bool requiresSelect = hasBehavior(behaviors, UIElementBehavior::Select);

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

UIBehaviorStateStorageCounters UIBehaviorStateStorage::counters() const noexcept
{
    return counters_;
}

} // namespace Tina::UI::Detail
