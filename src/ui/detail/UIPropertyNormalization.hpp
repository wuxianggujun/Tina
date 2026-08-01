#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/ui/UIContextConfig.hpp>
#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIImage.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIPopup.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/UITreeView.hpp>

namespace Tina::UI::Detail {

struct NormalizedUIContextCapacityConfig final {
    usize nodeCapacity = 0;
    usize rootCapacity = 0;
    usize dirtyQueueCapacity = 0;
    usize layoutSnapshotCapacity = 0;
    usize hitSnapshotCapacity = 0;
    usize paintSnapshotCapacity = 0;
    usize canvasCommandCapacity = 0;
    usize imageContentCapacity = 0;
    usize routePathCapacity = 0;
    usize routedPointerListenerCapacity = 0;
    usize buttonActionCapacity = 0;
    usize textByteCapacity = 0;
    bool applyDefaultProductChrome = true;
};

[[nodiscard]] Core::Result<NormalizedUIContextCapacityConfig>
normalizeUIContextCapacityConfig(UIContextCapacityConfig config);

[[nodiscard]] Core::Result<UIImageContent>
normalizeImageContent(UIImageContent content);

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
[[nodiscard]] bool
isValidContentAlignment(UIContentAlignment alignment) noexcept;

} // namespace Tina::UI::Detail
