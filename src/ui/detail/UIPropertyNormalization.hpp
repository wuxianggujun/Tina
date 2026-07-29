#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIPopup.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/UITreeView.hpp>

namespace Tina::UI::Detail {

[[nodiscard]] UIBoxPaint normalizeBoxPaint(UIBoxPaint paint) noexcept;

[[nodiscard]] Core::Result<UIScrollViewStyle>
normalizeScrollViewStyle(UIScrollViewStyle style);
[[nodiscard]] Core::Result<UIScrollViewPaint>
normalizeScrollViewPaint(UIScrollViewPaint paint);
[[nodiscard]] Core::Result<UIScrollOffset>
normalizeScrollOffset(UIScrollOffset offset);

[[nodiscard]] Core::Result<UIListViewCreateConfig>
normalizeListViewCreateConfig(UIListViewCreateConfig config);
[[nodiscard]] Core::Result<UIListViewStyle>
normalizeListViewStyle(UIListViewStyle style);
[[nodiscard]] Core::Result<UIListViewPaint>
normalizeListViewPaint(UIListViewPaint paint);
[[nodiscard]] bool
isValidListViewScrollAlignment(UIListViewScrollAlignment alignment) noexcept;

[[nodiscard]] Core::Result<UITreeViewCreateConfig>
normalizeTreeViewCreateConfig(UITreeViewCreateConfig config);
[[nodiscard]] Core::Result<UITreeViewStyle>
normalizeTreeViewStyle(UITreeViewStyle style);
[[nodiscard]] Core::Result<UITreeViewPaint>
normalizeTreeViewPaint(UITreeViewPaint paint);
[[nodiscard]] bool
isValidTreeViewScrollAlignment(UITreeViewScrollAlignment alignment) noexcept;

[[nodiscard]] Core::Result<UIPopupStyle>
normalizePopupStyle(UIPopupStyle style);
[[nodiscard]] Core::Result<UIDropdownPaint>
normalizeDropdownPaint(UIDropdownPaint paint);

[[nodiscard]] Core::Status validateProductTheme(const UITheme& theme);
[[nodiscard]] Core::Result<UILayoutStyle>
normalizeLayoutStyle(UILayoutStyle style);

} // namespace Tina::UI::Detail
