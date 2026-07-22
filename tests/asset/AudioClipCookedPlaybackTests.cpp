#include <gtest/gtest.h>

#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/audio/AudioClipView.hpp>
#include <tina/audio/AudioEngine.hpp>
#include <tina/core/id/AssetId.hpp>

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::Tests {
namespace {

using Asset::CookedAssetFileLoadConfig;
using Asset::makeCookedAssetFileFromBytes;
using Asset::parseAudioClipFromCooked;
using AssetFormat::AudioClipPayloadDesc;
using AssetFormat::writeCookedAudioClipAsset;
using Audio::AudioEngine;
using Audio::AudioEngineConfig;
using Audio::pcmClipViewFromAudioClipPayload;

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

TEST(AudioClipCookedPlaybackTests, ParseCookedAudioClipAndPlayOneShot)
{
    std::pmr::unsynchronized_pool_resource memory{};
    const auto clipId = *Core::AssetId::fromBytes(idBytes(0x42));
    // 4 frames @ 48 kHz mono so mixRealtime same-rate path works.
    const std::array<float, 4> pcm{0.5F, 0.25F, -0.25F, -0.5F};
    auto cooked = writeCookedAudioClipAsset(clipId, AudioClipPayloadDesc{
                                                        .channels = 1,
                                                        .sampleRate = 48000,
                                                        .frameCount = 4,
                                                        .interleavedPcm = pcm,
                                                    });
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);

    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << (file ? "" : file.error().message);
    EXPECT_EQ(file->header().assetKind, AssetFormat::AssetKind::AudioClip);

    auto clip = parseAudioClipFromCooked(*file);
    ASSERT_TRUE(clip.has_value()) << (clip ? "" : clip.error().message);
    EXPECT_EQ(clip->frameCount, 4U);
    EXPECT_EQ(clip->sampleRate, 48000U);

    auto pcmView = pcmClipViewFromAudioClipPayload(*clip);
    ASSERT_TRUE(pcmView.has_value()) << (pcmView ? "" : pcmView.error().message);

    auto engine = AudioEngine::Create(AudioEngineConfig{
        .voiceCapacity = 2,
        .commandCapacity = 8,
        .completionCapacity = 8,
    });
    ASSERT_TRUE(engine.has_value()) << (engine ? "" : engine.error().message);

    auto voice = engine->playOneShotPcm(*pcmView);
    ASSERT_TRUE(voice.has_value()) << (voice ? "" : voice.error().message);
    ASSERT_TRUE(engine->pumpCompletions(4).has_value());

    float out[8]{};
    engine->mixRealtime(out, 2, 1, 48000);
    EXPECT_NEAR(out[0], 0.5F, 1.0e-4F);
    EXPECT_NEAR(out[1], 0.25F, 1.0e-4F);

    // Wrong kind rejects.
    auto textureCooked = AssetFormat::writeCookedTexture2DAsset(
        *Core::AssetId::fromBytes(idBytes(0x11)),
        AssetFormat::Texture2DPayloadDesc{
            .width = 1,
            .height = 1,
            .pixels = std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}},
        });
    ASSERT_TRUE(textureCooked.has_value());
    auto textureFile = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(textureCooked->begin(), textureCooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(textureFile.has_value());
    auto wrongKind = parseAudioClipFromCooked(*textureFile);
    ASSERT_FALSE(wrongKind.has_value());
}

} // namespace
} // namespace Tina::Tests
