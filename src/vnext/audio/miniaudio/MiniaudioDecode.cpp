#include <tina/audio/AudioDecode.hpp>

#include <tina/audio/AudioErrors.hpp>

#include <miniaudio.h>

#include <cstring>
#include <new>

namespace Tina::Audio {

AudioDecodeCapabilities queryAudioDecodeCapabilities() noexcept
{
    AudioDecodeCapabilities caps{};
#if defined(TINA_AUDIO_HAS_LIBVORBIS)
    caps.oggVorbis = true;
#endif
#if defined(TINA_AUDIO_HAS_LIBOPUS)
    caps.opus = true;
#endif
    return caps;
}

Core::Result<DecodedPcmBuffer> decodeAudioMemory(std::span<const std::byte> encoded) noexcept
{
    if (encoded.empty() || encoded.data() == nullptr)
    {
        return Core::failure(AudioErrorCode::InvalidConfiguration, "decodeAudioMemory requires non-empty bytes");
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_uint64 frameCount = 0;
    void* frames = nullptr;
    const ma_result result = ma_decode_memory(encoded.data(), encoded.size(), &config, &frameCount, &frames);
    if (result != MA_SUCCESS || frames == nullptr || frameCount == 0)
    {
        if (frames != nullptr)
        {
            ma_free(frames, nullptr);
        }
        // miniaudio returns format-not-supported / invalid-data for missing codecs
        // and corrupt payloads alike; map optional-codec gaps first when obvious.
#if !defined(TINA_AUDIO_HAS_LIBVORBIS) || !defined(TINA_AUDIO_HAS_LIBOPUS)
        // Heuristic: "OggS" magic with optional codecs disabled → clearer error.
        if (encoded.size() >= 4 &&
            std::memcmp(encoded.data(), "OggS", 4) == 0)
        {
#if !defined(TINA_AUDIO_HAS_LIBVORBIS) && !defined(TINA_AUDIO_HAS_LIBOPUS)
            return Core::failure(
                AudioErrorCode::CodecNotEnabled,
                "Ogg container requires TINA_AUDIO_ENABLE_LIBVORBIS and/or "
                "TINA_AUDIO_ENABLE_LIBOPUS (vcpkg features audio-miniaudio-vorbis / "
                "audio-miniaudio-opus)");
#elif !defined(TINA_AUDIO_HAS_LIBVORBIS)
            return Core::failure(
                AudioErrorCode::CodecNotEnabled,
                "Ogg Vorbis requires TINA_AUDIO_ENABLE_LIBVORBIS "
                "(vcpkg feature audio-miniaudio-vorbis)");
#else
            return Core::failure(
                AudioErrorCode::CodecNotEnabled,
                "Ogg Opus requires TINA_AUDIO_ENABLE_LIBOPUS "
                "(vcpkg feature audio-miniaudio-opus)");
#endif
        }
#endif
        return Core::failure(AudioErrorCode::DecodeFailed, "ma_decode_memory failed for the given payload");
    }

    return DecodedPcmBuffer{
        .frames = static_cast<float*>(frames),
        .frameCount = static_cast<Core::u64>(frameCount),
        .channels = config.channels,
        .sampleRate = config.sampleRate,
    };
}

void freeDecodedPcm(DecodedPcmBuffer& buffer) noexcept
{
    if (buffer.frames != nullptr)
    {
        ma_free(buffer.frames, nullptr);
        buffer.frames = nullptr;
    }
    buffer.frameCount = 0;
    buffer.channels = 0;
    buffer.sampleRate = 0;
}

} // namespace Tina::Audio
