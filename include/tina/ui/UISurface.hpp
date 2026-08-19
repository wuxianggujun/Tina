#pragma once

#include <tina/core/base/Types.hpp>

#include <compare>

namespace Tina::UI {

// Presentational container profile. Surface does not add behavior or retained
// state; it selects product-theme chrome for an ordinary Element.
enum class UISurfaceVariant : u8 {
    Plain = 0,
    Filled,
    Elevated,
};

struct UISurfaceConfig final {
    UISurfaceVariant variant = UISurfaceVariant::Filled;

    auto operator<=>(const UISurfaceConfig&) const = default;
};

} // namespace Tina::UI
