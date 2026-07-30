#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIDirty.hpp>
#include <tina/ui/UINodeId.hpp>

#include <memory_resource>
#include <span>
#include <vector>

namespace Tina::UI::Detail {

enum class UIDirtyQueueEntryDisposition : u8 {
    Keep = 0,
    DiscardStale,
    DiscardCurrent,
};

struct UIDirtyQueueEntryClassifier final {
    using Classify = UIDirtyQueueEntryDisposition (*)(const void* context, UINodeId node) noexcept;

    const void* context = nullptr;
    Classify classify = nullptr;
};

class UIDirtyQueueStorage final {
  public:
    UIDirtyQueueStorage(usize nodeCapacity, usize queueCapacity, std::pmr::memory_resource& resource);

    [[nodiscard]] usize nodeCapacity() const noexcept;
    [[nodiscard]] usize queueCapacity() const noexcept;
    [[nodiscard]] usize reservationCount() const noexcept;
    [[nodiscard]] usize occupiedSlotCount() const noexcept;
    [[nodiscard]] usize highWater() const noexcept;

    [[nodiscard]] UIDirty& flags(u32 nodeIndex) noexcept;
    [[nodiscard]] UIDirty flags(u32 nodeIndex) const noexcept;
    [[nodiscard]] bool isQueued(u32 nodeIndex) const noexcept;
    [[nodiscard]] bool isReserved(u32 nodeIndex) const noexcept;
    [[nodiscard]] bool isRouteCandidate(u32 nodeIndex) const noexcept;

    void resetNode(u32 nodeIndex) noexcept;
    void compact(UIDirtyQueueEntryClassifier classifier) noexcept;
    [[nodiscard]] usize validCount(UIDirtyQueueEntryClassifier classifier) const noexcept;

    void reserve(u32 nodeIndex) noexcept;
    void enqueue(UINodeId node);

    void addRouteCandidate(UINodeId node);
    [[nodiscard]] std::span<const UINodeId> routeCandidates() const noexcept;
    void releaseRouteReservations() noexcept;

    void clearQueuedDirtyState() noexcept;

  private:
    void consumeReservation(u32 nodeIndex) noexcept;

    std::pmr::vector<UIDirty> flagsByNodeIndex_;
    std::pmr::vector<u8> queuedByNodeIndex_;
    std::pmr::vector<u8> reservedByNodeIndex_;
    std::pmr::vector<UINodeId> queue_;
    std::pmr::vector<UINodeId> routeCandidateScratch_;
    std::pmr::vector<u8> routeCandidateByNodeIndex_;
    usize queueCapacity_ = 0;
    usize reservationCount_ = 0;
    usize highWater_ = 0;
};

} // namespace Tina::UI::Detail
