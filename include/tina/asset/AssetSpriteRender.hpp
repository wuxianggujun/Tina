#pragma once

#include <tina/asset/CookedAssetFile.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderScene.hpp>

namespace Tina::Asset {

struct SpriteRenderParams final {
    Core::u64 stableEntityKey = 1;
    float centerX = 0.0F;
    float centerY = 0.0F;
    float rotationRadians = 0.0F;
    // When <= 0, derived from sprite UV size * texture pixels / pixelsPerUnit when texture is provided.
    float widthMeters = 0.0F;
    float heightMeters = 0.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    Core::i16 sortingLayer = 0;
    Core::i32 orderInLayer = 0;
    Core::u8 red = 255;
    Core::u8 green = 255;
    Core::u8 blue = 255;
    Core::u8 alpha = 255;
    bool flipX = false;
    bool flipY = false;
    bool visible = true;
};

// Builds a RenderSprite2DInput from a loaded Sprite cooked asset (+ optional Texture2D for size).
// UV comes from Sprite payload. Size uses params when >0, else texture size / ppu * UV span.
[[nodiscard]] Core::Result<Render::RenderSprite2DInput>
makeSpriteRenderInput(const CookedAssetFile& spriteAsset, const CookedAssetFile* textureAsset,
                      Render::FrameResourceRef texture, const SpriteRenderParams& params = {});

} // namespace Tina::Asset
