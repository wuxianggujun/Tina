#include "UIThemeTransitionResolver.hpp"

namespace Tina::UI::Detail {

namespace {

void clearDetachedBindings(ProductChrome& target, u16 targetBindings) noexcept
{
    if ((targetBindings & ThemeBindingBoxPaint) == 0)
    {
        target.box = {};
    }
    if ((targetBindings & ThemeBindingTextStyle) == 0)
    {
        target.text = {};
    }
    if ((targetBindings & ThemeBindingButtonPaint) == 0)
    {
        target.button = {};
    }
    if ((targetBindings & ThemeBindingCheckboxPaint) == 0)
    {
        target.checkbox = {};
    }
    if ((targetBindings & ThemeBindingSliderPaint) == 0)
    {
        target.slider = {};
    }
    if ((targetBindings & ThemeBindingProgressBarPaint) == 0)
    {
        target.progressBar = {};
    }
    if ((targetBindings & ThemeBindingRadioButtonPaint) == 0)
    {
        target.radioButton = {};
    }
    if ((targetBindings & ThemeBindingScrollViewPaint) == 0)
    {
        target.scrollView = {};
    }
    if ((targetBindings & ThemeBindingDropdownPaint) == 0)
    {
        target.dropdown = {};
    }
    if ((targetBindings & ThemeBindingListViewPaint) == 0)
    {
        target.listView = {};
    }
    if ((targetBindings & ThemeBindingTreeViewPaint) == 0)
    {
        target.treeView = {};
    }
    if ((targetBindings & ThemeBindingTextEditPaint) == 0)
    {
        target.textEdit = {};
    }
    if ((targetBindings & ThemeBindingTabPaint) == 0)
    {
        target.tab = {};
    }
}

} // namespace

bool textMeasureInputsDiffer(const UITextStyle& left, const UITextStyle& right) noexcept
{
    return left.logicalSize != right.logicalSize || left.advanceScale != right.advanceScale ||
           left.lineHeightScale != right.lineHeightScale;
}

ProductChromeTransition resolveProductChromeTransition(ProductChromeStorage current, UIStyleRoleId role,
                                                       const UITheme& theme, u16 affectedBindings,
                                                       u16 targetBindings) noexcept
{
    ProductChromeTransition transition{
        .target = productChromeFor(role, theme),
    };
    clearDetachedBindings(transition.target, targetBindings);

    const auto classify = [&](u16 binding, const auto& currentValue, const auto& targetValue) noexcept {
        if ((affectedBindings & binding) != 0 && currentValue != targetValue)
        {
            transition.changedBindings |= binding;
        }
    };
    classify(ThemeBindingBoxPaint, current.box, transition.target.box);
    classify(ThemeBindingTextStyle, current.text, transition.target.text);
    classify(ThemeBindingButtonPaint, current.button, transition.target.button);
    classify(ThemeBindingCheckboxPaint, current.checkbox, transition.target.checkbox);
    classify(ThemeBindingSliderPaint, current.slider, transition.target.slider);
    classify(ThemeBindingProgressBarPaint, current.progressBar, transition.target.progressBar);
    classify(ThemeBindingRadioButtonPaint, current.radioButton, transition.target.radioButton);
    classify(ThemeBindingScrollViewPaint, current.scrollView, transition.target.scrollView);
    classify(ThemeBindingDropdownPaint, current.dropdown, transition.target.dropdown);
    classify(ThemeBindingListViewPaint, current.listView, transition.target.listView);
    classify(ThemeBindingTreeViewPaint, current.treeView, transition.target.treeView);
    classify(ThemeBindingTextEditPaint, current.textEdit, transition.target.textEdit);
    classify(ThemeBindingTabPaint, current.tab, transition.target.tab);

    const auto markLayoutChange = [&](u16 binding, bool changed) noexcept {
        if ((affectedBindings & binding) != 0 && changed)
        {
            transition.layoutAffectingBindings |= binding;
        }
    };
    markLayoutChange(ThemeBindingTextStyle, textMeasureInputsDiffer(current.text, transition.target.text));
    markLayoutChange(ThemeBindingRadioButtonPaint,
                     current.radioButton.labelGap != transition.target.radioButton.labelGap ||
                         current.radioButton.indicatorVisible != transition.target.radioButton.indicatorVisible);
    markLayoutChange(ThemeBindingScrollViewPaint,
                     current.scrollView.thickness != transition.target.scrollView.thickness ||
                         current.scrollView.minThumbExtent != transition.target.scrollView.minThumbExtent);
    markLayoutChange(ThemeBindingDropdownPaint,
                     current.dropdown.indicatorWidth != transition.target.dropdown.indicatorWidth ||
                         current.dropdown.indicatorHeight != transition.target.dropdown.indicatorHeight ||
                         current.dropdown.indicatorInset != transition.target.dropdown.indicatorInset);
    markLayoutChange(ThemeBindingListViewPaint,
                     current.listView.scrollBar.thickness != transition.target.listView.scrollBar.thickness ||
                         current.listView.scrollBar.minThumbExtent !=
                             transition.target.listView.scrollBar.minThumbExtent);
    markLayoutChange(ThemeBindingTreeViewPaint,
                     current.treeView.scrollBar.thickness != transition.target.treeView.scrollBar.thickness ||
                         current.treeView.scrollBar.minThumbExtent !=
                             transition.target.treeView.scrollBar.minThumbExtent);
    return transition;
}

void applyProductChromeTransition(ProductChromeStorage storage, const ProductChromeTransition& transition,
                                  u16 affectedBindings) noexcept
{
    if ((affectedBindings & ThemeBindingBoxPaint) != 0)
    {
        storage.box = transition.target.box;
    }
    if ((affectedBindings & ThemeBindingTextStyle) != 0)
    {
        storage.text = transition.target.text;
    }
    if ((affectedBindings & ThemeBindingButtonPaint) != 0)
    {
        storage.button = transition.target.button;
    }
    if ((affectedBindings & ThemeBindingCheckboxPaint) != 0)
    {
        storage.checkbox = transition.target.checkbox;
    }
    if ((affectedBindings & ThemeBindingSliderPaint) != 0)
    {
        storage.slider = transition.target.slider;
    }
    if ((affectedBindings & ThemeBindingProgressBarPaint) != 0)
    {
        storage.progressBar = transition.target.progressBar;
    }
    if ((affectedBindings & ThemeBindingRadioButtonPaint) != 0)
    {
        storage.radioButton = transition.target.radioButton;
    }
    if ((affectedBindings & ThemeBindingScrollViewPaint) != 0)
    {
        storage.scrollView = transition.target.scrollView;
    }
    if ((affectedBindings & ThemeBindingDropdownPaint) != 0)
    {
        storage.dropdown = transition.target.dropdown;
    }
    if ((affectedBindings & ThemeBindingListViewPaint) != 0)
    {
        storage.listView = transition.target.listView;
    }
    if ((affectedBindings & ThemeBindingTreeViewPaint) != 0)
    {
        storage.treeView = transition.target.treeView;
    }
    if ((affectedBindings & ThemeBindingTextEditPaint) != 0)
    {
        storage.textEdit = transition.target.textEdit;
    }
    if ((affectedBindings & ThemeBindingTabPaint) != 0)
    {
        storage.tab = transition.target.tab;
    }
}

} // namespace Tina::UI::Detail
