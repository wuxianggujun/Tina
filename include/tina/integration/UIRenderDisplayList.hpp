#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/UIDisplayList.hpp>
#include <tina/ui/UICommittedPaint.hpp>

namespace Tina::Integration {

// Framebuffer destination for one committed logical UI snapshot. The mapping
// deliberately carries no windowing content-scale state: the actual logical
// and framebuffer extents are the authoritative scale pair.
struct UIRenderViewportMapping final {
    Render::UIPixelRect framebufferViewport{};
};

struct UIRenderDisplayListBuildStatistics final {
    usize sourcePaintEntryCount = 0;
    usize submittedSolidQuadCount = 0;
    usize redundantClipElisionCount = 0;
};

// The DisplayList view borrows the builder's fixed storage and follows its
// single-buffer invalidation contract. A subsequent beginFrame(), rollback(),
// move, or destruction of the builder invalidates it.
struct UIRenderDisplayListBuild final {
    Render::UIDisplayListView displayList{};
    UIRenderDisplayListBuildStatistics statistics{};
};

// Owns one complete builder transaction. If beginFrame() fails, an already-open
// caller transaction is left untouched. Once beginFrame() succeeds, every
// validation, conversion, or capacity failure rolls the new build back.
[[nodiscard]] Core::Result<UIRenderDisplayListBuild> buildUIDisplayList(
    Render::UIDisplayListBuilder& builder,
    UI::UICommittedPaintView paintView,
    UIRenderViewportMapping mapping);

} // namespace Tina::Integration
