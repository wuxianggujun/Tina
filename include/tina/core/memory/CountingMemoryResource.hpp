#pragma once

#include <tina/core/memory/MemoryTag.hpp>

#include <memory_resource>

namespace Tina::Core {

class MemoryTracker;

// Counts only allocations explicitly routed through this resource. It is not a
// process-wide heap tracker and intentionally does not replace global new/delete.
class CountingMemoryResource final : public std::pmr::memory_resource {
public:
    CountingMemoryResource(
        MemoryTracker& tracker,
        MemoryTag tag,
        std::pmr::memory_resource& upstream) noexcept;

    CountingMemoryResource(const CountingMemoryResource&) = delete;
    CountingMemoryResource& operator=(const CountingMemoryResource&) = delete;
    CountingMemoryResource(CountingMemoryResource&&) = delete;
    CountingMemoryResource& operator=(CountingMemoryResource&&) = delete;

    [[nodiscard]] MemoryTag tag() const noexcept { return m_tag; }
    [[nodiscard]] MemoryTracker& tracker() const noexcept { return *m_tracker; }
    [[nodiscard]] std::pmr::memory_resource& upstream() const noexcept { return *m_upstream; }

private:
    void* do_allocate(usize bytes, usize alignment) override;
    void do_deallocate(void* pointer, usize bytes, usize alignment) override;
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    MemoryTracker* m_tracker = nullptr;
    MemoryTag m_tag = MemoryTag::Invalid;
    std::pmr::memory_resource* m_upstream = nullptr;
};

} // namespace Tina::Core
