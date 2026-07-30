#include "UIControlPaintEmitter.hpp"

namespace Tina::UI::Detail {

bool UIControlPaintBatch::add(UILogicalRect worldRect, UIPremultipliedRgba8Color color) noexcept
{
    if (worldRect.width <= 0.0F || worldRect.height <= 0.0F || color.isTransparent())
    {
        return true;
    }
    if (size_ >= primitives_.size())
    {
        return false;
    }

    primitives_[size_] = UIControlPaintPrimitive{
        .worldRect = worldRect,
        .color = color,
    };
    ++size_;
    return true;
}

usize UIControlPaintBatch::size() const noexcept
{
    return size_;
}

void UIControlPaintBatch::appendTo(std::pmr::vector<UICommittedPaintEntry>& output, UINodeId node,
                                   UILogicalRect effectiveClip, u32& nextPaintOrdinal) const noexcept
{
    for (usize index = 0; index < size_; ++index)
    {
        const UIControlPaintPrimitive& primitive = primitives_[index];
        output.push_back(UICommittedPaintEntry{
            .node = node,
            .worldRect = primitive.worldRect,
            .effectiveClip = effectiveClip,
            .paintOrdinal = nextPaintOrdinal++,
            .solidFill = primitive.color,
        });
    }
}

} // namespace Tina::UI::Detail
