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

    UITextStorage(usize byteCapacity, usize freeAllocationCapacity, std::pmr::memory_resource& resource);

    [[nodiscard]] Core::Result<Allocation> allocate(u32 byteCount);
    void release(Allocation allocation) noexcept;

    void write(Allocation allocation, std::string_view text) noexcept;
    [[nodiscard]] std::string_view view(Allocation allocation, u32 length) const noexcept;

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize used() const noexcept;
    [[nodiscard]] usize highWater() const noexcept;

private:
    std::pmr::vector<char> bytes_;
    std::pmr::vector<Allocation> freeAllocations_;
    usize used_ = 0;
    usize highWater_ = 0;
    usize bumpOffset_ = 0;
};

} // namespace Tina::UI::Detail
