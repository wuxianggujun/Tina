#include <tina/asset/AssetTypedViews.hpp>

#include <tina/asset/AssetErrors.hpp>

namespace Tina::Asset {

Core::Result<AssetFormat::Texture2DPayloadView> parseTexture2DFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Texture2D)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not Texture2D");
    }
    return AssetFormat::parseTexture2DPayload(file.payload());
}

Core::Result<AssetFormat::SpritePayloadView> parseSpriteFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Sprite)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not Sprite");
    }
    return AssetFormat::parseSpritePayload(file.payload());
}

Core::Result<AssetFormat::TilesetPayloadView> parseTilesetFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::Tileset)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not Tileset");
    }
    return AssetFormat::parseTilesetPayload(file.payload());
}

Core::Result<AssetFormat::TileMapPayloadView> parseTileMapFromCooked(const CookedAssetFile& file)
{
    if (!file)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "cooked asset is empty");
    }
    if (file.header().assetKind != AssetFormat::AssetKind::TileMap)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch, "cooked asset is not TileMap");
    }
    return AssetFormat::parseTileMapPayload(file.payload());
}

} // namespace Tina::Asset
