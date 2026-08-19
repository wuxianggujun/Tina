#pragma once

#include <tina/core/base/Types.hpp>

#include <compare>

namespace Tina::UI {

enum class UIBadgeTone : u8 {
    Neutral = 0,
    Accent,
    Danger,
};

// Badge is a compact, read-only label profile. It reuses ordinary Element text
// and box paint rather than introducing badge-specific retained storage.
struct UIBadgeConfig final {
    UIBadgeTone tone = UIBadgeTone::Neutral;

    auto operator<=>(const UIBadgeConfig&) const = default;
};

} // namespace Tina::UI
