#pragma once

#include <tina/asset/CookedAssetFile.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

namespace Tina::Asset {

// Uploads one prefiltered cooked EnvironmentMap as an atomic GPU IBL resource.
[[nodiscard]] Core::Result<Render::GpuEnvironmentMapId>
uploadEnvironmentMapFromCooked(Render::IRenderDevice& device,
                               const CookedAssetFile& environmentMapAsset);

} // namespace Tina::Asset
