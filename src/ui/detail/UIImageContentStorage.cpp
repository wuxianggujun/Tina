#include "UIImageContentStorage.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cassert>
#include <utility>

namespace Tina::UI::Detail {

UIImageContentStorage::UIImageContentStorage(usize nodeCapacity, usize imageCapacity,
                                             std::pmr::memory_resource& resource)
    : slotByNodeIndex_(&resource), slots_(&resource),
      rollbackSlotByNodeIndex_(&resource), rollbackSlots_(&resource)
{
    slotByNodeIndex_.resize(nodeCapacity, InvalidSlot);
    slots_.resize(imageCapacity);
    rollbackSlotByNodeIndex_.resize(nodeCapacity, InvalidSlot);
    rollbackSlots_.resize(imageCapacity);
    for (usize index = 0; index < imageCapacity; ++index)
    {
        slots_[index].next = index + 1U < imageCapacity ? static_cast<u32>(index + 1U) : InvalidSlot;
    }
    freeHead_ = imageCapacity == 0 ? InvalidSlot : 0U;
}

Core::Status UIImageContentStorage::assign(u32 nodeIndex, const UIImageContent& content)
{
    if (nodeIndex >= slotByNodeIndex_.size())
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "UI image content state index is out of range");
    }
    if (slotByNodeIndex_[nodeIndex] != InvalidSlot)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "UI node already owns image content storage");
    }
    if (freeHead_ == InvalidSlot || freeHead_ >= slots_.size())
    {
        return Core::failure(UIErrorCode::CapacityExceeded,
                             "UI image content capacity has been exhausted");
    }

    const u32 slotIndex = freeHead_;
    Slot& slot = slots_[slotIndex];
    freeHead_ = slot.next;
    slot.content = content;
    slot.next = InvalidSlot;
    slot.active = true;
    slotByNodeIndex_[nodeIndex] = slotIndex;
    ++activeCount_;
    highWater_ = (std::max)(highWater_, activeCount_);
    return Core::success();
}

Core::Status UIImageContentStorage::replace(u32 nodeIndex, const UIImageContent& content)
{
    if (nodeIndex >= slotByNodeIndex_.size())
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "UI image content state index is out of range");
    }
    const u32 slotIndex = slotByNodeIndex_[nodeIndex];
    if (slotIndex == InvalidSlot)
    {
        return assign(nodeIndex, content);
    }
    if (slotIndex >= slots_.size() || !slots_[slotIndex].active)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "UI image content storage slot ownership is invalid");
    }
    slots_[slotIndex].content = content;
    return Core::success();
}

Core::Status UIImageContentStorage::setTint(u32 nodeIndex, UIStraightSrgba8Color tint)
{
    if (nodeIndex >= slotByNodeIndex_.size())
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "UI image content state index is out of range");
    }
    const u32 slotIndex = slotByNodeIndex_[nodeIndex];
    if (slotIndex == InvalidSlot || slotIndex >= slots_.size() || !slots_[slotIndex].active)
    {
        return Core::failure(UIErrorCode::InvalidElementDescriptor,
                             "UI node does not own image content storage");
    }
    slots_[slotIndex].content.tint = tint;
    return Core::success();
}

void UIImageContentStorage::release(u32 nodeIndex) noexcept
{
    if (nodeIndex >= slotByNodeIndex_.size())
    {
        return;
    }
    const u32 slotIndex = slotByNodeIndex_[nodeIndex];
    if (slotIndex == InvalidSlot || slotIndex >= slots_.size())
    {
        slotByNodeIndex_[nodeIndex] = InvalidSlot;
        return;
    }

    Slot& slot = slots_[slotIndex];
    if (slot.active)
    {
        slot = {};
        slot.next = freeHead_;
        freeHead_ = slotIndex;
        --activeCount_;
    }
    slotByNodeIndex_[nodeIndex] = InvalidSlot;
}

void UIImageContentStorage::beginCandidateTransaction() noexcept
{
    assert(!candidateTransactionActive_);
    std::copy(slotByNodeIndex_.begin(), slotByNodeIndex_.end(),
              rollbackSlotByNodeIndex_.begin());
    std::copy(slots_.begin(), slots_.end(), rollbackSlots_.begin());
    rollbackFreeHead_ = freeHead_;
    rollbackActiveCount_ = activeCount_;
    rollbackHighWater_ = highWater_;
    candidateTransactionActive_ = true;
}

void UIImageContentStorage::commitCandidateTransaction() noexcept
{
    candidateTransactionActive_ = false;
}

void UIImageContentStorage::rollbackCandidateTransaction() noexcept
{
    if (!candidateTransactionActive_)
    {
        return;
    }
    assert(slotByNodeIndex_.size() == rollbackSlotByNodeIndex_.size());
    assert(slots_.size() == rollbackSlots_.size());
    std::copy(rollbackSlotByNodeIndex_.begin(), rollbackSlotByNodeIndex_.end(),
              slotByNodeIndex_.begin());
    std::copy(rollbackSlots_.begin(), rollbackSlots_.end(), slots_.begin());
    freeHead_ = rollbackFreeHead_;
    activeCount_ = rollbackActiveCount_;
    highWater_ = rollbackHighWater_;
    candidateTransactionActive_ = false;
}

const UIImageContent* UIImageContentStorage::get(u32 nodeIndex) const noexcept
{
    if (nodeIndex >= slotByNodeIndex_.size())
    {
        return nullptr;
    }
    const u32 slotIndex = slotByNodeIndex_[nodeIndex];
    if (slotIndex == InvalidSlot || slotIndex >= slots_.size() || !slots_[slotIndex].active)
    {
        return nullptr;
    }
    return &slots_[slotIndex].content;
}

UIImageContent* UIImageContentStorage::getMutable(u32 nodeIndex) noexcept
{
    return const_cast<UIImageContent*>(std::as_const(*this).get(nodeIndex));
}

usize UIImageContentStorage::capacity() const noexcept
{
    return slots_.size();
}

usize UIImageContentStorage::activeCount() const noexcept
{
    return activeCount_;
}

usize UIImageContentStorage::highWater() const noexcept
{
    return highWater_;
}

} // namespace Tina::UI::Detail
