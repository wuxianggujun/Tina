#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>
#include <string_view>
#include <vector>

namespace Tina::UI::Detail {

class UITextStorage final {
public:
    struct Allocation final {
        u32 offset = 0;
        u32 capacity = 0;

        bool operator==(const Allocation&) const = default;
    };

    struct Reservation final {
        Allocation remaining{};

        [[nodiscard]] usize remainingBytes() const noexcept
        {
            return remaining.capacity;
        }

        bool operator==(const Reservation&) const = default;
    };

    UITextStorage(usize byteCapacity, usize freeAllocationCapacity, std::pmr::memory_resource& resource);

    [[nodiscard]] Core::Result<Allocation> allocate(u32 byteCount);
    [[nodiscard]] Core::Result<Reservation> reserve(u32 byteCount);
    [[nodiscard]] Core::Result<Allocation> allocateReserved(Reservation& reservation, u32 byteCount);
    void releaseReservation(Reservation& reservation) noexcept;
    void release(Allocation allocation) noexcept;

    void write(Allocation allocation, std::string_view text) noexcept;
    [[nodiscard]] std::string_view view(Allocation allocation, u32 length) const noexcept;

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize used() const noexcept;
    [[nodiscard]] usize highWater() const noexcept;
    [[nodiscard]] usize outstandingReservedBytes() const noexcept;
    [[nodiscard]] usize reservationRequestedBytes() const noexcept;
    [[nodiscard]] usize reservationReservedBytes() const noexcept;
    [[nodiscard]] usize reservationPublishedBytes() const noexcept;
    [[nodiscard]] usize reservationCapacityFailureCount() const noexcept;

private:
    [[nodiscard]] Core::Result<Allocation> allocateBlock(u32 byteCount);

    std::pmr::vector<char> bytes_;
    std::pmr::vector<Allocation> freeAllocations_;
    usize used_ = 0;
    usize highWater_ = 0;
    usize bumpOffset_ = 0;
    usize outstandingReservedBytes_ = 0;
    usize reservationRequestedBytes_ = 0;
    usize reservationReservedBytes_ = 0;
    usize reservationPublishedBytes_ = 0;
    usize reservationCapacityFailureCount_ = 0;
};

} // namespace Tina::UI::Detail
