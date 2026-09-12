#include <tina/audio/AudioDecode.hpp>

#include "OggValidation.hpp"
#include "libopus/miniaudio_libopus.h"
#include "libvorbis/miniaudio_libvorbis.h"

#include <tina/audio/AudioErrors.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

namespace Tina::Audio {
namespace {

constexpr Core::u32 MinimumSampleRate = 1000;
constexpr Core::u32 MaximumSampleRate = 192000;
constexpr Core::usize DecodeBlockFrames = 4096;

struct DecoderOwner final {
    ma_decoder decoder{};
    bool initialized = false;

    ~DecoderOwner() { reset(); }
    void reset() noexcept
    {
        if (initialized)
        {
            (void)ma_decoder_uninit(&decoder);
            initialized = false;
        }
    }
};

[[nodiscard]] Core::Error decoderError(ma_result result, std::string_view operation)
{
    Core::Error error{
        result == MA_OUT_OF_MEMORY ? Core::CoreErrorCode::OutOfMemory : AudioErrorCode::DecodeFailed,
        "Audio decoder could not read a complete valid stream"};
    error.setNativeCode(result);
    error.addContext("decodeAudioMemory", operation);
    return error;
}

[[nodiscard]] Core::Status selectDecoder(std::span<const std::byte> encoded,
                                          ma_decoder_config& config,
                                          ma_decoding_backend_vtable*& customBackend)
{
    if (encoded.size() >= 4 && std::memcmp(encoded.data(), "OggS", 4) == 0)
    {
        auto codec = Detail::validateOggAudio(encoded);
        if (!codec) { return Core::failure(std::move(codec.error())); }
        customBackend = *codec == Detail::OggAudioCodec::Vorbis
                            ? ma_decoding_backend_libvorbis : ma_decoding_backend_libopus;
        config.ppCustomBackendVTables = &customBackend;
        config.customBackendCount = 1;
        return Core::success();
    }
    if (encoded.size() >= 12 && std::memcmp(encoded.data(), "RIFF", 4) == 0 &&
        std::memcmp(encoded.data() + 8, "WAVE", 4) == 0)
    {
        Core::u64 riffBytes = 0;
        for (Core::u32 byte = 0; byte < 4; ++byte)
        {
            riffBytes |= static_cast<Core::u64>(std::to_integer<Core::u8>(encoded[4 + byte])) << (8U * byte);
        }
        if (riffBytes < 4 || riffBytes + 8 > encoded.size())
        {
            return Core::failure(AudioErrorCode::DecodeFailed, "WAV RIFF chunk is truncated");
        }
        config.encodingFormat = ma_encoding_format_wav;
        return Core::success();
    }
    if (encoded.size() >= 4 && std::memcmp(encoded.data(), "fLaC", 4) == 0)
    {
        config.encodingFormat = ma_encoding_format_flac;
        return Core::success();
    }
    const bool id3 = encoded.size() >= 3 && std::memcmp(encoded.data(), "ID3", 3) == 0;
    const bool mpeg = encoded.size() >= 2 && encoded[0] == std::byte{0xff} &&
                      (std::to_integer<Core::u8>(encoded[1]) & 0xe0U) == 0xe0U;
    if (id3 || mpeg)
    {
        config.encodingFormat = ma_encoding_format_mp3;
        return Core::success();
    }
    return Core::failure(AudioErrorCode::NotSupported,
                         "Audio source must contain WAV, FLAC, MP3, Ogg Vorbis or Ogg Opus");
}

[[nodiscard]] Core::Result<std::vector<float>> readPcm(ma_decoder& decoder,
                                                       Core::u64 maxDecodedBytes)
{
    const Core::usize channels = decoder.outputChannels;
    const auto maxSamples = static_cast<Core::usize>((std::min)(
        maxDecodedBytes / sizeof(float),
        static_cast<Core::u64>((std::numeric_limits<Core::usize>::max)() / sizeof(float))));
    const Core::u64 maxFrames = maxSamples / channels;
    ma_uint64 sourceFrames = 0;
    const auto lengthResult = ma_data_source_get_length_in_pcm_frames(decoder.pBackend, &sourceFrames);
    if (lengthResult != MA_SUCCESS && lengthResult != MA_NOT_IMPLEMENTED)
    {
        return Core::failure(decoderError(lengthResult, "queryLength"));
    }
    ma_uint64 expectedFrames = 0;
    if (lengthResult == MA_SUCCESS && sourceFrames != 0)
    {
        // ma_decoder_get_length_in_pcm_frames uses a floating-point ceil estimate
        // (4800 frames at 48 -> 24 kHz can become 2401). The converter predicts
        // its exact output from the source length and initial resampler state.
        const auto convertedLength = ma_data_converter_get_expected_output_frame_count(
            &decoder.converter, sourceFrames, &expectedFrames);
        if (convertedLength != MA_SUCCESS && convertedLength != MA_NOT_IMPLEMENTED)
        {
            return Core::failure(decoderError(convertedLength, "queryConvertedLength"));
        }
    }
    if (expectedFrames > maxFrames || maxFrames == 0)
    {
        return Core::failure(AudioErrorCode::DecodeLimitExceeded, "Decoded PCM exceeds the byte budget");
    }

    std::vector<float> pcm;
    if (expectedFrames != 0) { pcm.reserve(static_cast<Core::usize>(expectedFrames) * channels); }
    std::array<float, DecodeBlockFrames * AudioPcmStreamMaxChannels> block{};
    for (;;)
    {
        ma_uint64 framesRead = 0;
        const auto result = ma_decoder_read_pcm_frames(&decoder, block.data(), DecodeBlockFrames, &framesRead);
        if ((result != MA_SUCCESS && result != MA_AT_END) || framesRead > DecodeBlockFrames)
        {
            return Core::failure(decoderError(result, "readPcm"));
        }
        const Core::usize samplesRead = static_cast<Core::usize>(framesRead) * channels;
        if (samplesRead > maxSamples - pcm.size())
        {
            return Core::failure(AudioErrorCode::DecodeLimitExceeded, "Decoded PCM exceeds the byte budget");
        }
        const auto samples = std::span<const float>{block}.first(samplesRead);
        if (!std::all_of(samples.begin(), samples.end(), [](float sample) { return std::isfinite(sample); }))
        {
            return Core::failure(AudioErrorCode::DecodeFailed, "Decoded PCM contains a non-finite sample");
        }
        const auto required = pcm.size() + samplesRead;
        if (required > pcm.capacity())
        {
            // Unknown-length sources may grow, but vector's implicit geometric
            // growth must not allocate beyond the caller's output byte budget.
            const auto growth = pcm.capacity() > maxSamples / 2 ? maxSamples : pcm.capacity() * 2;
            pcm.reserve((std::max)(required, growth));
        }
        pcm.insert(pcm.end(), samples.begin(), samples.end());
        if (framesRead == 0 || result == MA_AT_END) { break; }
    }
    if (pcm.empty() || (expectedFrames != 0 && pcm.size() / channels != expectedFrames))
    {
        return Core::failure(AudioErrorCode::DecodeFailed, "Audio stream is empty or has an incomplete PCM frame count");
    }
    // Compare produced frames, not the backend cursor: MP3 cursors include
    // encoder delay even though the reported length and emitted PCM exclude it.
    return pcm;
}

} // namespace

AudioDecodeCapabilities queryAudioDecodeCapabilities() noexcept { return {}; }

bool isSupportedAudioSourceExtension(std::string_view extension) noexcept
{
    return std::any_of(AudioSourceExtensions.begin(), AudioSourceExtensions.end(),
        [extension](std::string_view supported) {
            return extension.size() == supported.size() &&
                   std::equal(extension.begin(), extension.end(), supported.begin(), [](char input, char expected) {
                       const auto lower = input >= 'A' && input <= 'Z' ? static_cast<char>(input + ('a' - 'A')) : input;
                       return lower == expected;
                   });
        });
}

Core::Result<DecodedPcmBuffer> decodeAudioMemory(std::span<const std::byte> encoded,
                                                AudioDecodeConfig config) noexcept
try
{
    if (encoded.empty() || encoded.data() == nullptr || config.maxEncodedBytes == 0 ||
        config.maxDecodedBytes < sizeof(float) || config.outputChannels > AudioPcmStreamMaxChannels ||
        (config.outputSampleRate != 0 &&
         (config.outputSampleRate < MinimumSampleRate || config.outputSampleRate > MaximumSampleRate)))
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "Audio decode input, limits or output format are invalid");
    }
    if (encoded.size() > config.maxEncodedBytes)
    {
        return Core::failure(AudioErrorCode::DecodeLimitExceeded, "Encoded audio exceeds the byte budget");
    }

    ma_decoder_config backendConfig = ma_decoder_config_init(ma_format_f32, config.outputChannels,
                                                             config.outputSampleRate);
    ma_decoding_backend_vtable* customBackend = nullptr;
    if (auto selected = selectDecoder(encoded, backendConfig, customBackend); !selected)
    {
        return Core::failure(std::move(selected.error()));
    }
    DecoderOwner owner;
    auto result = ma_decoder_init_memory(encoded.data(), encoded.size(), &backendConfig, &owner.decoder);
    if (result != MA_SUCCESS) { return Core::failure(decoderError(result, "openMemory")); }
    owner.initialized = true;
    if (owner.decoder.outputChannels > AudioPcmStreamMaxChannels)
    {
        // Reopen using miniaudio's mapped downmix rather than dropping surround channels.
        owner.reset();
        backendConfig.channels = AudioPcmStreamMaxChannels;
        result = ma_decoder_init_memory(encoded.data(), encoded.size(), &backendConfig, &owner.decoder);
        if (result != MA_SUCCESS) { return Core::failure(decoderError(result, "downmix")); }
        owner.initialized = true;
    }
    const auto channels = owner.decoder.outputChannels;
    const auto sampleRate = owner.decoder.outputSampleRate;
    if (channels == 0 || channels > AudioPcmStreamMaxChannels ||
        sampleRate < MinimumSampleRate || sampleRate > MaximumSampleRate)
    {
        return Core::failure(AudioErrorCode::NotSupported, "Audio channel count or sample rate is outside the supported range");
    }
    auto pcm = readPcm(owner.decoder, config.maxDecodedBytes);
    if (!pcm) { return Core::failure(std::move(pcm.error())); }
    return DecodedPcmBuffer{std::move(*pcm), channels, sampleRate};
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Audio decoder allocation failed");
}
catch (const std::length_error&)
{
    return Core::failure(AudioErrorCode::DecodeLimitExceeded, "Decoded audio cannot fit in addressable memory");
}
catch (...)
{
    return Core::failure(AudioErrorCode::DecodeFailed, "Unexpected audio decode failure");
}

} // namespace Tina::Audio
