#include <tina/core/memory/MemoryTracker.hpp>

#include <algorithm>
#include <limits>

namespace Tina::Core {
namespace {

void incrementSaturating(std::atomic<u64>& counter) noexcept
{
    auto current = counter.load(std::memory_order_relaxed);
    while (current != (std::numeric_limits<u64>::max)()
        && !counter.compare_exchange_weak(
            current,
            current + 1,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
    }
}

} // namespace

bool MemoryTracker::tryRecordAllocation(MemoryTag tag, usize bytes) noexcept
{
    if (!isValidMemoryTag(tag)) {
        return false;
    }

    Counters& counters = m_counters[memoryTagIndex(tag)];
    auto current = counters.currentBytes.load(std::memory_order_relaxed);
    do {
        if (bytes > (std::numeric_limits<usize>::max)() - current) {
            return false;
        }
    } while (!counters.currentBytes.compare_exchange_weak(
        current,
        current + bytes,
        std::memory_order_relaxed,
        std::memory_order_relaxed));

    const usize newCurrent = current + bytes;
    auto peak = counters.peakBytes.load(std::memory_order_relaxed);
    while (peak < newCurrent
        && !counters.peakBytes.compare_exchange_weak(
            peak,
            newCurrent,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
    }
    incrementSaturating(counters.allocationCount);
    return true;
}

bool MemoryTracker::tryRecordDeallocation(MemoryTag tag, usize bytes) noexcept
{
    if (!isValidMemoryTag(tag)) {
        return false;
    }

    Counters& counters = m_counters[memoryTagIndex(tag)];
    auto current = counters.currentBytes.load(std::memory_order_relaxed);
    do {
        if (bytes > current) {
            incrementSaturating(counters.invalidDeallocationCount);
            return false;
        }
    } while (!counters.currentBytes.compare_exchange_weak(
        current,
        current - bytes,
        std::memory_order_relaxed,
        std::memory_order_relaxed));

    incrementSaturating(counters.deallocationCount);
    return true;
}

void MemoryTracker::recordAllocationFailure(MemoryTag tag) noexcept
{
    if (isValidMemoryTag(tag)) {
        incrementSaturating(m_counters[memoryTagIndex(tag)].failedAllocationCount);
    }
}

void MemoryTracker::recordInvalidDeallocation(MemoryTag tag) noexcept
{
    if (isValidMemoryTag(tag)) {
        incrementSaturating(m_counters[memoryTagIndex(tag)].invalidDeallocationCount);
    }
}

MemoryStatistics MemoryTracker::snapshot(MemoryTag tag) const noexcept
{
    if (!isValidMemoryTag(tag)) {
        return {};
    }

    const Counters& counters = m_counters[memoryTagIndex(tag)];
    return MemoryStatistics{
        .currentBytes = counters.currentBytes.load(std::memory_order_relaxed),
        .peakBytes = counters.peakBytes.load(std::memory_order_relaxed),
        .allocationCount = counters.allocationCount.load(std::memory_order_relaxed),
        .deallocationCount = counters.deallocationCount.load(std::memory_order_relaxed),
        .failedAllocationCount = counters.failedAllocationCount.load(std::memory_order_relaxed),
        .invalidDeallocationCount = counters.invalidDeallocationCount.load(std::memory_order_relaxed),
    };
}

MemorySnapshot MemoryTracker::snapshot() const noexcept
{
    MemorySnapshot result{};
    for (usize index = 0; index < MemoryTagCount; ++index) {
        result[index] = snapshot(static_cast<MemoryTag>(index));
    }
    return result;
}

} // namespace Tina::Core
