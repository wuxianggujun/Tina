#include <tina/audio/AudioDecode.hpp>
#include <tina/audio/AudioEngine.hpp>
#include <tina/audio/AudioErrors.hpp>
#include <tina/audio/EncodedPcmStreamer.hpp>

#include "support/AudioFixtures.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

namespace Tina::Audio {
namespace {

TEST(EncodedPcmStreamerTests, VorbisStreamMatchesFullDecodeFrameCount)
{
    const auto encoded = Tests::readAudioFixture("tone-vorbis.ogg");
    auto whole = decodeAudioMemory(encoded);
    ASSERT_TRUE(whole) << whole.error().message;
    auto engine = AudioEngine::Create({.voiceCapacity = 1, .commandCapacity = 8, .completionCapacity = 8});
    ASSERT_TRUE(engine);
    auto streamer = EncodedPcmStreamer::Start(*engine, encoded, EncodedPcmStreamDesc{
        .bus = AudioBusId::Music,
        .bufferCapacityFrames = 16384,
        .decodeChunkFrames = 512,
    });
    ASSERT_TRUE(streamer) << streamer.error().message;
    ASSERT_TRUE(engine->pumpCompletions(8));
    while (!streamer->finished())
    {
        ASSERT_TRUE(streamer->pump());
        ASSERT_TRUE(engine->pumpCompletions(8));
    }
    std::vector<float> output(static_cast<Core::usize>(whole->frameCount() + 64U) * 2U);
    engine->mixRealtime(output.data(), static_cast<Core::u32>(whole->frameCount() + 64U), 2, 48000);
    EXPECT_TRUE(std::any_of(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(whole->frameCount() * 2),
                            [](float sample) { return sample != 0.0F; }));
    std::array<AudioCompletionEvent, 8> events{};
    auto count = engine->pumpCompletions(events, 8);
    ASSERT_TRUE(count);
    ASSERT_GE(*count, 1U);
    EXPECT_EQ(events[0].kind, AudioCompletionKind::Stopped);
}

TEST(EncodedPcmStreamerTests, LoopKeepsVoiceAlivePastNaturalEnd)
{
    const auto encoded = Tests::readAudioFixture("tone.wav");
    auto engine = AudioEngine::Create({.voiceCapacity = 1, .commandCapacity = 8, .completionCapacity = 8});
    ASSERT_TRUE(engine);
    auto streamer = EncodedPcmStreamer::Start(*engine, encoded, EncodedPcmStreamDesc{
        .play = AudioPlayDesc{.loopMode = AudioLoopMode::Loop},
        .bus = AudioBusId::Music,
        .bufferCapacityFrames = 2048,
        .decodeChunkFrames = 256,
    });
    ASSERT_TRUE(streamer) << streamer.error().message;
    ASSERT_TRUE(engine->pumpCompletions(8));
    for (int step = 0; step < 32; ++step)
    {
        ASSERT_TRUE(streamer->pump());
        std::array<float, 512> output{};
        engine->mixRealtime(output.data(), 256, 2, 48000);
        ASSERT_TRUE(engine->pumpCompletions(8));
    }
    auto live = engine->isVoiceLive(streamer->voice());
    ASSERT_TRUE(live);
    EXPECT_TRUE(*live);
    EXPECT_FALSE(streamer->finished());
    ASSERT_TRUE(streamer->cancel());
    ASSERT_TRUE(engine->pumpCompletions(8));
}

} // namespace
} // namespace Tina::Audio
