#pragma once

#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace Tina::Asset::Detail {

// Cook-time PCM WAV decode only (RIFF/WAVE, PCM 16-bit). Keeps Asset free of
// miniaudio. Defined in CatalogCook.cpp and shared with the media importer.
[[nodiscard]] Core::Result<AssetFormat::AudioClipPayloadDesc>
decodePcm16WavToClipDesc(std::span<const std::byte> bytes, std::vector<float>& pcmOut);

} // namespace Tina::Asset::Detail
