#pragma once

#include <tina/ui/UIContent.hpp>
#include <tina/ui/UIImageSource.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

enum class UIImageFit : u8 {
    Fill = 0,
    Contain,
    Cover,
    None,
};

struct UIImageContent final {
    UIImageSource source{};
    UIImageFit fit = UIImageFit::Contain;
    UIContentAlignment alignment{};
    UIStraightSrgba8Color tint = rgba8(255, 255, 255);
    UIImageSampling sampling = UIImageSampling::Linear;

    auto operator<=>(const UIImageContent&) const = default;
};

} // namespace Tina::UI
