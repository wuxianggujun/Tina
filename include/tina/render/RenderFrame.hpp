#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/render/RenderSurface.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/render/UIDisplayList.hpp>

#include <optional>

namespace Tina::Render {

struct RenderFrame final {
    u64 frameIndex = 0;
    double interpolation = 0.0;
    std::optional<RenderSurfaceState> primaryWindowSurface{};
    // Submit-call-local borrow into Runtime-owned fixed storage. A render
    // backend must consume/copy/encode this view synchronously and must not
    // retain the view, any span, or an element pointer after submitFrame()
    // returns.
    UIDisplayListView primaryWindowUIDisplayList{};
    // World RenderScene follows the same submit-call-local borrow contract as
    // the UI DisplayList. A backend must not retain it after submitFrame().
    RenderSceneView primaryWorldScene{};
};

} // namespace Tina::Render
