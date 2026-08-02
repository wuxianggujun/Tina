#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIBehavior.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UITextEdit.hpp>

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

struct UITextInputState final {
    UITextSelection selection{};
};

struct UIScrollBehaviorState final {
    UIScrollViewStyle style{};
    UIScrollOffset requestedOffset{};
    UIScrollViewMetrics committedMetrics{};
    UILogicalRect committedViewportRect{};
};

struct UISelectBehaviorState final {
    UINodeId selectedOption{};
};

struct UIBehaviorStateSlotCounts final {
    usize activate = 0;
    usize toggle = 0;
    usize range = 0;
    usize textInput = 0;
    usize scroll = 0;
    usize selection = 0;

    bool operator==(const UIBehaviorStateSlotCounts&) const = default;
};

struct UIBehaviorStateStorageCounters final {
    // Monotonic slot totals. Published includes ordinary and reservation-backed publish operations.
    UIBehaviorStateSlotCounts requested{};
    UIBehaviorStateSlotCounts reserved{};
    UIBehaviorStateSlotCounts published{};
    UIBehaviorStateSlotCounts capacityFailures{};
    // Current capacity held by successful reservations but not yet published or released.
    UIBehaviorStateSlotCounts outstandingReservations{};
};

class UIBehaviorStateStorage final {
  public:
    UIBehaviorStateStorage(usize nodeCapacity, usize activateCapacity, usize toggleCapacity, usize rangeInputCapacity,
                           usize textInputCapacity, usize scrollCapacity, usize selectCapacity,
                           std::pmr::memory_resource& resource);

    // On success, the caller keeps a mutable copy of requested as its remaining reservation.
    [[nodiscard]] Core::Status reserve(const UIBehaviorStateSlotCounts& requested);
    [[nodiscard]] Core::Status publish(u32 nodeIndex, UIElementBehavior behaviors);
    [[nodiscard]] Core::Status publishReserved(u32 nodeIndex, UIElementBehavior behaviors,
                                               UIBehaviorStateSlotCounts& remainingReservation);
    void releaseReservation(UIBehaviorStateSlotCounts& remainingReservation) noexcept;
    void release(u32 nodeIndex) noexcept;

    [[nodiscard]] bool hasActivate(u32 nodeIndex) const noexcept;
    [[nodiscard]] bool hasToggle(u32 nodeIndex) const noexcept;
    [[nodiscard]] u8* tryToggleValue(u32 nodeIndex) noexcept;
    [[nodiscard]] const u8* tryToggleValue(u32 nodeIndex) const noexcept;
    [[nodiscard]] UIRangeInputState* tryRangeInputState(u32 nodeIndex) noexcept;
    [[nodiscard]] const UIRangeInputState* tryRangeInputState(u32 nodeIndex) const noexcept;
    [[nodiscard]] UITextInputState* tryTextInputState(u32 nodeIndex) noexcept;
    [[nodiscard]] const UITextInputState* tryTextInputState(u32 nodeIndex) const noexcept;
    [[nodiscard]] UIScrollBehaviorState* tryScrollState(u32 nodeIndex) noexcept;
    [[nodiscard]] const UIScrollBehaviorState* tryScrollState(u32 nodeIndex) const noexcept;
    [[nodiscard]] UISelectBehaviorState* trySelectState(u32 nodeIndex) noexcept;
    [[nodiscard]] const UISelectBehaviorState* trySelectState(u32 nodeIndex) const noexcept;

    [[nodiscard]] usize activateCapacity() const noexcept;
    [[nodiscard]] usize activeActivateCount() const noexcept;
    [[nodiscard]] usize activateHighWater() const noexcept;
    [[nodiscard]] usize toggleCapacity() const noexcept;
    [[nodiscard]] usize activeToggleCount() const noexcept;
    [[nodiscard]] usize toggleHighWater() const noexcept;
    [[nodiscard]] usize rangeInputCapacity() const noexcept;
    [[nodiscard]] usize activeRangeInputCount() const noexcept;
    [[nodiscard]] usize rangeInputHighWater() const noexcept;
    [[nodiscard]] usize textInputCapacity() const noexcept;
    [[nodiscard]] usize activeTextInputCount() const noexcept;
    [[nodiscard]] usize textInputHighWater() const noexcept;
    [[nodiscard]] usize scrollCapacity() const noexcept;
    [[nodiscard]] usize activeScrollCount() const noexcept;
    [[nodiscard]] usize scrollHighWater() const noexcept;
    [[nodiscard]] usize selectCapacity() const noexcept;
    [[nodiscard]] usize activeSelectCount() const noexcept;
    [[nodiscard]] usize selectHighWater() const noexcept;
    [[nodiscard]] UIBehaviorStateStorageCounters counters() const noexcept;

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

    struct TextInputSlot final {
        u32 next = InvalidSlot;
        UITextInputState state{};
        bool active = false;
    };

    struct ScrollSlot final {
        u32 next = InvalidSlot;
        UIScrollBehaviorState state{};
        bool active = false;
    };

    struct SelectSlot final {
        u32 next = InvalidSlot;
        UISelectBehaviorState state{};
        bool active = false;
    };

    [[nodiscard]] Core::Status validatePublishTarget(u32 nodeIndex) const;
    void publishPreflighted(u32 nodeIndex, UIElementBehavior behaviors) noexcept;

    std::pmr::vector<u32> activateSlotByNodeIndex_;
    std::pmr::vector<u32> toggleSlotByNodeIndex_;
    std::pmr::vector<u32> rangeInputSlotByNodeIndex_;
    std::pmr::vector<u32> textInputSlotByNodeIndex_;
    std::pmr::vector<u32> scrollSlotByNodeIndex_;
    std::pmr::vector<u32> selectSlotByNodeIndex_;
    std::pmr::vector<ActivateSlot> activateSlots_;
    std::pmr::vector<ToggleSlot> toggleSlots_;
    std::pmr::vector<RangeInputSlot> rangeInputSlots_;
    std::pmr::vector<TextInputSlot> textInputSlots_;
    std::pmr::vector<ScrollSlot> scrollSlots_;
    std::pmr::vector<SelectSlot> selectSlots_;
    u32 activateFreeHead_ = InvalidSlot;
    u32 toggleFreeHead_ = InvalidSlot;
    u32 rangeInputFreeHead_ = InvalidSlot;
    u32 textInputFreeHead_ = InvalidSlot;
    u32 scrollFreeHead_ = InvalidSlot;
    u32 selectFreeHead_ = InvalidSlot;
    usize activeActivateCount_ = 0;
    usize activateHighWater_ = 0;
    usize activeToggleCount_ = 0;
    usize toggleHighWater_ = 0;
    usize activeRangeInputCount_ = 0;
    usize rangeInputHighWater_ = 0;
    usize activeTextInputCount_ = 0;
    usize textInputHighWater_ = 0;
    usize activeScrollCount_ = 0;
    usize scrollHighWater_ = 0;
    usize activeSelectCount_ = 0;
    usize selectHighWater_ = 0;
    UIBehaviorStateStorageCounters counters_{};
};

} // namespace Tina::UI::Detail
