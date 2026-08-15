#include <tina/asset_format/AnimationClip3DPayload.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace Tina::AssetFormat {
namespace {

TEST(AnimationClip3DPayloadTests, RoundTripsCanonicalTracks)
{
    const std::array<float, 2> translationTimes{0.0F, 1.0F};
    const std::array<float, 6> translationValues{0, 0, 0, 1, 2, 3};
    const std::array<float, 2> rotationTimes{0.0F, 1.0F};
    const std::array<float, 8> rotationValues{0, 0, 0, 1, 0, 0.70710677F, 0, 0.70710677F};
    const std::array tracks{
        AnimationTrackDesc{.jointIndex = 0, .channel = AnimationChannel::Translation,
                           .interpolation = AnimationInterpolation::Linear,
                           .times = translationTimes, .values = translationValues},
        AnimationTrackDesc{.jointIndex = 0, .channel = AnimationChannel::Rotation,
                           .interpolation = AnimationInterpolation::Step,
                           .times = rotationTimes, .values = rotationValues},
    };
    auto payload = writeAnimationClip3DPayloadBytes(AnimationClip3DPayloadDesc{
        .playbackMode = AnimationClip3DPlaybackMode::Loop, .jointCount = 1,
        .durationSeconds = 1.0F, .tracks = tracks});
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    auto view = parseAnimationClip3DPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->trackCount, 2U);
    EXPECT_EQ(view->totalKeyframeCount, 4U);
    EXPECT_EQ(view->track(1)->channel, AnimationChannel::Rotation);
    EXPECT_FLOAT_EQ(view->trackValues(0)[3], 1.0F);
}

TEST(AnimationClip3DPayloadTests, CookedAssetRoundTripUsesNewKind)
{
    const std::array<float, 1> times{0.0F};
    const std::array<float, 3> values{0, 0, 0};
    const std::array tracks{AnimationTrackDesc{.jointIndex = 0, .channel = AnimationChannel::Translation,
                                               .times = times, .values = values}};
    const auto id = *Core::AssetId::fromBytes(Core::AssetId::Bytes{std::byte{0x51}});
    auto cooked = writeCookedAnimationClip3DAsset(id, AnimationClip3DPayloadDesc{
        .jointCount = 1, .durationSeconds = 0.0F, .tracks = tracks});
    ASSERT_FALSE(cooked.has_value());
    auto valid = writeCookedAnimationClip3DAsset(id, AnimationClip3DPayloadDesc{
        .jointCount = 1, .durationSeconds = 0.0001F, .tracks =
            std::array{AnimationTrackDesc{.jointIndex = 0, .channel = AnimationChannel::Translation,
                                          .times = std::array<float, 1>{0.0001F},
                                          .values = values}}});
    ASSERT_TRUE(valid.has_value()) << valid.error().message;
    auto asset = parseCookedAssetView(*valid);
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().assetKind, AssetKind::AnimationClip3D);
}

TEST(AnimationClip3DPayloadTests, RejectsMalformedOrderDurationAndSchema)
{
    const std::array<float, 2> times{0.0F, 1.0F};
    const std::array<float, 6> values{0, 0, 0, 1, 1, 1};
    const std::array tracks{AnimationTrackDesc{.jointIndex = 0, .channel = AnimationChannel::Translation,
                                               .times = times, .values = values}};
    auto payload = writeAnimationClip3DPayloadBytes(AnimationClip3DPayloadDesc{
        .jointCount = 1, .durationSeconds = 1.0F, .tracks = tracks});
    ASSERT_TRUE(payload.has_value());
    auto malformed = *payload;
    malformed[0] = std::byte{2};
    EXPECT_FALSE(parseAnimationClip3DPayload(malformed).has_value());
    malformed = *payload;
    malformed[28] = std::byte{1};
    EXPECT_FALSE(parseAnimationClip3DPayload(malformed).has_value());
    malformed = *payload;
    malformed.pop_back();
    EXPECT_FALSE(parseAnimationClip3DPayload(malformed).has_value());

    const std::array badTracks{
        AnimationTrackDesc{.jointIndex = 0, .channel = AnimationChannel::Rotation,
                           .times = times, .values = values}};
    EXPECT_FALSE(writeAnimationClip3DPayloadBytes(AnimationClip3DPayloadDesc{
        .jointCount = 1, .durationSeconds = 1.0F, .tracks = badTracks}).has_value());
}

TEST(AnimationClip3DPayloadTests, RejectsMisalignedFloatBlocks)
{
    const std::array<float, 2> times{0.0F, 1.0F};
    const std::array<float, 6> values{0, 0, 0, 1, 1, 1};
    const std::array tracks{AnimationTrackDesc{.jointIndex = 0,
                                               .channel = AnimationChannel::Translation,
                                               .times = times,
                                               .values = values}};
    auto payload = writeAnimationClip3DPayloadBytes(AnimationClip3DPayloadDesc{
        .jointCount = 1, .durationSeconds = 1.0F, .tracks = tracks});
    ASSERT_TRUE(payload.has_value()) << payload.error().message;

    std::vector<std::byte> storage(payload->size() + alignof(float));
    const auto storageAddress = reinterpret_cast<std::uintptr_t>(storage.data());
    std::size_t payloadOffset = 0;
    while (((storageAddress + payloadOffset) % alignof(float)) == 0U)
    {
        ++payloadOffset;
    }
    std::copy(payload->begin(), payload->end(), storage.begin() + payloadOffset);

    const auto misalignedPayload = std::span<const std::byte>{storage}.subspan(payloadOffset, payload->size());
    auto parsed = parseAnimationClip3DPayload(misalignedPayload);
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(AnimationClip3DPayloadTests, RejectsFrozenJointAndPerTrackKeyLimits)
{
    const std::array<float, 1> oneTime{1.0F};
    const std::array<float, 3> oneValue{0.0F, 0.0F, 0.0F};
    const std::array<AnimationTrackDesc, 1> oneTrack{
        AnimationTrackDesc{.jointIndex = 0,
                           .channel = AnimationChannel::Translation,
                           .times = oneTime,
                           .values = oneValue},
    };
    EXPECT_FALSE(writeAnimationClip3DPayloadBytes(AnimationClip3DPayloadDesc{
        .jointCount = static_cast<Core::u16>(AnimationClip3DWire::MaxJointCount + 1U),
        .durationSeconds = 1.0F,
        .tracks = oneTrack,
    }).has_value());

    std::vector<float> times(AnimationClip3DWire::MaxKeyframesPerTrack + 1U);
    std::vector<float> values(times.size() * 3U, 0.0F);
    for (std::size_t index = 0; index < times.size(); ++index)
    {
        times[index] = static_cast<float>(index + 1U);
    }
    const std::array<AnimationTrackDesc, 1> oversizedTrack{
        AnimationTrackDesc{.jointIndex = 0,
                           .channel = AnimationChannel::Translation,
                           .times = times,
                           .values = values},
    };
    EXPECT_FALSE(writeAnimationClip3DPayloadBytes(AnimationClip3DPayloadDesc{
        .jointCount = 1,
        .durationSeconds = AnimationClip3DWire::MaxDurationSeconds,
        .tracks = oversizedTrack,
    }).has_value());
}

TEST(AnimationClip3DPayloadTests, RejectsFrozenTrackAndAggregateKeyLimits)
{
    std::vector<AnimationTrackDesc> tooManyTracks(AnimationClip3DWire::MaxTracks + 1U);
    EXPECT_FALSE(writeAnimationClip3DPayloadBytes(AnimationClip3DPayloadDesc{
        .jointCount = AnimationClip3DWire::MaxJointCount,
        .durationSeconds = 1.0F,
        .tracks = tooManyTracks,
    }).has_value());

    std::vector<float> times(AnimationClip3DWire::MaxKeyframesPerTrack);
    for (std::size_t index = 0; index < times.size(); ++index)
    {
        times[index] = static_cast<float>(index) *
                       (AnimationClip3DWire::MaxDurationSeconds /
                        static_cast<float>(times.size() - 1U));
    }
    times.back() = AnimationClip3DWire::MaxDurationSeconds;
    std::vector<float> vec3Values(times.size() * 3U, 0.0F);
    std::vector<float> rotationValues(times.size() * 4U, 0.0F);
    for (std::size_t key = 0; key < times.size(); ++key)
    {
        rotationValues[key * 4U + 3U] = 1.0F;
    }
    constexpr std::size_t TrackCount =
        (AnimationClip3DWire::MaxTotalKeyframes /
         AnimationClip3DWire::MaxKeyframesPerTrack) + 1U;
    std::vector<AnimationTrackDesc> tracks;
    tracks.reserve(TrackCount);
    for (std::size_t index = 0; index < TrackCount; ++index)
    {
        const auto channel = static_cast<AnimationChannel>(
            static_cast<Core::u8>(AnimationChannel::Translation) + (index % 3U));
        tracks.push_back(AnimationTrackDesc{
            .jointIndex = static_cast<Core::u16>(index / 3U),
            .channel = channel,
            .times = times,
            .values = channel == AnimationChannel::Rotation
                          ? std::span<const float>{rotationValues}
                          : std::span<const float>{vec3Values},
        });
    }
    EXPECT_FALSE(writeAnimationClip3DPayloadBytes(AnimationClip3DPayloadDesc{
        .jointCount = AnimationClip3DWire::MaxJointCount,
        .durationSeconds = AnimationClip3DWire::MaxDurationSeconds,
        .tracks = tracks,
    }).has_value());
}

} // namespace
} // namespace Tina::AssetFormat
