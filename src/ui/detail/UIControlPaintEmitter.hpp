#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UICommittedPaint.hpp>

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::UI::Detail {

struct UIControlPaintPrimitive final {
    UILogicalRect worldRect{};
    UIPremultipliedRgba8Color color{};
};

class UIControlPaintBatch final {
  public:
    static constexpr usize Capacity = 4;

    [[nodiscard]] bool add(UILogicalRect worldRect, UIPremultipliedRgba8Color color) noexcept;

    [[nodiscard]] usize size() const noexcept;

    void appendTo(std::pmr::vector<UICommittedPaintEntry>& output, UINodeId node, UILogicalRect effectiveClip,
                  u32& nextPaintOrdinal) const noexcept;

  private:
    std::array<UIControlPaintPrimitive, Capacity> primitives_{};
    usize size_ = 0;
};

} // namespace Tina::UI::Detail
