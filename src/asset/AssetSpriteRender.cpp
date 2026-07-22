#include <tina/asset/AssetSpriteRender.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>

#include <cmath>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::u32 spriteKeyFromAssetId(Core::AssetId assetId) noexcept
{
    const auto& bytes = assetId.bytes();
    Core::u32 key = 0;
    for (std::size_t index = 0; index < 4; ++index)
    {
        key = (key << 8U) | std::to_integer<Core::u32>(bytes[index]);
    }
    return key == 0 ? 1U : key;
}

} // namespace

Core::Result<Render::RenderSprite2DInput> makeSpriteRenderInput(const CookedAssetFile& spriteAsset,
                                                                const CookedAssetFile* textureAsset,
                                                                const SpriteRenderParams& params)
{
    auto sprite = parseSpriteFromCooked(spriteAsset);
    if (!sprite)
    {
        return Core::failure(std::move(sprite.error()));
    }

    float widthMeters = params.widthMeters;
    float heightMeters = params.heightMeters;
    if (widthMeters <= 0.0F || heightMeters <= 0.0F)
    {
        float texW = 1.0F;
        float texH = 1.0F;
        if (textureAsset != nullptr)
        {
            auto texture = parseTexture2DFromCooked(*textureAsset);
            if (!texture)
            {
                return Core::failure(std::move(texture.error()));
            }
            texW = static_cast<float>(texture->width);
            texH = static_cast<float>(texture->height);
        }
        const float uvW = sprite->u1 - sprite->u0;
        const float uvH = sprite->v1 - sprite->v0;
        const float ppu = sprite->pixelsPerUnit > 0.0F ? sprite->pixelsPerUnit : 100.0F;
        if (widthMeters <= 0.0F)
        {
            widthMeters = (texW * uvW) / ppu;
        }
        if (heightMeters <= 0.0F)
        {
            heightMeters = (texH * uvH) / ppu;
        }
    }
    if (!(widthMeters > 0.0F) || !(heightMeters > 0.0F) || !std::isfinite(widthMeters) || !std::isfinite(heightMeters))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "sprite render size is invalid");
    }

    // Apply pivot as geometric offset so render center is pivot-aware world position.
    // params.center is treated as the authored pivot position in world space.
    const float pivotOffsetX = (0.5F - sprite->pivotX) * widthMeters * params.scaleX;
    const float pivotOffsetY = (0.5F - sprite->pivotY) * heightMeters * params.scaleY;
    const float cosine = std::cos(params.rotationRadians);
    const float sine = std::sin(params.rotationRadians);
    const float centerX = params.centerX + pivotOffsetX * cosine - pivotOffsetY * sine;
    const float centerY = params.centerY + pivotOffsetX * sine + pivotOffsetY * cosine;

    Render::RenderSprite2DInput input{
        .spriteKey = params.spriteKey != 0 ? params.spriteKey : spriteKeyFromAssetId(spriteAsset.header().assetId),
        .stableEntityKey = params.stableEntityKey == 0 ? 1ULL : params.stableEntityKey,
        .centerX = centerX,
        .centerY = centerY,
        .rotationRadians = params.rotationRadians,
        .widthMeters = widthMeters,
        .heightMeters = heightMeters,
        .scaleX = params.scaleX,
        .scaleY = params.scaleY,
        .u0 = sprite->u0,
        .v0 = sprite->v0,
        .u1 = sprite->u1,
        .v1 = sprite->v1,
        .sortingLayer = params.sortingLayer,
        .orderInLayer = params.orderInLayer,
        .red = params.red,
        .green = params.green,
        .blue = params.blue,
        .alpha = params.alpha,
        .flipX = params.flipX,
        .flipY = params.flipY,
        .visible = params.visible,
    };
    return input;
}

} // namespace Tina::Asset
