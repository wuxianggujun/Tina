#pragma once

#include <tina/asset/CookedAssetFile.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderDevice.hpp>

namespace Tina::Asset {

// Uploads a loaded Texture2D cooked asset (typed Rgba8Unorm payload) to the render device.
// Returns a backend GpuTextureId owned by the device until destroyTexture2D.
[[nodiscard]] Core::Result<Render::GpuTextureId> uploadTexture2DFromCooked(Render::IRenderDevice& device,
                                                                           const CookedAssetFile& textureAsset);

// Convenience: upload + direct bind for Sprite2D batches using a caller-selected
// spriteKey. It shares the device namespace with allocator-managed bindings and
// must not be used while a Sprite2DBindingRegistry manages that device.
[[nodiscard]] Core::Status uploadAndBindTexture2DForSpriteKey(Render::IRenderDevice& device,
                                                              const CookedAssetFile& textureAsset, Core::u32 spriteKey);

} // namespace Tina::Asset
