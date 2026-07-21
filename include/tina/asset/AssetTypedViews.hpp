#pragma once

#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/error/Result.hpp>

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

} // namespace Tina::Asset
