#pragma once

#include <tina/ui/UILayout.hpp>

#include <compare>

namespace Tina::UI {

// Places intrinsic control content inside the element's content box. This is
// independent from Flex alignment, which only arranges child elements.
struct UIContentAlignment final {
    UIAxisAlignment horizontal = UIAxisAlignment::Start;
    UIAxisAlignment vertical = UIAxisAlignment::Start;

    auto operator<=>(const UIContentAlignment&) const = default;
};

// Published together with layout so paint, caret/selection, pointer-to-text
// mapping, and external inspection use one placement result.
struct UICommittedContentPlacement final {
    UILogicalRect contentBox{};
    UILogicalPoint origin{};
    UILogicalSize intrinsicSize{};
    bool hasIntrinsicContent = false;

    auto operator<=>(const UICommittedContentPlacement&) const = default;
};

} // namespace Tina::UI
