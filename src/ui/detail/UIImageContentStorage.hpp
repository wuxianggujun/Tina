#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIImage.hpp>

#include <limits>
#include <memory_resource>
#include <vector>

namespace Tina::UI::Detail {

class UIImageContentStorage final {
  public:
    UIImageContentStorage(usize nodeCapacity, usize imageCapacity,
                          std::pmr::memory_resource& resource);

    [[nodiscard]] Core::Status assign(u32 nodeIndex, const UIImageContent& content);
    // Paint-only mutation: tint/opacity does not change intrinsic layout size.
    [[nodiscard]] Core::Status setTint(u32 nodeIndex, UIStraightSrgba8Color tint);
    void release(u32 nodeIndex) noexcept;

    [[nodiscard]] UIImageContent* getMutable(u32 nodeIndex) noexcept;
    [[nodiscard]] const UIImageContent* get(u32 nodeIndex) const noexcept;
    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize activeCount() const noexcept;
    [[nodiscard]] usize highWater() const noexcept;

  private:
    static constexpr u32 InvalidSlot = (std::numeric_limits<u32>::max)();

    struct Slot final {
        UIImageContent content{};
        u32 next = InvalidSlot;
        bool active = false;
    };

    std::pmr::vector<u32> slotByNodeIndex_;
    std::pmr::vector<Slot> slots_;
    u32 freeHead_ = InvalidSlot;
    usize activeCount_ = 0;
    usize highWater_ = 0;
};

} // namespace Tina::UI::Detail
