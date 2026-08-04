#include <tina/asset/AssetGpuEnvironmentMap.hpp>

#include <tina/asset/AssetTypedViews.hpp>

namespace Tina::Asset {

Core::Result<Render::GpuEnvironmentMapId>
uploadEnvironmentMapFromCooked(Render::IRenderDevice& device,
                               const CookedAssetFile& environmentMapAsset)
{
    auto view = parseEnvironmentMapFromCooked(environmentMapAsset);
    if (!view)
    {
        return Core::failure(
            std::move(view.error()).withContext("uploadEnvironmentMapFromCooked", "parse"));
    }
    return device.createEnvironmentMap(Render::EnvironmentMapUploadDesc{
        .diffuseFaceSize = view->diffuseFaceSize,
        .specularFaceSize = view->specularFaceSize,
        .specularMipCount = view->specularMipCount,
        .brdfWidth = view->brdfWidth,
        .brdfHeight = view->brdfHeight,
        .diffuseRgba16FloatPixels = view->diffusePixels,
        .specularRgba16FloatPixels = view->specularPixels,
        .brdfRg16FloatPixels = view->brdfPixels,
    });
}

} // namespace Tina::Asset
