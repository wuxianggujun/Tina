#include "FrameArena.hpp"

#include <algorithm>
#include <bit>
#include <exception>
#include <limits>
#include <new>
#include <string_view>
#include <utility>

namespace Tina::Core {

Result<FrameArena> FrameArena::Create(
    FrameArenaConfig config,
    std::pmr::memory_resource& upstream)
{
    if (config.capacityBytes == 0) {
        return failure(
            CoreErrorCode::InvalidArgument,
            "FrameArena capacityBytes must be greater than zero");
    }
    if (!std::has_single_bit(config.maximumAlignment)) {
        return failure(
            CoreErrorCode::InvalidArgument,
            "FrameArena maximumAlignment must be a non-zero power of two");
    }

    try {
        void* storage = upstream.allocate(config.capacityBytes, config.maximumAlignment);
        return FrameArena(storage, config, upstream);
    } catch (const std::bad_alloc&) {
        return failure(
            CoreErrorCode::OutOfMemory,
            "FrameArena backing allocation failed");
    } catch (const std::exception& exception) {
        return failure(
            CoreErrorCode::Internal,
            std::string_view(exception.what()));
    } catch (...) {
        return failure(
            CoreErrorCode::Internal,
            "FrameArena backing allocation failed with an unknown exception");
    }
}

FrameArena::FrameArena(
    void* storage,
    FrameArenaConfig config,
    std::pmr::memory_resource& upstream) noexcept
    : m_upstream(&upstream),
      m_storage(static_cast<std::byte*>(storage)),
      m_capacityBytes(config.capacityBytes),
      m_maximumAlignment(config.maximumAlignment)
{
}

FrameArena::~FrameArena()
{
    releaseBacking();
}

FrameArena::FrameArena(FrameArena&& other) noexcept
    : m_upstream(std::exchange(other.m_upstream, nullptr)),
      m_storage(std::exchange(other.m_storage, nullptr)),
      m_capacityBytes(std::exchange(other.m_capacityBytes, 0)),
      m_maximumAlignment(std::exchange(other.m_maximumAlignment, 0)),
      m_usedBytes(std::exchange(other.m_usedBytes, 0)),
      m_peakBytes(std::exchange(other.m_peakBytes, 0)),
      m_allocationCount(std::exchange(other.m_allocationCount, 0)),
      m_failedAllocationCount(std::exchange(other.m_failedAllocationCount, 0)),
      m_epoch(std::exchange(other.m_epoch, 1))
{
}

FrameArena& FrameArena::operator=(FrameArena&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    releaseBacking();
    m_upstream = std::exchange(other.m_upstream, nullptr);
    m_storage = std::exchange(other.m_storage, nullptr);
    m_capacityBytes = std::exchange(other.m_capacityBytes, 0);
    m_maximumAlignment = std::exchange(other.m_maximumAlignment, 0);
    m_usedBytes = std::exchange(other.m_usedBytes, 0);
    m_peakBytes = std::exchange(other.m_peakBytes, 0);
    m_allocationCount = std::exchange(other.m_allocationCount, 0);
    m_failedAllocationCount = std::exchange(other.m_failedAllocationCount, 0);
    m_epoch = std::exchange(other.m_epoch, 1);
    return *this;
}

void* FrameArena::tryAllocate(usize bytes, usize alignment) noexcept
{
    if (m_storage == nullptr || !std::has_single_bit(alignment)
        || alignment > m_maximumAlignment) {
        recordFailure();
        return nullptr;
    }

    const usize effectiveBytes = std::max<usize>(bytes, 1);
    const uintptr currentAddress = reinterpret_cast<uintptr>(m_storage + m_usedBytes);
    const usize misalignment = currentAddress % alignment;
    const usize padding = misalignment == 0 ? 0 : alignment - misalignment;
    const usize remaining = m_capacityBytes - m_usedBytes;
    if (padding > remaining || effectiveBytes > remaining - padding) {
        recordFailure();
        return nullptr;
    }

    void* allocation = m_storage + m_usedBytes + padding;
    m_usedBytes += padding + effectiveBytes;
    m_peakBytes = std::max(m_peakBytes, m_usedBytes);
    if (m_allocationCount != (std::numeric_limits<u64>::max)()) {
        ++m_allocationCount;
    }
    return allocation;
}

void FrameArena::reset() noexcept
{
    m_usedBytes = 0;
    if (m_epoch != (std::numeric_limits<u64>::max)()) {
        ++m_epoch;
    }
}

FrameArenaStatistics FrameArena::statistics() const noexcept
{
    return FrameArenaStatistics{
        .usedBytes = m_usedBytes,
        .peakBytes = m_peakBytes,
        .capacityBytes = m_capacityBytes,
        .allocationCount = m_allocationCount,
        .failedAllocationCount = m_failedAllocationCount,
        .epoch = m_epoch,
    };
}

bool FrameArena::owns(const void* pointer) const noexcept
{
    if (pointer == nullptr || m_storage == nullptr) {
        return false;
    }
    const uintptr address = reinterpret_cast<uintptr>(pointer);
    const uintptr begin = reinterpret_cast<uintptr>(m_storage);
    return address >= begin && address - begin < m_capacityBytes;
}

void* FrameArena::do_allocate(usize bytes, usize alignment)
{
    if (void* allocation = tryAllocate(bytes, alignment)) {
        return allocation;
    }
    throw std::bad_alloc{};
}

void FrameArena::do_deallocate(void*, usize, usize)
{
}

bool FrameArena::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

void FrameArena::releaseBacking() noexcept
{
    if (m_storage != nullptr) {
        m_upstream->deallocate(m_storage, m_capacityBytes, m_maximumAlignment);
    }
    m_upstream = nullptr;
    m_storage = nullptr;
    m_capacityBytes = 0;
    m_maximumAlignment = 0;
    m_usedBytes = 0;
}

void FrameArena::recordFailure() noexcept
{
    if (m_failedAllocationCount != (std::numeric_limits<u64>::max)()) {
        ++m_failedAllocationCount;
    }
}

} // namespace Tina::Core
