#pragma once

#include <tina/core/base/Types.hpp>

#include <tina/navigation3d/NavigationVolume3D.hpp>

#include <memory_resource>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace Tina::Navigation3D::TestSupport {

class SealedMemoryResource final : public std::pmr::memory_resource {
public:
    void seal() noexcept { m_sealed = true; }
    [[nodiscard]] Core::usize allocations() const noexcept { return m_allocations; }
private:
    void* do_allocate(Tina::Core::usize bytes, Tina::Core::usize alignment) override
    {
        if (m_sealed) { throw std::bad_alloc{}; }
        ++m_allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* pointer, Tina::Core::usize bytes, Tina::Core::usize alignment) override
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
    explicit FailAfterMemoryResource(Core::usize allocationLimit) noexcept
        : m_remaining(allocationLimit) {}
    [[nodiscard]] Core::usize liveBytes() const noexcept { return m_liveBytes; }
private:
    void* do_allocate(Tina::Core::usize bytes, Tina::Core::usize alignment) override
    {
        if (m_remaining == 0) { throw std::bad_alloc{}; }
        --m_remaining;
        void* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        m_liveBytes += bytes;
        return pointer;
    }
    void do_deallocate(void* pointer, Tina::Core::usize bytes, Tina::Core::usize alignment) override
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

// Mutable authoring buffer for a test volume. Deliberately mirrors the production index
// layout rather than wrapping it: a helper that computed the index differently would make
// every test agree with itself and disagree with the module.
class VolumeBuilder final {
public:
    VolumeBuilder(Core::u32 width, Core::u32 height, Core::u32 depth)
        : m_width(width), m_height(height), m_depth(depth),
          m_flags(static_cast<Core::usize>(width) * height * depth, 0),
          m_costs(static_cast<Core::usize>(width) * height * depth, 1)
    {
    }

    VolumeBuilder& solid(Core::u32 x, Core::u32 y, Core::u32 z)
    {
        m_flags[index(x, y, z)] = NavigationVolume3DContract::CellSolid;
        return *this;
    }

    // Fills an entire y layer, which is how a test states "there is a floor here".
    VolumeBuilder& solidLayer(Core::u32 y)
    {
        for (Core::u32 z = 0; z < m_depth; ++z)
        {
            for (Core::u32 x = 0; x < m_width; ++x)
            {
                m_flags[index(x, y, z)] = NavigationVolume3DContract::CellSolid;
            }
        }
        return *this;
    }

    VolumeBuilder& solidColumn(Core::u32 x, Core::u32 z, Core::u32 fromY, Core::u32 toY)
    {
        for (Core::u32 y = fromY; y <= toY; ++y)
        {
            m_flags[index(x, y, z)] = NavigationVolume3DContract::CellSolid;
        }
        return *this;
    }

    VolumeBuilder& cost(Core::u32 x, Core::u32 y, Core::u32 z, Core::u8 value)
    {
        m_costs[index(x, y, z)] = value;
        return *this;
    }

    [[nodiscard]] Core::Result<NavigationVolume3D> build(
        Math::Vec3 origin = {}, float cellSize = 1.0F, Core::usize blockerCapacity = 8,
        std::pmr::memory_resource& memory = *std::pmr::get_default_resource()) const
    {
        auto data = NavigationVolume3DData::Create(
            {.widthCells = m_width, .heightCells = m_height, .depthCells = m_depth,
             .originXMeters = origin.x, .originYMeters = origin.y, .originZMeters = origin.z,
             .cellSizeMeters = cellSize, .cellFlags = m_flags, .traversalCosts = m_costs},
            memory);
        if (!data) { return Core::failure(std::move(data.error())); }
        return NavigationVolume3D::Create(
            std::move(*data), {.dynamicBlockerCapacity = blockerCapacity}, memory);
    }

    [[nodiscard]] std::span<const Core::u8> flags() const noexcept { return m_flags; }
    [[nodiscard]] std::span<const Core::u8> costs() const noexcept { return m_costs; }

private:
    [[nodiscard]] Core::usize index(Core::u32 x, Core::u32 y, Core::u32 z) const noexcept
    {
        return static_cast<Core::usize>(x) + static_cast<Core::usize>(z) * m_width +
               static_cast<Core::usize>(y) * m_width * m_depth;
    }

    Core::u32 m_width;
    Core::u32 m_height;
    Core::u32 m_depth;
    std::vector<Core::u8> m_flags;
    std::vector<Core::u8> m_costs;
};

// A floor at y = 0 with open space above, the shape almost every movement test starts from.
[[nodiscard]] inline Core::Result<NavigationVolume3D> makeFlatVolume(
    Core::u32 width, Core::u32 height, Core::u32 depth,
    std::pmr::memory_resource& memory = *std::pmr::get_default_resource())
{
    return VolumeBuilder(width, height, depth).solidLayer(0).build({}, 1.0F, 8, memory);
}

inline constexpr NavigationAgentProfile3D WalkerProfile{
    .heightCells = 2,
    .maxStepUpCells = 1,
    .maxFallCells = 3,
};

} // namespace Tina::Navigation3D::TestSupport
