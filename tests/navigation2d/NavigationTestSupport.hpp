#pragma once

#include <tina/navigation2d/NavigationGrid2D.hpp>

#include <memory_resource>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace Tina::Navigation2D::TestSupport {

class SealedMemoryResource final : public std::pmr::memory_resource {
public:
    void seal() noexcept { m_sealed = true; }
    [[nodiscard]] Core::usize allocations() const noexcept { return m_allocations; }
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (m_sealed) { throw std::bad_alloc{}; }
        ++m_allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
    Core::usize m_allocations = 0;
    bool m_sealed = false;
};

class FailAfterMemoryResource final : public std::pmr::memory_resource {
public:
    explicit FailAfterMemoryResource(Core::usize allocationLimit) noexcept : m_remaining(allocationLimit) {}
    [[nodiscard]] Core::usize liveBytes() const noexcept { return m_liveBytes; }
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (m_remaining == 0) { throw std::bad_alloc{}; }
        --m_remaining;
        void* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        m_liveBytes += bytes;
        return pointer;
    }
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        m_liveBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
    Core::usize m_remaining = 0;
    Core::usize m_liveBytes = 0;
};

[[nodiscard]] inline Core::Result<NavigationGrid2D> makeGrid(
    Core::u32 width, Core::u32 height, std::span<const NavigationCell2D> blocked = {},
    std::span<const Core::u8> costs = {}, Math::Vec2 origin = {}, float cellSize = 1.0F,
    std::pmr::memory_resource& memory = *std::pmr::get_default_resource())
{
    std::vector<Core::u8> flags(static_cast<Core::usize>(width) * height, 0);
    std::vector<Core::u8> uniformCosts(flags.size(), 1);
    for (const auto cell : blocked) { flags[static_cast<Core::usize>(cell.y) * width + cell.x] = NavigationGrid2DContract::CellBlocked; }
    auto data = NavigationGrid2DData::Create(
        {.widthCells = width, .heightCells = height, .originXMeters = origin.x, .originYMeters = origin.y,
         .cellSizeMeters = cellSize, .cellFlags = flags, .traversalCosts = costs.empty() ? std::span<const Core::u8>(uniformCosts) : costs}, memory);
    if (!data) { return Core::failure(std::move(data.error())); }
    return NavigationGrid2D::Create(std::move(*data), {.dynamicBlockerCapacity = 8}, memory);
}

} // namespace Tina::Navigation2D::TestSupport
