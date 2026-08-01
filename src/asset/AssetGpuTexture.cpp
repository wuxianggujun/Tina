#include <tina/asset/AssetGpuTexture.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>

namespace Tina::Asset {

Core::Result<Render::GpuTextureId> uploadTexture2DFromCooked(Render::IRenderDevice& device,
                                                             const CookedAssetFile& textureAsset)
{
    auto view = parseTexture2DFromCooked(textureAsset);
    if (!view)
    {
        return Core::failure(std::move(view.error()).withContext("uploadTexture2DFromCooked", "parse"));
    }
    return device.createTexture2DRgba8(Render::Texture2DUploadDesc{
        .width = view->width,
        .height = view->height,
        .rgba8Pixels = view->pixels,
    });
}

Core::Status uploadAndBindTexture2DForSpriteKey(Render::IRenderDevice& device, const CookedAssetFile& textureAsset,
                                                Core::u32 spriteKey)
{
    if (spriteKey == 0)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "spriteKey must be non-zero for texture binding");
    }
    auto texture = uploadTexture2DFromCooked(device, textureAsset);
    if (!texture)
    {
        return Core::failure(std::move(texture.error()));
    }
    return device.setTexture2DBinding(spriteKey, *texture);
}

} // namespace Tina::Asset
