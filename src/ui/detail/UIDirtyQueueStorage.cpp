#include "UIDirtyQueueStorage.hpp"

#include <algorithm>

namespace Tina::UI::Detail {

UIDirtyQueueStorage::UIDirtyQueueStorage(usize nodeCapacity, usize queueCapacity,
                                         std::pmr::memory_resource& resource)
    : flagsByNodeIndex_(&resource), queuedByNodeIndex_(&resource), reservedByNodeIndex_(&resource),
      queue_(&resource), routeCandidateScratch_(&resource), routeCandidateByNodeIndex_(&resource),
      queueCapacity_(queueCapacity)
{
    flagsByNodeIndex_.resize(nodeCapacity, UIDirty::None);
    queuedByNodeIndex_.resize(nodeCapacity, 0);
    reservedByNodeIndex_.resize(nodeCapacity, 0);
    queue_.reserve(queueCapacity);
    routeCandidateScratch_.reserve(nodeCapacity);
    routeCandidateByNodeIndex_.resize(nodeCapacity, 0);
}

usize UIDirtyQueueStorage::nodeCapacity() const noexcept
{
    return flagsByNodeIndex_.size();
}

usize UIDirtyQueueStorage::queueCapacity() const noexcept
{
    return queueCapacity_;
}

usize UIDirtyQueueStorage::reservationCount() const noexcept
{
    return reservationCount_;
}

usize UIDirtyQueueStorage::occupiedSlotCount() const noexcept
{
    return queue_.size() + reservationCount_;
}

usize UIDirtyQueueStorage::highWater() const noexcept
{
    return highWater_;
}

UIDirty& UIDirtyQueueStorage::flags(u32 nodeIndex) noexcept
{
    return flagsByNodeIndex_[nodeIndex];
}

UIDirty UIDirtyQueueStorage::flags(u32 nodeIndex) const noexcept
{
    return flagsByNodeIndex_[nodeIndex];
}

bool UIDirtyQueueStorage::isQueued(u32 nodeIndex) const noexcept
{
    return nodeIndex < queuedByNodeIndex_.size() && queuedByNodeIndex_[nodeIndex] != 0;
}

bool UIDirtyQueueStorage::isReserved(u32 nodeIndex) const noexcept
{
    return nodeIndex < reservedByNodeIndex_.size() && reservedByNodeIndex_[nodeIndex] != 0;
}

bool UIDirtyQueueStorage::isRouteCandidate(u32 nodeIndex) const noexcept
{
    return nodeIndex < routeCandidateByNodeIndex_.size() && routeCandidateByNodeIndex_[nodeIndex] != 0;
}

void UIDirtyQueueStorage::resetNode(u32 nodeIndex) noexcept
{
    if (nodeIndex >= flagsByNodeIndex_.size())
    {
        return;
    }
    flagsByNodeIndex_[nodeIndex] = UIDirty::None;
    queuedByNodeIndex_[nodeIndex] = 0;
    consumeReservation(nodeIndex);
    routeCandidateByNodeIndex_[nodeIndex] = 0;
}

void UIDirtyQueueStorage::compact(UIDirtyQueueEntryClassifier classifier) noexcept
{
    if (classifier.context == nullptr || classifier.classify == nullptr)
    {
        return;
    }
    usize writeIndex = 0;
    for (const UINodeId queued : queue_)
    {
        if (!queued.hasValue() || queued.index() >= queuedByNodeIndex_.size())
        {
            continue;
        }
        const UIDirtyQueueEntryDisposition disposition = classifier.classify(classifier.context, queued);
        if (disposition == UIDirtyQueueEntryDisposition::DiscardCurrent)
        {
            queuedByNodeIndex_[queued.index()] = 0;
            continue;
        }
        if (disposition == UIDirtyQueueEntryDisposition::DiscardStale)
        {
            continue;
        }
        queue_[writeIndex++] = queued;
    }
    queue_.resize(writeIndex);
}

usize UIDirtyQueueStorage::validCount(UIDirtyQueueEntryClassifier classifier) const noexcept
{
    if (classifier.context == nullptr || classifier.classify == nullptr)
    {
        return 0;
    }
    return static_cast<usize>(std::count_if(queue_.begin(), queue_.end(), [&](UINodeId queued) noexcept {
        return queued.hasValue() && queued.index() < queuedByNodeIndex_.size() &&
               classifier.classify(classifier.context, queued) == UIDirtyQueueEntryDisposition::Keep;
    }));
}

void UIDirtyQueueStorage::consumeReservation(u32 nodeIndex) noexcept
{
    if (nodeIndex >= reservedByNodeIndex_.size() || reservedByNodeIndex_[nodeIndex] == 0)
    {
        return;
    }
    reservedByNodeIndex_[nodeIndex] = 0;
    if (reservationCount_ != 0)
    {
        --reservationCount_;
    }
}

void UIDirtyQueueStorage::reserve(u32 nodeIndex) noexcept
{
    if (nodeIndex >= reservedByNodeIndex_.size() || queuedByNodeIndex_[nodeIndex] != 0 ||
        reservedByNodeIndex_[nodeIndex] != 0)
    {
        return;
    }
    reservedByNodeIndex_[nodeIndex] = 1;
    ++reservationCount_;
}

void UIDirtyQueueStorage::enqueue(UINodeId node)
{
    const u32 nodeIndex = node.index();
    if (nodeIndex >= queuedByNodeIndex_.size() || queuedByNodeIndex_[nodeIndex] != 0)
    {
        return;
    }
    consumeReservation(nodeIndex);
    queue_.push_back(node);
    queuedByNodeIndex_[nodeIndex] = 1;
    highWater_ = (std::max)(highWater_, queue_.size());
}

void UIDirtyQueueStorage::addRouteCandidate(UINodeId node)
{
    const u32 nodeIndex = node.index();
    if (!node.hasValue() || nodeIndex >= routeCandidateByNodeIndex_.size() ||
        routeCandidateByNodeIndex_[nodeIndex] != 0)
    {
        return;
    }
    routeCandidateByNodeIndex_[nodeIndex] = 1;
    routeCandidateScratch_.push_back(node);
}

std::span<const UINodeId> UIDirtyQueueStorage::routeCandidates() const noexcept
{
    return routeCandidateScratch_;
}

void UIDirtyQueueStorage::releaseRouteReservations() noexcept
{
    for (const UINodeId node : routeCandidateScratch_)
    {
        if (!node.hasValue() || node.index() >= routeCandidateByNodeIndex_.size())
        {
            continue;
        }
        consumeReservation(node.index());
        routeCandidateByNodeIndex_[node.index()] = 0;
    }
    routeCandidateScratch_.clear();
}

void UIDirtyQueueStorage::clearQueuedDirtyState() noexcept
{
    std::fill(flagsByNodeIndex_.begin(), flagsByNodeIndex_.end(), UIDirty::None);
    std::fill(queuedByNodeIndex_.begin(), queuedByNodeIndex_.end(), 0);
    queue_.clear();
}

} // namespace Tina::UI::Detail
