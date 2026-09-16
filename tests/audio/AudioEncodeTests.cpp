#include <tina/audio/AudioEncode.hpp>
#include <tina/audio/AudioErrors.hpp>

#include "support/AudioFixtures.hpp"
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace Tina::Audio {
namespace {

TEST(AudioEncodeTests, WavFixtureTranscodesToSeekableOpus)
{
    const auto source = Tests::readAudioFixture("tone.wav");
    auto decoder = AudioDecoder::open(source, AudioDecodeConfig{.outputSampleRate = 48000});
    ASSERT_TRUE(decoder) << decoder.error().message;
    const auto channels = decoder->channels();
    auto opus = encodeOpusOggFromDecoder(*decoder);
    ASSERT_TRUE(opus) << opus.error().message;
    EXPECT_GE(opus->size(), 64U);
    auto cooked = AudioDecoder::open(*opus);
    ASSERT_TRUE(cooked) << cooked.error().message;
    EXPECT_EQ(cooked->codec(), AudioSourceCodec::Opus);
    EXPECT_EQ(cooked->channels(), channels);
    EXPECT_EQ(cooked->sampleRate(), 48000U);
    std::vector<float> first(channels);
    auto frames = cooked->readPcm(first);
    ASSERT_TRUE(frames);
    ASSERT_EQ(*frames, 1U);
    EXPECT_TRUE(std::isfinite(first[0]));
    ASSERT_TRUE(cooked->seekFrame(0));
    std::vector<float> again(channels);
    auto replay = cooked->readPcm(again);
    ASSERT_TRUE(replay);
    ASSERT_EQ(*replay, 1U);
    EXPECT_EQ(first[0], again[0]);
}

} // namespace
} // namespace Tina::Audio
