#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/render/RenderSurface.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/render/UIDisplayList.hpp>

#include <optional>
#include <span>

namespace Tina::Render {

// Submit-call-local borrow of one R8 glyph atlas page. Backend may upload or
// update a GPU texture synchronously; must not retain the span after submit.
struct UIGlyphAtlasPageView final {
    u32 width = 0;
    u32 height = 0;
    std::span<const u8> pixels{};
};

struct RenderFrame final {
    u64 frameIndex = 0;
    double interpolation = 0.0;
    std::optional<RenderSurfaceState> primaryWindowSurface{};
    // Submit-call-local borrow into Runtime-owned fixed storage. A render
    // backend must consume/copy/encode this view synchronously and must not
    // retain the view, any span, or an element pointer after submitFrame()
    // returns.
    UIDisplayListView primaryWindowUIDisplayList{};
    // Optional R8 atlas page for Glyph commands (atlasPage 0 in DisplayList).
    // Empty pixels means no GPU atlas update this frame.
    std::optional<UIGlyphAtlasPageView> primaryWindowUIGlyphAtlas{};
    // World RenderScene follows the same submit-call-local borrow contract as
    // the UI DisplayList. A backend must not retain it after submitFrame().
    RenderSceneView primaryWorldScene{};
};

} // namespace Tina::Render
