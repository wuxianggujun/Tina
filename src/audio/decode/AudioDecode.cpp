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
#include <memory>
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
    error.addContext("AudioDecoder", operation);
    return error;
}

[[nodiscard]] Core::Result<AudioSourceCodec> selectDecoder(std::span<const std::byte> encoded,
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
        return *codec == Detail::OggAudioCodec::Vorbis ? AudioSourceCodec::Vorbis
                                                       : AudioSourceCodec::Opus;
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
        return AudioSourceCodec::Wav;
    }
    if (encoded.size() >= 4 && std::memcmp(encoded.data(), "fLaC", 4) == 0)
    {
        config.encodingFormat = ma_encoding_format_flac;
        return AudioSourceCodec::Flac;
    }
    const bool id3 = encoded.size() >= 3 && std::memcmp(encoded.data(), "ID3", 3) == 0;
    const bool mpeg = encoded.size() >= 2 && encoded[0] == std::byte{0xff} &&
                      (std::to_integer<Core::u8>(encoded[1]) & 0xe0U) == 0xe0U;
    if (id3 || mpeg)
    {
        config.encodingFormat = ma_encoding_format_mp3;
        return AudioSourceCodec::Mp3;
    }
    return Core::failure(AudioErrorCode::NotSupported,
                         "Audio source must contain WAV, FLAC, MP3, Ogg Vorbis or Ogg Opus");
}

struct PagedEncodedSource final {
    std::span<const std::byte> encoded{};
    Core::u64 byteCursor = 0;
};

[[nodiscard]] Core::Status openDecoder(PagedEncodedSource& source, AudioDecodeConfig config,
                                       DecoderOwner& owner, ma_decoding_backend_vtable*& customBackend,
                                       AudioSourceCodec& codec) noexcept
{
    if (source.encoded.empty() || source.encoded.data() == nullptr || config.maxEncodedBytes == 0 ||
        config.maxDecodedBytes < sizeof(float) || config.outputChannels > AudioPcmStreamMaxChannels ||
        (config.outputSampleRate != 0 &&
         (config.outputSampleRate < MinimumSampleRate || config.outputSampleRate > MaximumSampleRate)))
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration,
                             "Audio decode input, limits or output format are invalid");
    }
    if (source.encoded.size() > config.maxEncodedBytes)
    {
        return Core::failure(AudioErrorCode::DecodeLimitExceeded, "Encoded audio exceeds the byte budget");
    }

    ma_decoder_config backendConfig = ma_decoder_config_init(ma_format_f32, config.outputChannels,
                                                             config.outputSampleRate);
    auto selected = selectDecoder(source.encoded, backendConfig, customBackend);
    if (!selected) { return Core::failure(std::move(selected.error())); }
    codec = *selected;
    source.byteCursor = 0;
    // Custom Vorbis/Opus backends require a memory data source; the span is a
    // catalog mmap window, not a heap copy. Sequential decode faults pages.
    auto result = ma_decoder_init_memory(source.encoded.data(), source.encoded.size(),
                                         &backendConfig, &owner.decoder);
    if (result != MA_SUCCESS) { return Core::failure(decoderError(result, "openMemory")); }
    owner.initialized = true;
    if (owner.decoder.outputChannels > AudioPcmStreamMaxChannels)
    {
        owner.reset();
        source.byteCursor = 0;
        backendConfig.channels = AudioPcmStreamMaxChannels;
        result = ma_decoder_init_memory(source.encoded.data(), source.encoded.size(),
                                        &backendConfig, &owner.decoder);
        if (result != MA_SUCCESS) { return Core::failure(decoderError(result, "downmix")); }
        owner.initialized = true;
    }
    const auto channels = owner.decoder.outputChannels;
    const auto sampleRate = owner.decoder.outputSampleRate;
    if (channels == 0 || channels > AudioPcmStreamMaxChannels ||
        sampleRate < MinimumSampleRate || sampleRate > MaximumSampleRate)
    {
        return Core::failure(AudioErrorCode::NotSupported,
                             "Audio channel count or sample rate is outside the supported range");
    }
    return Core::success();
}

[[nodiscard]] Core::u64 queriedFrameCount(ma_decoder& decoder) noexcept
{
    ma_uint64 sourceFrames = 0;
    const auto lengthResult = ma_data_source_get_length_in_pcm_frames(decoder.pBackend, &sourceFrames);
    if (lengthResult != MA_SUCCESS || sourceFrames == 0) { return 0; }
    ma_uint64 expectedFrames = 0;
    const auto convertedLength = ma_data_converter_get_expected_output_frame_count(
        &decoder.converter, sourceFrames, &expectedFrames);
    if (convertedLength != MA_SUCCESS && convertedLength != MA_NOT_IMPLEMENTED) { return 0; }
    return convertedLength == MA_SUCCESS ? expectedFrames : sourceFrames;
}

} // namespace

struct AudioDecoder::Impl final {
    PagedEncodedSource source{};
    DecoderOwner owner{};
    ma_decoding_backend_vtable* customBackend = nullptr;
    AudioDecodeConfig config{};
    AudioSourceCodec codec = AudioSourceCodec::Wav;
    Core::u32 channels = 0;
    Core::u32 sampleRate = 0;
    Core::u64 reportedFrameCount = 0;
    Core::u64 cursor = 0;
};

AudioDecoder::AudioDecoder() noexcept = default;
AudioDecoder::AudioDecoder(AudioDecoder&& other) noexcept = default;
AudioDecoder& AudioDecoder::operator=(AudioDecoder&& other) noexcept = default;
AudioDecoder::~AudioDecoder() = default;

Core::u32 AudioDecoder::channels() const noexcept
{
    return m_impl ? m_impl->channels : 0;
}
Core::u32 AudioDecoder::sampleRate() const noexcept
{
    return m_impl ? m_impl->sampleRate : 0;
}
Core::u64 AudioDecoder::frameCount() const noexcept
{
    return m_impl ? m_impl->reportedFrameCount : 0;
}
AudioSourceCodec AudioDecoder::codec() const noexcept
{
    return m_impl ? m_impl->codec : AudioSourceCodec::Wav;
}
Core::u64 AudioDecoder::cursorFrame() const noexcept
{
    return m_impl ? m_impl->cursor : 0;
}

Core::Result<AudioDecoder> AudioDecoder::open(std::span<const std::byte> encoded,
                                              AudioDecodeConfig config) noexcept
try
{
    auto impl = std::make_unique<Impl>();
    impl->config = config;
    impl->source.encoded = encoded;
    if (auto status = openDecoder(impl->source, config, impl->owner, impl->customBackend, impl->codec); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    impl->channels = impl->owner.decoder.outputChannels;
    impl->sampleRate = impl->owner.decoder.outputSampleRate;
    impl->reportedFrameCount = queriedFrameCount(impl->owner.decoder);
    AudioDecoder decoder;
    decoder.m_impl = std::move(impl);
    return decoder;
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Audio decoder allocation failed");
}

Core::Result<Core::u64> AudioDecoder::readPcm(std::span<float> interleavedOut) noexcept
{
    if (m_impl == nullptr || !m_impl->owner.initialized)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "AudioDecoder is not open");
    }
    const Core::usize channels = m_impl->channels;
    if (channels == 0 || interleavedOut.size() % channels != 0)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration,
                             "AudioDecoder output span must be a multiple of the channel count");
    }
    const auto maxFrames = static_cast<ma_uint64>(interleavedOut.size() / channels);
    if (maxFrames == 0) { return Core::u64{0}; }
    ma_uint64 framesRead = 0;
    const auto result = ma_decoder_read_pcm_frames(&m_impl->owner.decoder, interleavedOut.data(),
                                                   maxFrames, &framesRead);
    if ((result != MA_SUCCESS && result != MA_AT_END) || framesRead > maxFrames)
    {
        return Core::failure(decoderError(result, "readPcm"));
    }
    const auto samples = std::span<const float>{interleavedOut.data(),
                                                static_cast<Core::usize>(framesRead) * channels};
    if (!std::all_of(samples.begin(), samples.end(), [](float sample) { return std::isfinite(sample); }))
    {
        return Core::failure(AudioErrorCode::DecodeFailed, "Decoded PCM contains a non-finite sample");
    }
    m_impl->cursor += framesRead;
    return static_cast<Core::u64>(framesRead);
}

Core::Status AudioDecoder::seekFrame(Core::u64 frame) noexcept
{
    if (m_impl == nullptr || !m_impl->owner.initialized)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "AudioDecoder is not open");
    }
    const auto result = ma_decoder_seek_to_pcm_frame(&m_impl->owner.decoder, frame);
    if (result != MA_SUCCESS)
    {
        return Core::failure(decoderError(result, "seekFrame"));
    }
    m_impl->cursor = frame;
    return Core::success();
}

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
    auto decoder = AudioDecoder::open(encoded, config);
    if (!decoder) { return Core::failure(std::move(decoder.error())); }
    const Core::usize channels = decoder->channels();
    const auto maxSamples = static_cast<Core::usize>((std::min)(
        config.maxDecodedBytes / sizeof(float),
        static_cast<Core::u64>((std::numeric_limits<Core::usize>::max)() / sizeof(float))));
    const Core::u64 maxFrames = channels == 0 ? 0 : maxSamples / channels;
    const Core::u64 expectedFrames = decoder->frameCount();
    if (expectedFrames > maxFrames || maxFrames == 0)
    {
        return Core::failure(AudioErrorCode::DecodeLimitExceeded, "Decoded PCM exceeds the byte budget");
    }

    std::vector<float> pcm;
    if (expectedFrames != 0) { pcm.reserve(static_cast<Core::usize>(expectedFrames) * channels); }
    std::array<float, DecodeBlockFrames * AudioPcmStreamMaxChannels> block{};
    for (;;)
    {
        const auto frames = decoder->readPcm(std::span<float>{block}.first(DecodeBlockFrames * channels));
        if (!frames) { return Core::failure(std::move(frames.error())); }
        if (*frames == 0) { break; }
        const Core::usize samplesRead = static_cast<Core::usize>(*frames) * channels;
        if (samplesRead > maxSamples - pcm.size())
        {
            return Core::failure(AudioErrorCode::DecodeLimitExceeded, "Decoded PCM exceeds the byte budget");
        }
        const auto required = pcm.size() + samplesRead;
        if (required > pcm.capacity())
        {
            const auto growth = pcm.capacity() > maxSamples / 2 ? maxSamples : pcm.capacity() * 2;
            pcm.reserve((std::max)(required, growth));
        }
        pcm.insert(pcm.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(samplesRead));
        if (*frames < DecodeBlockFrames) { break; }
    }
    if (pcm.empty() || (expectedFrames != 0 && pcm.size() / channels != expectedFrames))
    {
        return Core::failure(AudioErrorCode::DecodeFailed, "Audio stream is empty or has an incomplete PCM frame count");
    }
    return DecodedPcmBuffer{std::move(pcm), decoder->channels(), decoder->sampleRate()};
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
