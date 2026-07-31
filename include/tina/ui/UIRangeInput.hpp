#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::UI {

// Capability-level value adjustment. Physical key/gamepad mapping belongs to
// Runtime input routing and is intentionally separate from spatial focus
// navigation.
enum class UIRangeInputCommand : u8 {
    Decrease,
    Increase,
};

struct UIRangeInputCommandResult final {
    bool consumed = false;
    bool changed = false;
    // True on Down when the committed keyboard focus has RangeInput
    // capability, including read-only or clamped targets. Runtime uses this to
    // avoid reinterpreting the same physical transition as spatial focus.
    bool targeted = false;
};

} // namespace Tina::UI
