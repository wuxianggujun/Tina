#include "BgfxSprite2DGeometry.hpp"

#include <tina/render/RenderErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace Tina::Render::Bgfx {
namespace {

inline constexpr u32 VerticesPerSprite = 4;
inline constexpr u32 IndicesPerSprite = 6;

static_assert(std::is_standard_layout_v<BgfxSprite2DVertex>);
static_assert(sizeof(BgfxSprite2DVertex) == sizeof(float) * 4U + sizeof(u32));

[[nodiscard]] bool finite(float value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool finiteCamera(const RenderCamera2D& camera) noexcept
{
    const double right =
        static_cast<double>(camera.normalizedViewport.x) + static_cast<double>(camera.normalizedViewport.width);
    const double bottom =
        static_cast<double>(camera.normalizedViewport.y) + static_cast<double>(camera.normalizedViewport.height);
    return camera.stableCameraKey != 0 && finite(camera.centerX) && finite(camera.centerY) &&
           finite(camera.rotationRadians) && finite(camera.worldWidth) && finite(camera.worldHeight) &&
           finite(camera.actualPixelsPerMeter) && camera.worldWidth > 0.0F && camera.worldHeight > 0.0F &&
           camera.actualPixelsPerMeter > 0.0F && finite(camera.normalizedViewport.x) &&
           finite(camera.normalizedViewport.y) && finite(camera.normalizedViewport.width) &&
           finite(camera.normalizedViewport.height) && camera.normalizedViewport.x >= 0.0F &&
           camera.normalizedViewport.y >= 0.0F && camera.normalizedViewport.width > 0.0F &&
           camera.normalizedViewport.height > 0.0F && std::isfinite(right) && std::isfinite(bottom) && right <= 1.0 &&
           bottom <= 1.0;
}

[[nodiscard]] bool finiteSprite(const RenderSprite2DItem& sprite) noexcept
{
    const float scaledWidth = sprite.widthMeters * std::abs(sprite.scaleX);
    const float scaledHeight = sprite.heightMeters * std::abs(sprite.scaleY);
    return sprite.spriteKey != 0 && sprite.stableEntityKey != 0 && finite(sprite.centerX) && finite(sprite.centerY) &&
           finite(sprite.rotationRadians) && finite(sprite.widthMeters) && finite(sprite.heightMeters) &&
           finite(sprite.scaleX) && finite(sprite.scaleY) && sprite.widthMeters > 0.0F && sprite.heightMeters > 0.0F &&
           sprite.scaleX != 0.0F && sprite.scaleY != 0.0F && finite(scaledWidth) && finite(scaledHeight) &&
           scaledWidth > 0.0F && scaledHeight > 0.0F;
}

[[nodiscard]] bool sortedBeforeOrEquivalent(const RenderSprite2DItem& left, const RenderSprite2DItem& right) noexcept
{
    if (left.sortingLayer != right.sortingLayer)
    {
        return left.sortingLayer < right.sortingLayer;
    }
    if (left.orderInLayer != right.orderInLayer)
    {
        return left.orderInLayer < right.orderInLayer;
    }
    if (left.stableEntityKey != right.stableEntityKey)
    {
        return left.stableEntityKey < right.stableEntityKey;
    }
    return left.insertionOrder <= right.insertionOrder;
}

[[nodiscard]] u32 packAbgr(const RenderSprite2DItem& sprite) noexcept
{
    return (static_cast<u32>(sprite.alpha) << 24U) | (static_cast<u32>(sprite.blue) << 16U) |
           (static_cast<u32>(sprite.green) << 8U) | static_cast<u32>(sprite.red);
}

[[nodiscard]] Core::Result<BgfxSprite2DFrameRequirements> invalidFrame(const char* message)
{
    return Core::failure(RenderErrorCode::InvalidRenderSceneInput, message);
}

[[nodiscard]] Core::Result<BgfxSprite2DFrameRequirements> unsupportedFixtureKey()
{
    return Core::failure(Core::CoreErrorCode::Unsupported, "Sprite2D received an unsupported procedural fixture key");
}

[[nodiscard]] Core::Result<BgfxSprite2DFrameRequirements> countRequirements(std::span<const RenderSprite2DItem> sprites)
{
    if (sprites.size() > static_cast<usize>((std::numeric_limits<u32>::max)()))
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Sprite2D frame exceeds backend sprite count limits");
    }
    const auto spriteCount = static_cast<u32>(sprites.size());
    if (spriteCount > (std::numeric_limits<u32>::max)() / VerticesPerSprite ||
        spriteCount > (std::numeric_limits<u32>::max)() / IndicesPerSprite)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Sprite2D frame exceeds backend geometry count limits");
    }

    u32 batchCount = 0;
    u32 previousKey = 0;
    for (const RenderSprite2DItem& sprite : sprites)
    {
        if (batchCount == 0 || sprite.spriteKey != previousKey)
        {
            ++batchCount;
            previousKey = sprite.spriteKey;
        }
    }

    return BgfxSprite2DFrameRequirements{
        .spriteCount = spriteCount,
        .vertexCount = spriteCount * VerticesPerSprite,
        .indexCount = spriteCount * IndicesPerSprite,
        .batchCount = batchCount,
    };
}

void writeSprite(const RenderSprite2DItem& sprite, std::span<BgfxSprite2DVertex> vertices, std::span<u32> indices,
                 u32 firstVertex) noexcept
{
    const float cosine = std::cos(sprite.rotationRadians);
    const float sine = std::sin(sprite.rotationRadians);
    const float halfWidth = 0.5F * sprite.widthMeters * sprite.scaleX;
    const float halfHeight = 0.5F * sprite.heightMeters * sprite.scaleY;
    const float axisXx = cosine * halfWidth;
    const float axisXy = sine * halfWidth;
    const float axisYx = -sine * halfHeight;
    const float axisYy = cosine * halfHeight;

    const float leftU = sprite.flipX ? 1.0F : 0.0F;
    const float rightU = sprite.flipX ? 0.0F : 1.0F;
    const float topV = sprite.flipY ? 1.0F : 0.0F;
    const float bottomV = sprite.flipY ? 0.0F : 1.0F;
    const u32 color = packAbgr(sprite);

    vertices[0] = BgfxSprite2DVertex{
        .positionX = sprite.centerX - axisXx - axisYx,
        .positionY = sprite.centerY - axisXy - axisYy,
        .textureU = leftU,
        .textureV = bottomV,
        .abgr = color,
    };
    vertices[1] = BgfxSprite2DVertex{
        .positionX = sprite.centerX + axisXx - axisYx,
        .positionY = sprite.centerY + axisXy - axisYy,
        .textureU = rightU,
        .textureV = bottomV,
        .abgr = color,
    };
    vertices[2] = BgfxSprite2DVertex{
        .positionX = sprite.centerX + axisXx + axisYx,
        .positionY = sprite.centerY + axisXy + axisYy,
        .textureU = rightU,
        .textureV = topV,
        .abgr = color,
    };
    vertices[3] = BgfxSprite2DVertex{
        .positionX = sprite.centerX - axisXx + axisYx,
        .positionY = sprite.centerY - axisXy + axisYy,
        .textureU = leftU,
        .textureV = topV,
        .abgr = color,
    };

    indices[0] = firstVertex;
    indices[1] = firstVertex + 1U;
    indices[2] = firstVertex + 2U;
    indices[3] = firstVertex;
    indices[4] = firstVertex + 2U;
    indices[5] = firstVertex + 3U;
}

} // namespace

Core::Result<BgfxSprite2DFrameRequirements> checkedSprite2DFrame(RenderSceneView scene)
{
    const std::span<const RenderSprite2DItem> sprites = scene.sprites2D();
    if (sprites.empty())
    {
        return BgfxSprite2DFrameRequirements{};
    }
    if (!scene.camera2D().has_value() || !finiteCamera(*scene.camera2D()))
    {
        return invalidFrame("Sprite2D sprites require a valid Camera2D");
    }

    for (usize index = 0; index < sprites.size(); ++index)
    {
        const RenderSprite2DItem& sprite = sprites[index];
        if (sprite.spriteKey != Sprite2DFixtureSpriteKey)
        {
            return unsupportedFixtureKey();
        }
        if (!finiteSprite(sprite))
        {
            return invalidFrame("Sprite2D contains invalid geometry or resource values");
        }
        if (index != 0 && !sortedBeforeOrEquivalent(sprites[index - 1U], sprite))
        {
            return invalidFrame("Sprite2D items must be sorted by RenderScene before reaching bgfx");
        }
    }

    return countRequirements(sprites);
}

Core::Result<BgfxSprite2DFrameRequirements>
writeSprite2DGeometry(RenderSceneView scene, std::span<BgfxSprite2DVertex> vertices, std::span<u32> indices)
{
    auto requirements = checkedSprite2DFrame(scene);
    if (!requirements)
    {
        return Core::failure(std::move(requirements.error()));
    }
    if (vertices.size() < requirements->vertexCount || indices.size() < requirements->indexCount)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Sprite2D output buffers do not have enough capacity");
    }

    for (usize spriteIndex = 0; spriteIndex < scene.sprites2D().size(); ++spriteIndex)
    {
        const usize vertexOffset = spriteIndex * VerticesPerSprite;
        const usize indexOffset = spriteIndex * IndicesPerSprite;
        writeSprite(scene.sprites2D()[spriteIndex], vertices.subspan(vertexOffset, VerticesPerSprite),
                    indices.subspan(indexOffset, IndicesPerSprite), static_cast<u32>(vertexOffset));
    }
    return *requirements;
}

} // namespace Tina::Render::Bgfx
