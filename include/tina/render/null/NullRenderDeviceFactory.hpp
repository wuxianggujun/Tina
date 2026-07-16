#pragma once

#include <tina/render/RenderDevice.hpp>

namespace Tina::Render {

[[nodiscard]] Core::Result<std::unique_ptr<IRenderDevice>> createNullRenderDevice(
    const RenderDeviceCreateParams& params);

} // namespace Tina::Render
