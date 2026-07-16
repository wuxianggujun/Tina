#include <tina/core/memory/CountingMemoryResource.hpp>

#include <tina/core/memory/MemoryTracker.hpp>

#include <bit>
#include <new>

namespace Tina::Core {

CountingMemoryResource::CountingMemoryResource(
    MemoryTracker& tracker,
    MemoryTag tag,
    std::pmr::memory_resource& upstream) noexcept
    : m_tracker(&tracker), m_tag(tag), m_upstream(&upstream)
{
}

void* CountingMemoryResource::do_allocate(usize bytes, usize alignment)
{
    if (!isValidMemoryTag(m_tag) || !std::has_single_bit(alignment)) {
        m_tracker->recordAllocationFailure(m_tag);
        throw std::bad_alloc{};
    }

    void* allocation = nullptr;
    try {
        allocation = m_upstream->allocate(bytes, alignment);
    } catch (...) {
        m_tracker->recordAllocationFailure(m_tag);
        throw;
    }

    if (!m_tracker->tryRecordAllocation(m_tag, bytes)) {
        m_upstream->deallocate(allocation, bytes, alignment);
        m_tracker->recordAllocationFailure(m_tag);
        throw std::bad_alloc{};
    }
    return allocation;
}

void CountingMemoryResource::do_deallocate(void* pointer, usize bytes, usize alignment)
{
    if (pointer == nullptr || !isValidMemoryTag(m_tag) || !std::has_single_bit(alignment)) {
        m_tracker->recordInvalidDeallocation(m_tag);
        return;
    }

    m_upstream->deallocate(pointer, bytes, alignment);
    static_cast<void>(m_tracker->tryRecordDeallocation(m_tag, bytes));
}

bool CountingMemoryResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

} // namespace Tina::Core
