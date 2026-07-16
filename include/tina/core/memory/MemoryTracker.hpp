#pragma once

#include <tina/core/memory/MemoryStatistics.hpp>
#include <tina/core/memory/MemoryTag.hpp>

#include <array>
#include <atomic>

namespace Tina::Core {

using MemorySnapshot = std::array<MemoryStatistics, MemoryTagCount>;

// Lock-free diagnostic counters for allocations routed through Tina memory resources.
// A snapshot is observational and may span concurrent counter updates.
class MemoryTracker final {
public:
    MemoryTracker() noexcept = default;

    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;
    MemoryTracker(MemoryTracker&&) = delete;
    MemoryTracker& operator=(MemoryTracker&&) = delete;

    [[nodiscard]] bool tryRecordAllocation(MemoryTag tag, usize bytes) noexcept;
    [[nodiscard]] bool tryRecordDeallocation(MemoryTag tag, usize bytes) noexcept;
    void recordAllocationFailure(MemoryTag tag) noexcept;
    void recordInvalidDeallocation(MemoryTag tag) noexcept;

    [[nodiscard]] MemoryStatistics snapshot(MemoryTag tag) const noexcept;
    [[nodiscard]] MemorySnapshot snapshot() const noexcept;

private:
    struct Counters final {
        std::atomic<usize> currentBytes{0};
        std::atomic<usize> peakBytes{0};
        std::atomic<u64> allocationCount{0};
        std::atomic<u64> deallocationCount{0};
        std::atomic<u64> failedAllocationCount{0};
        std::atomic<u64> invalidDeallocationCount{0};
    };

    std::array<Counters, MemoryTagCount> m_counters{};
};

} // namespace Tina::Core
