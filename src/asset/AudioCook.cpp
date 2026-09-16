#include "AudioCook.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/audio/AudioEncode.hpp>
#include <tina/core/io/ReadFile.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace Tina::Asset::Detail {
namespace {

constexpr Core::u64 SeekWindowFrames = 256;

[[nodiscard]] Core::Status samplesMatch(std::span<const float> left, std::span<const float> right) noexcept
{
    if (left.size() != right.size() || left.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "EncodedStream seek produced a different PCM window");
    }
    for (Core::usize index = 0; index < left.size(); ++index)
    {
        if (left[index] != right[index])
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "EncodedStream seek is not sample-exact");
        }
    }
    return Core::success();
}

[[nodiscard]] Core::Result<Core::u32> drainAndCount(Audio::AudioDecoder& decoder, Core::u32 channels)
{
    std::array<float, 4096 * 2> block{};
    Core::u64 frames = 0;
    for (;;)
    {
        auto read = decoder.readPcm(std::span<float>{block}.first(4096U * channels));
        if (!read) { return Core::failure(std::move(read.error())); }
        if (*read == 0) { break; }
        frames += *read;
        if (frames > AssetFormat::AudioClipWire::MaxFrameCount)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "Audio source exceeds the cooked frame limit");
        }
    }
    if (frames == 0 || frames > AssetFormat::AudioClipWire::MaxFrameCount)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "Audio source is empty or exceeds the frame limit");
    }
    return static_cast<Core::u32>(frames);
}

[[nodiscard]] Core::Result<std::vector<float>> readExactWindow(Audio::AudioDecoder& decoder, Core::u32 channels,
                                                               Core::u64 frames)
{
    std::vector<float> window(static_cast<Core::usize>(frames) * channels);
    Core::u64 got = 0;
    while (got < frames)
    {
        auto read = decoder.readPcm(std::span<float>{
            window.data() + static_cast<Core::usize>(got) * channels,
            static_cast<Core::usize>(frames - got) * channels});
        if (!read) { return Core::failure(std::move(read.error())); }
        if (*read == 0)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig, "EncodedStream seek window ended early");
        }
        got += *read;
    }
    return window;
}

[[nodiscard]] Core::Status proveExactSeek(std::span<const std::byte> encoded, Core::u32 channels,
                                          Core::u32 frameCount)
{
    auto decoder = Audio::AudioDecoder::open(encoded);
    if (!decoder) { return Core::failure(std::move(decoder.error())); }
    const auto window = static_cast<Core::u64>((std::min)(SeekWindowFrames, static_cast<Core::u64>(frameCount)));
    if (auto status = decoder->seekFrame(0); !status) { return status; }
    auto first = readExactWindow(*decoder, channels, window);
    if (!first) { return Core::failure(std::move(first.error())); }
    const Core::u64 mid = frameCount > window ? (frameCount - window) / 2U : 0;
    if (auto status = decoder->seekFrame(mid); !status)
    {
        return Core::failure(std::move(status.error()).withContext("proveExactSeek", "seekMid"));
    }
    auto middle = readExactWindow(*decoder, channels, window);
    if (!middle) { return Core::failure(std::move(middle.error())); }

    auto replay = Audio::AudioDecoder::open(encoded);
    if (!replay) { return Core::failure(std::move(replay.error())); }
    if (auto status = replay->seekFrame(0); !status) { return status; }
    auto firstAgain = readExactWindow(*replay, channels, window);
    if (!firstAgain) { return Core::failure(std::move(firstAgain.error())); }
    if (auto status = samplesMatch(*first, *firstAgain); !status) { return status; }
    if (auto status = replay->seekFrame(mid); !status) { return status; }
    auto middleAgain = readExactWindow(*replay, channels, window);
    if (!middleAgain) { return Core::failure(std::move(middleAgain.error())); }
    return samplesMatch(*middle, *middleAgain);
}

} // namespace

Core::Result<std::vector<std::byte>> cookAudioClipPayload(std::span<const std::byte> encoded,
                                                          AssetFormat::AudioClipStorage storage)
{
    if (storage == AssetFormat::AudioClipStorage::MemoryPcm)
    {
        Audio::AudioDecodeConfig config;
        config.maxDecodedBytes = (std::min)(
            Audio::AudioDecodeConfig{}.maxDecodedBytes, AssetFormat::AudioClipWire::MaxMemoryPcmBytes);
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
            .storage = AssetFormat::AudioClipStorage::MemoryPcm,
            .codec = AssetFormat::AudioClipCodec::PcmF32,
            .interleavedPcm = decoded->interleavedPcm(),
        });
    }
    if (storage != AssetFormat::AudioClipStorage::EncodedStream)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "Audio clip storage is unsupported");
    }

    Audio::AudioDecodeConfig decodeConfig;
    decodeConfig.outputSampleRate = 48000;
    auto decoder = Audio::AudioDecoder::open(encoded, decodeConfig);
    if (!decoder)
    {
        return Core::failure(std::move(decoder.error()).withContext("cookAudioClipPayload", "openStream"));
    }
    if (decoder->channels() > 2U)
    {
        decodeConfig.outputChannels = 2;
        decoder = Audio::AudioDecoder::open(encoded, decodeConfig);
        if (!decoder)
        {
            return Core::failure(std::move(decoder.error()).withContext("cookAudioClipPayload", "downmixStream"));
        }
    }
    auto opus = Audio::encodeOpusOggFromDecoder(*decoder);
    if (!opus)
    {
        return Core::failure(std::move(opus.error()).withContext("cookAudioClipPayload", "encodeOpus"));
    }
    auto cookedDecoder = Audio::AudioDecoder::open(*opus);
    if (!cookedDecoder)
    {
        return Core::failure(std::move(cookedDecoder.error()).withContext("cookAudioClipPayload", "openOpus"));
    }
    auto frameCount = drainAndCount(*cookedDecoder, cookedDecoder->channels());
    if (!frameCount)
    {
        return Core::failure(std::move(frameCount.error()).withContext("cookAudioClipPayload", "validateStream"));
    }
    if (auto status = proveExactSeek(*opus, cookedDecoder->channels(), *frameCount); !status)
    {
        return Core::failure(std::move(status.error()).withContext("cookAudioClipPayload", "seekGold"));
    }
    return AssetFormat::writeAudioClipPayloadBytes({
        .channels = static_cast<Core::u16>(cookedDecoder->channels()),
        .sampleRate = cookedDecoder->sampleRate(),
        .frameCount = *frameCount,
        .storage = AssetFormat::AudioClipStorage::EncodedStream,
        .codec = AssetFormat::AudioClipCodec::Opus,
        .encoded = *opus,
    });
}

} // namespace Tina::Asset::Detail
