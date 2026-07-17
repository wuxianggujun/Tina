#pragma once

#include <tina/integration/WindowSurface.hpp>
#include <tina/render/RenderDevice.hpp>

#include <memory>

namespace Tina::Render::Bgfx {

// Private composition-root factory. The header stays under src/render/bgfx so
// neither the Game SDK nor the public Render module exposes the concrete backend.
[[nodiscard]] Core::Result<std::unique_ptr<IRenderDevice>> createBgfxRenderDevice(
    const RenderDeviceCreateParams& params,
    Integration::NativeWindowSurfaceLease lease);

} // namespace Tina::Render::Bgfx
