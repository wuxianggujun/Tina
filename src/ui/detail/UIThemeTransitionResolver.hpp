#pragma once

#include "UIStyleRoleResolver.hpp"

namespace Tina::UI::Detail {

struct ProductChromeStorage final {
    UIBoxPaint& box;
    UITextStyle& text;
    UIButtonPaint& button;
    UICheckboxPaint& checkbox;
    UISliderPaint& slider;
    UIProgressBarPaint& progressBar;
    UIRadioButtonPaint& radioButton;
    UIScrollViewPaint& scrollView;
    UIDropdownPaint& dropdown;
    UIListViewPaint& listView;
    UITreeViewPaint& treeView;
    UITextEditPaint& textEdit;
    UITabPaint& tab;
    UISplitterPaint& splitter;
    UIStraightSrgba8Color* imageTint = nullptr;
};

struct ProductChromeTransition final {
    ProductChrome target{};
    u16 changedBindings = 0;
    u16 layoutAffectingBindings = 0;
};

[[nodiscard]] bool textMeasureInputsDiffer(const UITextStyle& left, const UITextStyle& right) noexcept;

[[nodiscard]] ProductChromeTransition resolveProductChromeTransition(ProductChromeStorage current, UIStyleRoleId role,
                                                                     const UITheme& theme, u16 affectedBindings,
                                                                     u16 targetBindings) noexcept;

void applyProductChromeTransition(ProductChromeStorage storage, const ProductChromeTransition& transition,
                                  u16 affectedBindings) noexcept;

} // namespace Tina::UI::Detail
