#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/audio/AudioClipView.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

TEST(AudioClipPayloadTests, WriteParseRoundTrip)
{
    const std::array<float, 8> pcm{0.0F, 0.25F, 0.5F, 0.25F, 0.0F, -0.25F, -0.5F, -0.25F};
    auto written = writeAudioClipPayloadBytes(AudioClipPayloadDesc{
        .channels = 1,
        .sampleRate = 8000,
        .frameCount = 8,
        .interleavedPcm = pcm,
    });
    ASSERT_TRUE(written.has_value()) << (written ? "" : written.error().message);
    ASSERT_EQ(written->size(), AudioClipWire::HeaderBytes + pcm.size() * sizeof(float));

    auto view = parseAudioClipPayload(*written);
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_EQ(view->schemaVersion, AudioClipWire::SchemaVersion);
    EXPECT_EQ(view->channels, 1U);
    EXPECT_EQ(view->sampleRate, 8000U);
    EXPECT_EQ(view->frameCount, 8U);
    ASSERT_EQ(view->interleavedPcm.size(), pcm.size());
    for (std::size_t i = 0; i < pcm.size(); ++i)
    {
        EXPECT_FLOAT_EQ(view->interleavedPcm[i], pcm[i]);
    }
}

TEST(AudioClipPayloadTests, StereoRoundTrip)
{
    const std::array<float, 6> pcm{0.1F, -0.1F, 0.2F, -0.2F, 0.3F, -0.3F};
    auto written = writeAudioClipPayloadBytes(AudioClipPayloadDesc{
        .channels = 2,
        .sampleRate = 48000,
        .frameCount = 3,
        .interleavedPcm = pcm,
    });
    ASSERT_TRUE(written.has_value()) << (written ? "" : written.error().message);
    auto view = parseAudioClipPayload(*written);
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_EQ(view->channels, 2U);
    EXPECT_EQ(view->frameCount, 3U);
    EXPECT_EQ(view->interleavedPcm.size(), 6U);
}

TEST(AudioClipPayloadTests, RejectsSizeMismatchAndInvalidGeometry)
{
    const std::array<float, 4> pcm{0.0F, 0.0F, 0.0F, 0.0F};
    auto mismatch = writeAudioClipPayloadBytes(AudioClipPayloadDesc{
        .channels = 1,
        .sampleRate = 8000,
        .frameCount = 8,
        .interleavedPcm = pcm,
    });
    ASSERT_FALSE(mismatch.has_value());
    EXPECT_EQ(mismatch.error().code, AssetFormatErrorCode::InvalidLayout);

    auto badChannels = writeAudioClipPayloadBytes(AudioClipPayloadDesc{
        .channels = 0,
        .sampleRate = 8000,
        .frameCount = 4,
        .interleavedPcm = pcm,
    });
    ASSERT_FALSE(badChannels.has_value());

    auto badRate = writeAudioClipPayloadBytes(AudioClipPayloadDesc{
        .channels = 1,
        .sampleRate = 10,
        .frameCount = 4,
        .interleavedPcm = pcm,
    });
    ASSERT_FALSE(badRate.has_value());
}

TEST(AudioClipPayloadTests, CookedAudioClipRoundTrip)
{
    const auto clipId = *Core::AssetId::fromBytes(idBytes(0xA1));
    const std::array<float, 4> pcm{0.5F, 0.0F, -0.5F, 0.0F};
    auto cooked = writeCookedAudioClipAsset(clipId, AudioClipPayloadDesc{
                                                        .channels = 1,
                                                        .sampleRate = 22050,
                                                        .frameCount = 4,
                                                        .interleavedPcm = pcm,
                                                    });
    ASSERT_TRUE(cooked.has_value()) << (cooked ? "" : cooked.error().message);

    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << (asset ? "" : asset.error().message);
    EXPECT_EQ(asset->header().assetKind, AssetKind::AudioClip);
    EXPECT_EQ(asset->header().assetId, clipId);

    auto view = parseAudioClipPayload(asset->payload());
    ASSERT_TRUE(view.has_value()) << (view ? "" : view.error().message);
    EXPECT_EQ(view->sampleRate, 22050U);
    EXPECT_EQ(view->frameCount, 4U);
    EXPECT_FLOAT_EQ(view->interleavedPcm[0], 0.5F);
    ASSERT_TRUE(verifyCookedAssetContentHash(*asset).has_value());

    auto pcmView = Audio::pcmClipViewFromAudioClipPayload(*view);
    ASSERT_TRUE(pcmView.has_value()) << (pcmView ? "" : pcmView.error().message);
    EXPECT_EQ(pcmView->frameCount, 4U);
    EXPECT_EQ(pcmView->sampleRate, 22050U);
    EXPECT_EQ(pcmView->frames, view->interleavedPcm.data());
}

TEST(AudioClipPayloadTests, TruncatedPayloadFails)
{
    std::vector<std::byte> truncated(8, std::byte{0});
    auto view = parseAudioClipPayload(truncated);
    ASSERT_FALSE(view.has_value());
    EXPECT_EQ(view.error().code, AssetFormatErrorCode::InvalidLayout);
}

} // namespace
} // namespace Tina::AssetFormat
