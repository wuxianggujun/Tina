#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIBehavior.hpp>

#include <limits>
#include <memory_resource>
#include <vector>

namespace Tina::UI::Detail {

struct UIRangeInputState final {
    float minValue = 0.0F;
    float maxValue = 1.0F;
    float step = 0.0F;
    float value = 0.0F;
};

class UIBehaviorStateStorage final {
  public:
    UIBehaviorStateStorage(usize nodeCapacity, usize activateCapacity, usize toggleCapacity, usize rangeInputCapacity,
                           std::pmr::memory_resource& resource);

    [[nodiscard]] Core::Status publish(u32 nodeIndex, UIElementBehavior behaviors);
    void release(u32 nodeIndex) noexcept;

    [[nodiscard]] bool hasActivate(u32 nodeIndex) const noexcept;
    [[nodiscard]] bool hasToggle(u32 nodeIndex) const noexcept;
    [[nodiscard]] u8* tryToggleValue(u32 nodeIndex) noexcept;
    [[nodiscard]] const u8* tryToggleValue(u32 nodeIndex) const noexcept;
    [[nodiscard]] UIRangeInputState* tryRangeInputState(u32 nodeIndex) noexcept;
    [[nodiscard]] const UIRangeInputState* tryRangeInputState(u32 nodeIndex) const noexcept;

    [[nodiscard]] usize activateCapacity() const noexcept;
    [[nodiscard]] usize activeActivateCount() const noexcept;
    [[nodiscard]] usize activateHighWater() const noexcept;
    [[nodiscard]] usize toggleCapacity() const noexcept;
    [[nodiscard]] usize activeToggleCount() const noexcept;
    [[nodiscard]] usize toggleHighWater() const noexcept;
    [[nodiscard]] usize rangeInputCapacity() const noexcept;
    [[nodiscard]] usize activeRangeInputCount() const noexcept;
    [[nodiscard]] usize rangeInputHighWater() const noexcept;

  private:
    static constexpr u32 InvalidSlot = (std::numeric_limits<u32>::max)();

    struct ActivateSlot final {
        u32 next = InvalidSlot;
        bool active = false;
    };

    struct ToggleSlot final {
        u32 next = InvalidSlot;
        u8 value = 0;
        bool active = false;
    };

    struct RangeInputSlot final {
        u32 next = InvalidSlot;
        UIRangeInputState state{};
        bool active = false;
    };

    std::pmr::vector<u32> activateSlotByNodeIndex_;
    std::pmr::vector<u32> toggleSlotByNodeIndex_;
    std::pmr::vector<u32> rangeInputSlotByNodeIndex_;
    std::pmr::vector<ActivateSlot> activateSlots_;
    std::pmr::vector<ToggleSlot> toggleSlots_;
    std::pmr::vector<RangeInputSlot> rangeInputSlots_;
    u32 activateFreeHead_ = InvalidSlot;
    u32 toggleFreeHead_ = InvalidSlot;
    u32 rangeInputFreeHead_ = InvalidSlot;
    usize activeActivateCount_ = 0;
    usize activateHighWater_ = 0;
    usize activeToggleCount_ = 0;
    usize toggleHighWater_ = 0;
    usize activeRangeInputCount_ = 0;
    usize rangeInputHighWater_ = 0;
};

} // namespace Tina::UI::Detail
