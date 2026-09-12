#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <limits>
#include <memory_resource>
#include <type_traits>

namespace Tina::Core {

struct FrameArenaConfig final {
    usize capacityBytes = 0;
    usize maximumAlignment = 64;
};

struct FrameArenaStatistics final {
    usize usedBytes = 0;
    usize peakBytes = 0;
    usize capacityBytes = 0;
    u64 allocationCount = 0;
    u64 failedAllocationCount = 0;
    u64 epoch = 1;
};

// Fixed-capacity, single-owner linear memory. It allocates its backing block once
// during Create(), never falls back to another heap, and logically releases all arena
// allocations at reset(). Non-trivial objects must be destroyed before that reset.
class FrameArena final : public std::pmr::memory_resource {
public:
    [[nodiscard]] static Result<FrameArena> Create(
        FrameArenaConfig config,
        std::pmr::memory_resource& upstream = *std::pmr::get_default_resource());

    ~FrameArena() override;

    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;
    FrameArena(FrameArena&& other) noexcept;
    FrameArena& operator=(FrameArena&& other) noexcept;

    [[nodiscard]] void* tryAllocate(usize bytes, usize alignment) noexcept;

    template <typename Value>
        requires std::is_trivially_destructible_v<Value>
    [[nodiscard]] Value* tryAllocateUninitializedArray(usize count = 1) noexcept
    {
        if (count == 0) {
            return nullptr;
        }
        if (count > (std::numeric_limits<usize>::max)() / sizeof(Value)) {
            recordFailure();
            return nullptr;
        }
        return static_cast<Value*>(tryAllocate(sizeof(Value) * count, alignof(Value)));
    }

    void reset() noexcept;

    [[nodiscard]] FrameArenaStatistics statistics() const noexcept;
    [[nodiscard]] usize usedBytes() const noexcept { return m_usedBytes; }
    [[nodiscard]] usize remainingBytes() const noexcept { return m_capacityBytes - m_usedBytes; }
    [[nodiscard]] usize capacityBytes() const noexcept { return m_capacityBytes; }
    [[nodiscard]] usize maximumAlignment() const noexcept { return m_maximumAlignment; }
    [[nodiscard]] u64 epoch() const noexcept { return m_epoch; }
    [[nodiscard]] bool owns(const void* pointer) const noexcept;

private:
    FrameArena(
        void* storage,
        FrameArenaConfig config,
        std::pmr::memory_resource& upstream) noexcept;

    void* do_allocate(usize bytes, usize alignment) override;
    void do_deallocate(void* pointer, usize bytes, usize alignment) override;
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    void releaseBacking() noexcept;
    void recordFailure() noexcept;

    std::pmr::memory_resource* m_upstream = nullptr;
    std::byte* m_storage = nullptr;
    usize m_capacityBytes = 0;
    usize m_maximumAlignment = 0;
    usize m_usedBytes = 0;
    usize m_peakBytes = 0;
    u64 m_allocationCount = 0;
    u64 m_failedAllocationCount = 0;
    u64 m_epoch = 1;
};

} // namespace Tina::Core
