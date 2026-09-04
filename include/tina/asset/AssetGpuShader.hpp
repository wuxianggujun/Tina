#pragma once

#include <tina/asset/CookedAssetFile.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

namespace Tina::Asset {

[[nodiscard]] Core::Result<Render::GpuShaderId>
uploadShaderFromCooked(Render::IRenderDevice& device, const CookedAssetFile& shaderAsset);

} // namespace Tina::Asset
