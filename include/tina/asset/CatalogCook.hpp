#pragma once

#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/SourceImportCapture.hpp>
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

struct CatalogCookSourceResult final {
    CatalogCookRequest request{};
    SourceImportCandidate sourceImports{};
};

struct CatalogCookResult final {
    Core::u32 entryCount = 0;
    Core::u32 dependencyCount = 0;
    std::vector<std::byte> manifestBytes{};
};

// Builds cooked object bytes + manifest in memory (no disk IO).
[[nodiscard]] Core::Result<CatalogCookResult> cookCatalogPackage(const CatalogCookRequest& request);

// cookCatalogPackage + best-effort in-place publish under catalogRoot (manifest.tmnft + objects/).
[[nodiscard]] Core::Status cookAndPublishCatalogPackage(std::string_view catalogRootUtf8,
                                                        const CatalogCookRequest& request);

struct CatalogPackageStageConfig final {
    CatalogPackageOpenConfig validation{};
};

// Cooks into a fresh staging root, publishes its objects there, and returns a fully validated
// immutable CatalogSnapshot. The staging root must not exist; failures never modify a live package.
// On success the caller owns the staging root and must treat it as immutable.
[[nodiscard]] Core::Result<CatalogSnapshot>
cookAndStageCatalogPackage(std::string_view stagingRootUtf8, const CatalogCookRequest& request,
                           CatalogPackageStageConfig config);

// Assembles a complete package in a fresh staging root from unchanged baseline objects plus dirty
// assets cooked with the current format. cleanAssetIds must be unique and present in the fully
// validated baseline snapshot. stagingRootUtf8 must resolve outside baselineRootUtf8. Clean object
// bytes are read from baselineRootUtf8 and copied exactly; they are never linked and the baseline
// root is never modified. Dirty and clean ids must be disjoint, all objects must use
// dirtyRequest.targetPlatform, and the rebuilt dependency graph must be valid. When both clean and
// dirty inputs are empty, a valid baseline is required and an empty Catalog is staged.
[[nodiscard]] Core::Result<CatalogSnapshot>
cookAndStageIncrementalCatalogPackage(std::string_view stagingRootUtf8,
                                      std::string_view baselineRootUtf8,
                                      const CatalogSnapshot& baseline,
                                      std::span<const Core::AssetId> cleanAssetIds,
                                      const CatalogCookRequest& dirtyRequest,
                                      CatalogPackageStageConfig config);

// Minimal line recipe format (UTF-8):
//   # comment
//   platform WindowsX64
//   asset Texture2D <32hexId> <payloadPath>
//   asset Material <32hexId> <payloadPath> <dep32hex:Kind> ...
//   texture2d <32hexId> <width> <height> <hexRRGGBBAA> ...   // inline Rgba8Unorm pixels
//   sprite <32hexId> <texture32hexId> [u0 v0 u1 v1 pivotX pivotY ppu]
//   spriteanim <32hexId> <Once|Loop|PingPong> <frame>...
//     frame := <sprite32hexId>:<durationSeconds>[#<event>[#<event>...]]
//     event := <tag>@<offset>   // tag: 0x1F2E3D4C (non-zero u32) or IDENT hashed with FNV-1a 32
//                               // offset: [0,1] decimal (0.5) or percentage (50%)
//     // e.g. spriteanim <clipId> Loop <spriteA>:0.10#FOOTSTEP@50%#0xDEADBEEF@0.75 <spriteB>:0.15
//     // Events are sorted by ascending offset per frame; authoring order breaks ties.
//   audioclip <32hexId> <sampleRate> <channels> <frameCount> <f0...>
//   audioclip <32hexId> <sampleRate> <channels> <frameCount> sine <freqHz>
//   audioclip <32hexId> file <relativeOrAbsolute.wav>  // PCM16 WAV only (M11-A20)
//   staticmesh <32hexId> cube                          // canonical unit cube (M11-E1)
//   material <32hexId> unlit <r> <g> <b> [a] [texId]   // optional Texture2D dep (M11-E4/E5)
//   prefab <32hexId> root [mesh32hex] [material32hex]  // single-root Prefab (M11-E6b)
//   tileset <32hexId> <texture32hexId> <tilePxW> <tilePxH>
//   tile <localId> <materialFlags> <u0> <v0> <u1> <v1>   // after tileset; ends at next non-tile
//   tilemap <32hexId> <tileset32hexId> <widthCells> <heightCells> <cellSizeMeters>
//   tilelayer <stableLayerId> <0|1 visible> <name>
//   property <key> <value>                                 // layer property (no whitespace in either token)
//   row <localId>...                                       // inside a tilelayer; heightCells rows
//   endlayer
//   objectlayer <stableLayerId> <0|1 visible> <name>
//   point <stableObjectId> <0|1 visible> <name> <x> <y>
//   rectangle <stableObjectId> <0|1 visible> <name> <x> <y> <width> <height>
//   objectproperty <stableObjectId> <key> <value>
//   endlayer
//   endtilemap                                            // required; no single-layer fallback
// Paths are relative to the recipe file directory unless absolute.
// Inline typed lines build the current versioned payload without pre-encoded files.
[[nodiscard]] Core::Result<CatalogCookRequest> loadCatalogCookRecipeFile(std::string_view recipeUtf8Path);

// Reads only the recipe document and resolves its declared target platform. Referenced payload,
// image, and audio sources are not opened, so hosts can select a cook target before import.
[[nodiscard]] Core::Result<AssetFormat::TargetPlatform>
loadCatalogCookRecipeTargetPlatform(std::string_view recipeUtf8Path);

// Loads the same recipe request while capturing the exact already-read recipe, generic payload,
// and WAV bytes into one CatalogRecipe import unit. Every source must remain under sourceRootUtf8.
[[nodiscard]] Core::Result<CatalogCookSourceResult>
loadCatalogCookRecipeSourceFile(std::string_view recipeUtf8Path, SourceImportCaptureConfig captureConfig);

// Parse recipe text with an explicit base directory for relative payload paths.
[[nodiscard]] Core::Result<CatalogCookRequest> parseCatalogCookRecipe(std::string_view recipeText,
                                                                      std::string_view baseDirectoryUtf8);

} // namespace Tina::Asset
