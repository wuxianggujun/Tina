#pragma once

#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/asset_format/MaterialPayload.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/StaticMeshPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/error/Result.hpp>

#include <vector>

namespace Tina::Asset {

// Typed payload accessors over loaded CookedAssetFile CPU payload.
// Caller must keep the CookedAssetFile (or lease) alive for borrowed views.

[[nodiscard]] Core::Result<AssetFormat::Texture2DPayloadView>
parseTexture2DFromCooked(const CookedAssetFile& file);

[[nodiscard]] Core::Result<AssetFormat::SpritePayloadView>
parseSpriteFromCooked(const CookedAssetFile& file);

[[nodiscard]] Core::Result<AssetFormat::TilesetPayloadView>
parseTilesetFromCooked(const CookedAssetFile& file);

[[nodiscard]] Core::Result<AssetFormat::TileMapPayloadView>
parseTileMapFromCooked(const CookedAssetFile& file);

// M11-A18: AudioClip cooked payload accessor (float32 PCM). Playback still uses
// non-owning AudioPcmClipView; caller keeps CookedAssetFile / future lease alive.
[[nodiscard]] Core::Result<AssetFormat::AudioClipPayloadView>
parseAudioClipFromCooked(const CookedAssetFile& file);

// M11-E0/E1: StaticMesh cooked payload accessor (P3_N3_UV2 + U16 indices).
// Caller keeps CookedAssetFile / lease alive for borrowed vertex/index spans.
[[nodiscard]] Core::Result<AssetFormat::StaticMeshPayloadView>
parseStaticMeshFromCooked(const CookedAssetFile& file);

// Material cooked payload accessor (UnlitBaseColor + cooked PBR factors/texture flags).
[[nodiscard]] Core::Result<AssetFormat::MaterialPayloadView>
parseMaterialFromCooked(const CookedAssetFile& file);

// M11-E6b: Prefab cooked payload. Node storage is owned by the returned object;
// the PrefabPayloadView.nodes span aliases into `nodes`.
struct OwnedPrefabPayload final {
    std::vector<AssetFormat::PrefabNodeView> nodes{};
    AssetFormat::PrefabPayloadView view{};
};

[[nodiscard]] Core::Result<OwnedPrefabPayload> parsePrefabFromCooked(const CookedAssetFile& file);

} // namespace Tina::Asset
