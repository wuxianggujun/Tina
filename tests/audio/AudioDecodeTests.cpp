#include <tina/audio/AudioDecode.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/audio/AudioEngine.hpp>
#include <tina/audio/AudioErrors.hpp>

#include "support/AudioFixtures.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

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

TEST_P(AudioSourceDecodeTest, IncrementalDecoderMatchesWholeFilePcmAndSeek)
{
    const auto source = Tests::readAudioFixture(GetParam());
    auto whole = decodeAudioMemory(source);
    ASSERT_TRUE(whole) << whole.error().message;
    auto decoder = AudioDecoder::open(source);
    ASSERT_TRUE(decoder) << decoder.error().message;
    EXPECT_EQ(decoder->channels(), whole->channels());
    EXPECT_EQ(decoder->sampleRate(), whole->sampleRate());
    std::vector<float> incremental;
    incremental.resize(static_cast<Core::usize>(whole->frameCount() * whole->channels()));
    Core::u64 cursor = 0;
    std::array<float, 256 * 2> block{};
    for (;;)
    {
        auto frames = decoder->readPcm(std::span<float>{block}.first(256U * decoder->channels()));
        ASSERT_TRUE(frames) << frames.error().message;
        if (*frames == 0) { break; }
        const auto samples = static_cast<Core::usize>(*frames) * decoder->channels();
        std::copy(block.begin(), block.begin() + static_cast<std::ptrdiff_t>(samples),
                  incremental.begin() + static_cast<std::ptrdiff_t>(cursor * decoder->channels()));
        cursor += *frames;
    }
    EXPECT_EQ(cursor, whole->frameCount());
    ASSERT_EQ(incremental.size(), whole->interleavedPcm().size());
    for (Core::usize index = 0; index < incremental.size(); ++index)
    {
        EXPECT_EQ(incremental[index], whole->interleavedPcm()[index]);
    }
}

TEST(AudioDecodeTest, WavSeekIsSampleExact)
{
    const auto source = Tests::readAudioFixture("tone.wav");
    auto whole = decodeAudioMemory(source);
    ASSERT_TRUE(whole);
    auto decoder = AudioDecoder::open(source);
    ASSERT_TRUE(decoder);
    ASSERT_TRUE(decoder->seekFrame(0));
    std::array<float, 2> first{};
    auto frames = decoder->readPcm(first);
    ASSERT_TRUE(frames);
    ASSERT_EQ(*frames, 1U);
    EXPECT_EQ(first[0], whole->interleavedPcm()[0]);
    EXPECT_EQ(first[1], whole->interleavedPcm()[1]);
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
