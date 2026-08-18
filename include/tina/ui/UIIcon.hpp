#pragma once

#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIImageSource.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

// Strongly typed authoring profile for decorative icons. UIIcon deliberately
// reuses the Image content store and render path; it does not introduce a
// separate asset, atlas, widget state machine, or GPU pipeline.
struct UIIconContent final {
    UIImageSource source{};
    UIStraightSrgba8Color tint = rgba8(255, 255, 255);
    UIImageSampling sampling = UIImageSampling::Linear;
    UIContentAlignment alignment{
        .horizontal = UIAxisAlignment::Center,
        .vertical = UIAxisAlignment::Center,
    };

    auto operator<=>(const UIIconContent&) const = default;
};

} // namespace Tina::UI
