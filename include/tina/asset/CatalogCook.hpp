#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Asset {

struct CatalogCookAssetSpec final {
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Core::AssetId assetId{};
    Core::u16 assetTypeVersion = 1;
    std::vector<std::byte> payload{};
    std::vector<AssetFormat::CookedAssetWriteDependency> dependencies{};
};

struct CatalogCookRequest final {
    AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::WindowsX64;
    // Unsorted inputs are sorted by AssetId before writing; duplicate ids fail.
    std::vector<CatalogCookAssetSpec> assets{};
};

struct CatalogCookResult final {
    Core::u32 entryCount = 0;
    Core::u32 dependencyCount = 0;
    std::vector<std::byte> manifestBytes{};
};

// Builds cooked object bytes + manifest in memory (no disk IO).
[[nodiscard]] Core::Result<CatalogCookResult> cookCatalogPackage(const CatalogCookRequest& request);

// cookCatalogPackage + publishCatalogPackage under catalogRoot (manifest.tmnft + objects/).
[[nodiscard]] Core::Status cookAndPublishCatalogPackage(std::string_view catalogRootUtf8,
                                                        const CatalogCookRequest& request);

// Minimal line recipe format (UTF-8):
//   # comment
//   platform WindowsX64
//   asset Texture2D <32hexId> <payloadPath>
//   asset Material <32hexId> <payloadPath> <dep32hex:Kind> ...
//   texture2d <32hexId> <width> <height> <hexRRGGBBAA> ...   // inline Rgba8Unorm pixels
//   sprite <32hexId> <texture32hexId> [u0 v0 u1 v1 pivotX pivotY ppu]
//   audioclip <32hexId> <sampleRate> <channels> <frameCount> <f0...>
//   audioclip <32hexId> <sampleRate> <channels> <frameCount> sine <freqHz>
//   audioclip <32hexId> file <relativeOrAbsolute.wav>  // PCM16 WAV only (M11-A20)
//   staticmesh <32hexId> cube                          // canonical unit cube (M11-E1)
//   material <32hexId> unlit <r> <g> <b> [a]           // UnlitBaseColor linear RGBA (M11-E4)
//   tileset <32hexId> <texture32hexId> <tilePxW> <tilePxH>
//   tile <localId> <materialFlags> <u0> <v0> <u1> <v1>   // after tileset; ends at next non-tile
//   tilemap <32hexId> <tileset32hexId> <widthCells> <heightCells> <cellSizeMeters>
//   row <localId>...                                      // after tilemap; heightCells rows
// Paths are relative to the recipe file directory unless absolute.
// Inline typed lines build payload v1 without pre-encoded files.
[[nodiscard]] Core::Result<CatalogCookRequest> loadCatalogCookRecipeFile(std::string_view recipeUtf8Path);

// Parse recipe text with an explicit base directory for relative payload paths.
[[nodiscard]] Core::Result<CatalogCookRequest> parseCatalogCookRecipe(std::string_view recipeText,
                                                                      std::string_view baseDirectoryUtf8);

} // namespace Tina::Asset
