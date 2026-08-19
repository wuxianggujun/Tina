#pragma once

#include <tina/core/base/Types.hpp>

#include <compare>

namespace Tina::UI {

enum class UIDividerOrientation : u8 {
    Horizontal = 0,
    Vertical,
};

enum class UIDividerTone : u8 {
    Subtle = 0,
    Strong,
    Accent,
};

// Divider is decorative and behavior-neutral. thickness is expressed in
// logical pixels and is applied only when the matching layout axis is Auto.
struct UIDividerConfig final {
    UIDividerOrientation orientation = UIDividerOrientation::Horizontal;
    UIDividerTone tone = UIDividerTone::Subtle;
    float thickness = 1.0F;

    auto operator<=>(const UIDividerConfig&) const = default;
};

} // namespace Tina::UI
