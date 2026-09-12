#include <tina/audio/AudioDecode.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/audio/AudioEngine.hpp>
#include <tina/audio/AudioErrors.hpp>

#include "support/AudioFixtures.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

namespace Tina::Audio {
namespace {

static_assert(!std::is_copy_constructible_v<DecodedPcmBuffer>);
static_assert(std::is_nothrow_move_constructible_v<DecodedPcmBuffer>);
static_assert(std::is_nothrow_move_assignable_v<DecodedPcmBuffer>);

class AudioSourceDecodeTest : public testing::TestWithParam<const char*> {};
INSTANTIATE_TEST_SUITE_P(AllFormats, AudioSourceDecodeTest, testing::ValuesIn(Tests::AudioFixtureNames));

TEST(AudioDecodeTest, AllCodecsAreBaseCapabilitiesAndExtensionsAreCaseInsensitive)
{
    const auto caps = queryAudioDecodeCapabilities();
    EXPECT_TRUE(caps.wav && caps.flac && caps.mp3 && caps.oggVorbis && caps.opus);
    for (const auto extension : AudioSourceExtensions) {
        EXPECT_TRUE(isSupportedAudioSourceExtension(extension));
        std::string upper{extension};
        for (auto& character : upper) {
            if (character >= 'a' && character <= 'z') { character -= 'a' - 'A'; }
        }
        EXPECT_TRUE(isSupportedAudioSourceExtension(upper));
    }
    EXPECT_FALSE(isSupportedAudioSourceExtension(""));
    EXPECT_FALSE(isSupportedAudioSourceExtension(".aac"));
    EXPECT_FALSE(isSupportedAudioSourceExtension("music.ogg"));
    EXPECT_FALSE(isSupportedAudioSourceExtension(".ogg.exe"));
}

TEST_P(AudioSourceDecodeTest, DecodesRealSourceToOwnedFiniteNonSilentPcm)
{
    auto decoded = decodeAudioMemory(Tests::readAudioFixture(GetParam()));
    ASSERT_TRUE(decoded) << decoded.error().message;
    EXPECT_EQ(decoded->channels(), 2U);
    EXPECT_EQ(decoded->sampleRate(), 48000U);
    EXPECT_EQ(decoded->frameCount(), 4800U);
    EXPECT_EQ(decoded->interleavedPcm().size(), decoded->frameCount() * 2);
    EXPECT_TRUE(std::all_of(decoded->interleavedPcm().begin(), decoded->interleavedPcm().end(),
                           [](float sample) { return std::isfinite(sample) && std::abs(sample) < 0.5F; }));
    EXPECT_TRUE(std::any_of(decoded->interleavedPcm().begin(), decoded->interleavedPcm().end(),
                           [](float sample) { return std::abs(sample) > 0.1F; }));
    const auto* original = decoded->interleavedPcm().data();
    auto moved = std::move(*decoded);
    EXPECT_TRUE(decoded->empty());
    EXPECT_EQ(moved.clipView().frames, original);
    DecodedPcmBuffer reassigned;
    reassigned = std::move(moved);
    EXPECT_TRUE(moved.empty());
    EXPECT_EQ(reassigned.frameCount(), 4800U);
    EXPECT_EQ(reassigned.clipView().frames, original);
}

TEST_P(AudioSourceDecodeTest, EnforcesInputAndDecodedByteBudgets)
{
    const auto source = Tests::readAudioFixture(GetParam());
    auto inputLimit = decodeAudioMemory(source, {.maxEncodedBytes = source.size() - 1});
    ASSERT_FALSE(inputLimit);
    EXPECT_EQ(inputLimit.error().code, AudioErrorCode::DecodeLimitExceeded);
    auto outputLimit = decodeAudioMemory(source, {.maxDecodedBytes = 32});
    ASSERT_FALSE(outputLimit);
    EXPECT_EQ(outputLimit.error().code, AudioErrorCode::DecodeLimitExceeded);
    auto exactLimit = decodeAudioMemory(source, {.maxDecodedBytes = 4800 * 2 * sizeof(float)});
    ASSERT_TRUE(exactLimit) << exactLimit.error().message;
}

TEST_P(AudioSourceDecodeTest, ConvertsToMonoAndResamples)
{
    const auto source = Tests::readAudioFixture(GetParam());
    for (const Core::u32 rate : {1000U, 8000U, 22050U, 24000U, 44100U, 48000U, 96000U, 192000U}) {
        SCOPED_TRACE(rate);
        const Core::u64 expectedFrames = rate / 10; // Every source fixture is 100 ms.
        auto decoded = decodeAudioMemory(source, {.maxDecodedBytes = expectedFrames * sizeof(float),
                                                  .outputChannels = 1, .outputSampleRate = rate});
        ASSERT_TRUE(decoded) << decoded.error().message;
        EXPECT_EQ(decoded->channels(), 1U);
        EXPECT_EQ(decoded->sampleRate(), rate);
        EXPECT_EQ(decoded->frameCount(), expectedFrames);
    }
}

TEST_P(AudioSourceDecodeTest, MusicBusAndTerminalCompletionRespectPcmLifetime)
{
    auto decoded = decodeAudioMemory(Tests::readAudioFixture(GetParam()));
    ASSERT_TRUE(decoded) << decoded.error().message;
    auto engine = AudioEngine::Create({.voiceCapacity = 2, .commandCapacity = 8, .completionCapacity = 8});
    ASSERT_TRUE(engine);
    auto voice = engine->playOneShotPcm(decoded->clipView(), AudioBusId::Music);
    ASSERT_TRUE(voice) << voice.error().message;
    ASSERT_TRUE(engine->pumpCompletions(8));
    std::vector<float> output(1024 * 2);
    engine->mixRealtime(output.data(), 1024, 2, 48000);
    EXPECT_TRUE(std::any_of(output.begin(), output.end(), [](float x) { return x != 0; }));
    ASSERT_TRUE(engine->setBusMuted(AudioBusId::Music, true));
    engine->mixRealtime(output.data(), 1024, 2, 48000);
    EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](float x) { return x == 0; }));
    ASSERT_TRUE(engine->enqueueStop(*voice));
    std::array<AudioCompletionEvent, 8> events{};
    auto count = engine->pumpCompletions(events, 8);
    ASSERT_TRUE(count);
    EXPECT_EQ(std::count_if(events.begin(), events.begin() + *count,
        [](const auto& event) { return event.kind == AudioCompletionKind::Stopped; }), 1);
    const auto stats = engine->stats();
    ASSERT_TRUE(stats);
    EXPECT_EQ(stats->liveVoices, 0U);
    // PCM owner is destroyed only after terminal completion has been consumed.
}

TEST_P(AudioSourceDecodeTest, DecodedPcmStreamCopiesChunksAndDrainsEof)
{
    auto decoded = decodeAudioMemory(Tests::readAudioFixture(GetParam()));
    ASSERT_TRUE(decoded) << decoded.error().message;
    auto engine = AudioEngine::Create({.voiceCapacity = 1, .commandCapacity = 8, .completionCapacity = 8,
                                       .streamBufferFrameCapacity = 4800});
    ASSERT_TRUE(engine);
    auto stream = engine->playPcmStream({.channels = 2, .sampleRate = 48000, .bufferCapacityFrames = 4800},
                                        AudioBusId::Music);
    ASSERT_TRUE(stream);
    const auto view = decoded->clipView();
    ASSERT_TRUE(engine->submitPcmStreamFrames(*stream, {view.frames, view.frameCount}));
    ASSERT_TRUE(engine->signalPcmStreamEof(*stream));
    *decoded = DecodedPcmBuffer{}; // submit copied every frame; the producer owner may now go away.
    ASSERT_TRUE(engine->pumpCompletions(8));
    std::vector<float> output(5000 * 2);
    engine->mixRealtime(output.data(), 5000, 2, 48000);
    EXPECT_TRUE(std::any_of(output.begin(), output.begin() + 4800 * 2, [](float x) { return x != 0; }));
    EXPECT_TRUE(std::all_of(output.begin() + 4800 * 2, output.end(), [](float x) { return x == 0; }));
    std::array<AudioCompletionEvent, 8> events{};
    auto count = engine->pumpCompletions(events, 8);
    ASSERT_TRUE(count);
    ASSERT_EQ(*count, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);
    EXPECT_EQ(engine->stats()->liveVoices, 0U);
}

TEST(AudioDecodeTest, IntegerAndFloatWavFormatsShareNormalizedPcm)
{
    constexpr std::array samples{0.0F, 0.5F, -0.5F, 0.25F};
    for (auto bits : {8, 16, 24, 32, 64}) {
        for (bool floatingPoint : {false, true}) {
            if ((floatingPoint && bits < 32) || (!floatingPoint && bits == 64)) { continue; }
            SCOPED_TRACE(std::to_string(bits) + (floatingPoint ? " float" : " integer"));
            auto decoded = decodeAudioMemory(Tests::makePcmWav(samples, 1, static_cast<Core::u16>(bits), floatingPoint));
            ASSERT_TRUE(decoded) << decoded.error().message;
            ASSERT_EQ(decoded->frameCount(), samples.size());
            // Unsigned 8-bit PCM has only 256 levels; its normalized [-1, 1]
            // quantization step cannot meet the precision of 16-bit/float PCM.
            const float tolerance = bits == 8 ? 2.0F / 255.0F : 1.0e-5F;
            for (Tina::Core::usize index = 0; index < samples.size(); ++index) {
                EXPECT_NEAR(decoded->interleavedPcm()[index], samples[index], tolerance);
            }
        }
    }
}

TEST(AudioDecodeTest, SurroundDownmixIncludesCenterChannel)
{
    constexpr std::array surround{0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 0.0F,
                                0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 0.0F};
    auto decoded = decodeAudioMemory(Tests::makePcmWav(surround, 6));
    ASSERT_TRUE(decoded) << decoded.error().message;
    EXPECT_EQ(decoded->channels(), 2U);
    EXPECT_EQ(decoded->frameCount(), 2U);
    EXPECT_GT(decoded->interleavedPcm()[0], 0.0F);
    EXPECT_GT(decoded->interleavedPcm()[1], 0.0F);
}

TEST(AudioDecodeTest, InvalidInputConfigurationAndNonFinitePcmFailClosed)
{
    auto empty = decodeAudioMemory({});
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error().code, AudioErrorCode::InvalidConfiguration);
    const auto wav = Tests::makePcmWav(std::array{0.25F, -0.25F});
    EXPECT_FALSE(decodeAudioMemory(wav, {.maxEncodedBytes = 0}));
    EXPECT_FALSE(decodeAudioMemory(wav, {.maxDecodedBytes = 0}));
    EXPECT_FALSE(decodeAudioMemory(wav, {.outputChannels = 3}));
    EXPECT_FALSE(decodeAudioMemory(wav, {.outputSampleRate = 999}));
    EXPECT_FALSE(decodeAudioMemory(wav, {.outputSampleRate = 192001}));
    auto nan = decodeAudioMemory(Tests::makePcmWav(std::array{std::numeric_limits<float>::quiet_NaN()}, 1, 32, true));
    ASSERT_FALSE(nan);
    EXPECT_EQ(nan.error().code, AudioErrorCode::DecodeFailed);
    auto truncated = wav;
    truncated.pop_back();
    EXPECT_FALSE(decodeAudioMemory(truncated));
    const std::array junk{std::byte{0}, std::byte{1}, std::byte{2}};
    auto unknown = decodeAudioMemory(junk);
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code, AudioErrorCode::NotSupported);
}

TEST(AudioDecodeTest, RejectsTruncatedCorruptAndChainedOggWithoutPartialPcm)
{
    for (const auto* name : {"tone-vorbis.ogg", "tone-opus.opus"}) {
        SCOPED_TRACE(name);
        const auto original = Tests::readAudioFixture(name);
        for (const auto count : {Tina::Core::usize{4}, Tina::Core::usize{27}, original.size() / 2, original.size() - 1}) {
            auto decoded = decodeAudioMemory(std::span{original}.first(count));
            ASSERT_FALSE(decoded);
            EXPECT_EQ(decoded.error().code, AudioErrorCode::DecodeFailed);
        }
        auto corrupt = original;
        corrupt.back() ^= std::byte{1};
        EXPECT_FALSE(decodeAudioMemory(corrupt));
        auto chained = original;
        chained.insert(chained.end(), original.begin(), original.end());
        auto rejectedChain = decodeAudioMemory(chained);
        ASSERT_FALSE(rejectedChain);
        EXPECT_EQ(rejectedChain.error().code, AudioErrorCode::NotSupported);
    }
}

} // namespace
} // namespace Tina::Audio
