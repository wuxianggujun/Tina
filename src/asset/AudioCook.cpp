#include "AudioCook.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/core/io/ReadFile.hpp>

namespace Tina::Asset::Detail {

Core::Result<std::vector<std::byte>> cookAudioClipPayload(std::span<const std::byte> encoded)
{
    Audio::AudioDecodeConfig config;
    // Leave room for both headers in the runtime's complete cooked-file read.
    config.maxDecodedBytes = Core::MaxReadFileBytes - AssetFormat::Wire::CookedAssetHeaderBytes -
                             AssetFormat::AudioClipWire::HeaderBytes;
    auto decoded = Audio::decodeAudioMemory(encoded, config);
    if (!decoded)
    {
        return Core::failure(std::move(decoded.error()).withContext("cookAudioClipPayload", "decodeSource"));
    }
    if (decoded->frameCount() > AssetFormat::AudioClipWire::MaxFrameCount)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "Audio source exceeds the cooked frame limit");
    }
    return AssetFormat::writeAudioClipPayloadBytes({
        .channels = static_cast<Core::u16>(decoded->channels()),
        .sampleRate = decoded->sampleRate(),
        .frameCount = static_cast<Core::u32>(decoded->frameCount()),
        .interleavedPcm = decoded->interleavedPcm(),
    });
}

} // namespace Tina::Asset::Detail
