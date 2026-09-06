#pragma once

#include <tina/core/base/Types.hpp>

#include <limits>
#include <memory_resource>
#include <utility>
#include <vector>

// Indexed binary heap shared by every navigation solver. Templated on the record
// storage and the priority predicate, so it carries no notion of dimension: the
// only requirement is that Records[index] exposes a mutable u32 heapIndex.
namespace Tina::Navigation::Detail {

inline constexpr Core::u32 NavigationInvalidIndex = (std::numeric_limits<Core::u32>::max)();

template <typename Records>
void navigationHeapSwap(std::pmr::vector<Core::u32>& heap, Records& records,
                        Core::u32 left, Core::u32 right) noexcept
{
    std::swap(heap[left], heap[right]);
    records[heap[left]].heapIndex = left;
    records[heap[right]].heapIndex = right;
}

template <typename Records, typename Priority>
void navigationHeapSiftUp(std::pmr::vector<Core::u32>& heap, Records& records,
                          Core::u32 index, Priority&& higherPriority) noexcept
{
    while (index != 0U)
    {
        const Core::u32 parent = (index - 1U) / 2U;
        if (!higherPriority(heap[index], heap[parent])) { return; }
        navigationHeapSwap(heap, records, index, parent);
        index = parent;
    }
}

template <typename Records, typename Priority>
void navigationHeapPush(std::pmr::vector<Core::u32>& heap, Records& records,
                        Core::u32 cellIndex, Priority&& higherPriority) noexcept
{
    records[cellIndex].heapIndex = static_cast<Core::u32>(heap.size());
    heap.push_back(cellIndex);
    navigationHeapSiftUp(heap, records, records[cellIndex].heapIndex, higherPriority);
}

template <typename Records, typename Priority>
[[nodiscard]] Core::u32 navigationHeapPop(std::pmr::vector<Core::u32>& heap, Records& records,
                                         Priority&& higherPriority) noexcept
{
    const Core::u32 result = heap.front();
    records[result].heapIndex = NavigationInvalidIndex;
    const Core::u32 last = heap.back();
    heap.pop_back();
    if (heap.empty()) { return result; }
    heap.front() = last;
    records[last].heapIndex = 0;
    Core::u32 index = 0;
    for (;;)
    {
        const Core::u32 left = index * 2U + 1U;
        if (left >= heap.size()) { return result; }
        const Core::u32 right = left + 1U;
        const Core::u32 best = right < heap.size() && higherPriority(heap[right], heap[left]) ? right : left;
        if (!higherPriority(heap[best], heap[index])) { return result; }
        navigationHeapSwap(heap, records, best, index);
        index = best;
    }
}

} // namespace Tina::Navigation::Detail
