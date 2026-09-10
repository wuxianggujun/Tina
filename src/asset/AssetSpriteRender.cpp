#include <tina/asset/AssetSpriteRender.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/render/RenderErrors.hpp>

#include <cmath>

namespace Tina::Asset {

Core::Result<Render::RenderSprite2DInput> makeSpriteRenderInput(const CookedAssetFile& spriteAsset,
                                                                const CookedAssetFile* textureAsset,
                                                                Render::FrameResourceRef texture,
                                                                const Render::Sprite2DProjection& projection,
                                                                const SpriteRenderParams& params)
{
    if (!texture)
    {
        return Core::failure(Render::RenderErrorCode::InvalidFrameResource,
                             "sprite render input requires a valid frame texture resource");
    }
    if (!projection.isValid() || params.stableEntityKey == 0 || !std::isfinite(params.elevation)) {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "sprite projection, elevation or stable key is invalid");
    }
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
        const float ppu = sprite->pixelsPerUnit;
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

    const auto quad = projection.billboard({
        .positionX = params.positionX,
        .positionY = params.positionY,
        .elevation = params.elevation,
        .rotationRadians = params.rotationRadians,
        .widthMeters = widthMeters,
        .heightMeters = heightMeters,
        .scaleX = params.scaleX,
        .scaleY = params.scaleY,
        .pivotX = sprite->pivotX,
        .pivotY = sprite->pivotY,
    });
    if (!quad.isValid()) {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "sprite projection produced invalid quad bounds");
    }

    Render::RenderSprite2DInput input{
        .texture = texture,
        .stableEntityKey = params.stableEntityKey,
        .quad = quad,
        .u0 = sprite->u0,
        .v0 = sprite->v0,
        .u1 = sprite->u1,
        .v1 = sprite->v1,
        .sortingLayer = params.sortingLayer,
        .sortDepth = projection.sortDepth({params.positionX, params.positionY, params.elevation}),
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
