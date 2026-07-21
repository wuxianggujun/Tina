#include <tina/audio/AudioDecode.hpp>

#include <tina/audio/AudioEngine.hpp>
#include <tina/audio/AudioErrors.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace Tina::Audio {
namespace {

// Minimal mono 16-bit PCM WAV: 8 samples @ 8000 Hz (built-in decoder, no optional codecs).
[[nodiscard]] std::vector<std::byte> makeTinyPcm16Wav()
{
    constexpr std::uint32_t sampleRate = 8000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bitsPerSample = 16;
    constexpr std::uint32_t dataBytes = 16; // 8 samples * 2 bytes
    constexpr std::uint32_t fmtChunkSize = 16;
    constexpr std::uint32_t riffSize = 4 + (8 + fmtChunkSize) + (8 + dataBytes);

    std::vector<std::byte> bytes;
    bytes.reserve(44 + dataBytes);
    const auto append = [&](const void* data, std::size_t size) {
        const auto* begin = static_cast<const std::byte*>(data);
        bytes.insert(bytes.end(), begin, begin + size);
    };
    const auto appendU16 = [&](std::uint16_t value) {
        const std::uint8_t le[2] = {
            static_cast<std::uint8_t>(value & 0xFF),
            static_cast<std::uint8_t>((value >> 8) & 0xFF),
        };
        append(le, 2);
    };
    const auto appendU32 = [&](std::uint32_t value) {
        const std::uint8_t le[4] = {
            static_cast<std::uint8_t>(value & 0xFF),
            static_cast<std::uint8_t>((value >> 8) & 0xFF),
            static_cast<std::uint8_t>((value >> 16) & 0xFF),
            static_cast<std::uint8_t>((value >> 24) & 0xFF),
        };
        append(le, 4);
    };

    append("RIFF", 4);
    appendU32(riffSize);
    append("WAVE", 4);
    append("fmt ", 4);
    appendU32(fmtChunkSize);
    appendU16(1); // PCM
    appendU16(channels);
    appendU32(sampleRate);
    appendU32(sampleRate * channels * (bitsPerSample / 8));
    appendU16(static_cast<std::uint16_t>(channels * (bitsPerSample / 8)));
    appendU16(bitsPerSample);
    append("data", 4);
    appendU32(dataBytes);
    const std::array<std::int16_t, 8> samples{0, 1000, 2000, 1000, 0, -1000, -2000, -1000};
    for (const std::int16_t sample : samples)
    {
        appendU16(static_cast<std::uint16_t>(sample));
    }
    return bytes;
}

TEST(MiniaudioDecodeTest, CapabilitiesReportBuiltInAndOptionalCodecs)
{
    const AudioDecodeCapabilities caps = queryAudioDecodeCapabilities();
    EXPECT_TRUE(caps.wav);
    EXPECT_TRUE(caps.flac);
    EXPECT_TRUE(caps.mp3);
    // Optional codecs follow CMake TINA_AUDIO_ENABLE_LIBVORBIS / LIBOPUS.
    // Default audio-miniaudio preset keeps both false.
    if (caps.oggVorbis)
    {
        EXPECT_TRUE(caps.oggVorbis);
    }
    if (caps.opus)
    {
        EXPECT_TRUE(caps.opus);
    }
}

TEST(MiniaudioDecodeTest, DecodesTinyWavToFloatPcm)
{
    const std::vector<std::byte> wav = makeTinyPcm16Wav();
    auto decoded = decodeAudioMemory(std::span<const std::byte>(wav.data(), wav.size()));
    ASSERT_TRUE(decoded.has_value()) << (decoded ? "" : decoded.error().message);
    EXPECT_EQ(decoded->channels, 1U);
    EXPECT_EQ(decoded->sampleRate, 8000U);
    EXPECT_EQ(decoded->frameCount, 8U);
    ASSERT_NE(decoded->frames, nullptr);
    freeDecodedPcm(*decoded);
    EXPECT_TRUE(decoded->empty());
}

TEST(MiniaudioDecodeTest, EmptyPayloadIsInvalid)
{
    auto decoded = decodeAudioMemory({});
    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, AudioErrorCode::InvalidConfiguration);
}

TEST(MiniaudioDecodeTest, OggMagicWithoutOptionalCodecsIsClearWhenDisabled)
{
    const AudioDecodeCapabilities caps = queryAudioDecodeCapabilities();
    if (caps.oggVorbis || caps.opus)
    {
        GTEST_SKIP() << "optional Ogg codecs enabled in this build";
    }
    // "OggS" + junk: without vorbis/opus backends, should map to CodecNotEnabled.
    const std::array<std::byte, 8> oggStub{
        std::byte{'O'},
        std::byte{'g'},
        std::byte{'g'},
        std::byte{'S'},
        std::byte{0},
        std::byte{0},
        std::byte{0},
        std::byte{0},
    };
    auto decoded = decodeAudioMemory(std::span<const std::byte>(oggStub.data(), oggStub.size()));
    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, AudioErrorCode::CodecNotEnabled);
}

// M11-A13: encoded bytes -> decode -> playOneShotPcm -> mixRealtime end-to-end.
TEST(MiniaudioDecodeTest, DecodeWavThenPlayOneShotAndMix)
{
    const std::vector<std::byte> wav = makeTinyPcm16Wav();
    auto decoded = decodeAudioMemory(std::span<const std::byte>(wav.data(), wav.size()));
    ASSERT_TRUE(decoded.has_value()) << (decoded ? "" : decoded.error().message);
    ASSERT_FALSE(decoded->empty());

    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 2,
        .commandCapacity = 8,
        .completionCapacity = 8,
    });
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);

    auto voice = engine->playOneShotPcm(AudioPcmClipView{
        .frames = decoded->frames,
        .frameCount = decoded->frameCount,
        .channels = decoded->channels,
        .sampleRate = decoded->sampleRate,
    });
    ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);

    AudioCompletionEvent events[2]{};
    auto pumped = engine->pumpCompletions(std::span<AudioCompletionEvent>{events}, 0);
    ASSERT_TRUE(pumped.has_value());
    ASSERT_EQ(*pumped, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Started);

    float out[16]{};
    engine->mixRealtime(out, 4, 1, decoded->sampleRate);
    // Tiny WAV has non-zero samples; at least one output sample should be non-zero.
    bool anyNonZero = false;
    for (float sample : out)
    {
        if (sample != 0.0F)
        {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);

    freeDecodedPcm(*decoded);
}

} // namespace
} // namespace Tina::Audio
