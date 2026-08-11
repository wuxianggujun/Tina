#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/SpriteAnimationClipPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
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

void putU32(std::vector<std::byte>& bytes, Core::usize offset, Core::u32 value)
{
    for (Core::usize index = 0; index < sizeof(Core::u32); ++index)
    {
        bytes.at(offset + index) = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void expectParseError(const std::vector<std::byte>& payload, Core::ErrorCode expectedCode)
{
    auto parsed = parseSpriteAnimationClipPayload(payload);
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, expectedCode);
}

TEST(SpriteAnimationClipPayloadTests, RoundTripsFrameOrderThroughSortedSpriteDependencies)
{
    const auto spriteA = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteB = *Core::AssetId::fromBytes(idBytes(2U));
    const auto clipId = *Core::AssetId::fromBytes(idBytes(9U));
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteB, .durationSeconds = 0.125F},
        SpriteAnimationFrameDesc{.spriteId = spriteA, .durationSeconds = 0.25F},
        SpriteAnimationFrameDesc{.spriteId = spriteB, .durationSeconds = 0.5F},
    };
    const SpriteAnimationClipPayloadDesc desc{
        .playbackMode = SpriteAnimationPlaybackMode::PingPong,
        .frames = frames,
    };

    auto dependencies = makeSpriteAnimationClipDependencies(desc);
    ASSERT_TRUE(dependencies.has_value()) << dependencies.error().message;
    ASSERT_EQ(dependencies->size(), 2U);
    EXPECT_EQ((*dependencies)[0].assetId, spriteA);
    EXPECT_EQ((*dependencies)[1].assetId, spriteB);
    EXPECT_EQ((*dependencies)[0].expectedKind, AssetKind::Sprite);

    auto payload = writeSpriteAnimationClipPayloadBytes(desc);
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    EXPECT_EQ(payload->size(), SpriteAnimationClipWire::HeaderBytes +
                                   3U * SpriteAnimationClipWire::FrameBytes);
    // 0.125F is IEEE-754 0x3e000000. Cooked payloads are always little-endian.
    // Frame 0 duration lives at header (32B) + spriteDependencyIndex (4B).
    constexpr Core::usize frame0Duration = SpriteAnimationClipWire::HeaderBytes + 4U;
    EXPECT_EQ(std::to_integer<unsigned char>((*payload)[frame0Duration + 0U]), 0x00U);
    EXPECT_EQ(std::to_integer<unsigned char>((*payload)[frame0Duration + 1U]), 0x00U);
    EXPECT_EQ(std::to_integer<unsigned char>((*payload)[frame0Duration + 2U]), 0x00U);
    EXPECT_EQ(std::to_integer<unsigned char>((*payload)[frame0Duration + 3U]), 0x3EU);
    auto view = parseSpriteAnimationClipPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->schemaVersion, SpriteAnimationClipWire::SchemaVersion);
    EXPECT_EQ(view->playbackMode, SpriteAnimationPlaybackMode::PingPong);
    EXPECT_EQ(view->frameCount, 3U);
    EXPECT_EQ(view->spriteDependencyCount, 2U);
    EXPECT_EQ(view->totalEventCount, 0U);
    EXPECT_TRUE(view->eventsBytes.empty());
    ASSERT_TRUE(view->frame(0U).has_value());
    ASSERT_TRUE(view->frame(1U).has_value());
    ASSERT_TRUE(view->frame(2U).has_value());
    EXPECT_EQ(view->frame(0U)->spriteDependencyIndex, 1U);
    EXPECT_EQ(view->frame(1U)->spriteDependencyIndex, 0U);
    EXPECT_EQ(view->frame(2U)->spriteDependencyIndex, 1U);
    EXPECT_FLOAT_EQ(view->frame(0U)->durationSeconds, 0.125F);
    EXPECT_FALSE(view->frame(3U).has_value());

    auto cooked = writeCookedSpriteAnimationClipAsset(clipId, desc);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().assetKind, AssetKind::SpriteAnimationClip);
    EXPECT_EQ(asset->header().assetTypeVersion, SpriteAnimationClipWire::SchemaVersion);
    ASSERT_EQ(asset->header().dependencyCount, 2U);
    ASSERT_TRUE(asset->dependency(0U).has_value());
    ASSERT_TRUE(asset->dependency(1U).has_value());
    EXPECT_EQ(asset->dependency(0U)->assetId, spriteA);
    EXPECT_EQ(asset->dependency(1U)->assetId, spriteB);
    EXPECT_EQ(asset->dependency(1U)->expectedKind, AssetKind::Sprite);
    EXPECT_TRUE(verifyCookedAssetContentHash(*asset).has_value());
}

TEST(SpriteAnimationClipPayloadTests, SupportsEveryPlaybackMode)
{
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F},
    };
    constexpr std::array modes{
        SpriteAnimationPlaybackMode::Once,
        SpriteAnimationPlaybackMode::Loop,
        SpriteAnimationPlaybackMode::PingPong,
    };
    for (const auto mode : modes)
    {
        auto payload = writeSpriteAnimationClipPayloadBytes(
            SpriteAnimationClipPayloadDesc{.playbackMode = mode, .frames = frames});
        ASSERT_TRUE(payload.has_value());
        auto view = parseSpriteAnimationClipPayload(*payload);
        ASSERT_TRUE(view.has_value());
        EXPECT_EQ(view->playbackMode, mode);
    }
}

TEST(SpriteAnimationClipPayloadTests, RejectsInvalidDescriptions)
{
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes({}).has_value());

    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F},
    };
    auto desc = SpriteAnimationClipPayloadDesc{.frames = frames};

    desc.playbackMode = static_cast<SpriteAnimationPlaybackMode>(99U);
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes(desc).has_value());
    desc.playbackMode = SpriteAnimationPlaybackMode::Loop;

    frames[0].spriteId = {};
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes(desc).has_value());
    frames[0].spriteId = spriteId;

    frames[0].durationSeconds = 0.0F;
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes(desc).has_value());
    frames[0].durationSeconds = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes(desc).has_value());
    frames[0].durationSeconds = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(writeSpriteAnimationClipPayloadBytes(desc).has_value());
}

TEST(SpriteAnimationClipPayloadTests, RejectsCorruptHeaderFrameAndDuration)
{
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F},
    };
    auto payload = writeSpriteAnimationClipPayloadBytes(
        SpriteAnimationClipPayloadDesc{.frames = frames});
    ASSERT_TRUE(payload.has_value());

    auto corrupted = *payload;
    corrupted[2] = std::byte{99};
    expectParseError(corrupted, AssetFormatErrorCode::UnsupportedValue);

    corrupted = *payload;
    corrupted[3] = std::byte{1};
    expectParseError(corrupted, AssetFormatErrorCode::UnsupportedValue);

    // Each reserved header word must be zero.
    for (const Core::usize reservedOffset : {16U, 20U, 24U, 28U})
    {
        corrupted = *payload;
        putU32(corrupted, reservedOffset, 1U);
        expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);
    }

    corrupted = *payload;
    putU32(corrupted, SpriteAnimationClipWire::HeaderBytes, 1U);
    expectParseError(corrupted, AssetFormatErrorCode::InvalidDependency);

    corrupted = *payload;
    const float zeroDuration = 0.0F;
    std::memcpy(corrupted.data() + SpriteAnimationClipWire::HeaderBytes + 4U, &zeroDuration,
                sizeof(float));
    expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);

    corrupted = *payload;
    corrupted.pop_back();
    expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);
}

TEST(SpriteAnimationClipPayloadTests, RejectsSchemaV1Payloads)
{
    // Well-formed v1 payload: 16B header + one 8-byte frame. Its 24-byte total is
    // shorter than the 32-byte v2 header, so schemaVersion must be checked first for
    // the error to name the real cause.
    std::vector<std::byte> v1Payload(16U + 8U, std::byte{0});
    v1Payload[0] = std::byte{1};
    v1Payload[2] = static_cast<std::byte>(SpriteAnimationPlaybackMode::Loop);
    putU32(v1Payload, 4U, 1U);
    putU32(v1Payload, 8U, 1U);
    const float duration = 0.1F;
    std::memcpy(v1Payload.data() + 20U, &duration, sizeof(float));
    expectParseError(v1Payload, AssetFormatErrorCode::UnsupportedSchema);

    // A v1 payload long enough to reach the v2 header size is rejected the same way.
    std::vector<std::byte> longV1Payload(16U + 8U * 4U, std::byte{0});
    longV1Payload[0] = std::byte{1};
    longV1Payload[2] = static_cast<std::byte>(SpriteAnimationPlaybackMode::Loop);
    putU32(longV1Payload, 4U, 4U);
    putU32(longV1Payload, 8U, 1U);
    expectParseError(longV1Payload, AssetFormatErrorCode::UnsupportedSchema);

    // Unknown future schemas share the same rejection path.
    auto futureSchema = longV1Payload;
    futureSchema[0] = std::byte{3};
    expectParseError(futureSchema, AssetFormatErrorCode::UnsupportedSchema);

    // Payloads too short to even hold schemaVersion still report InvalidHeader.
    const std::vector<std::byte> truncated(1U, std::byte{2});
    expectParseError(truncated, AssetFormatErrorCode::InvalidHeader);
}

TEST(SpriteAnimationClipPayloadTests, RoundTripsFrameEvents)
{
    const auto spriteA = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteB = *Core::AssetId::fromBytes(idBytes(2U));
    constexpr Core::u32 footTag = 0x464F4F54U;
    constexpr Core::u32 dustTag = 0x44555354U;
    constexpr Core::u32 hitTag = 0x48495400U;

    const std::array frame0Events{
        SpriteAnimationEventDesc{.eventTag = footTag, .normalizedOffset = 0.5F},
        SpriteAnimationEventDesc{.eventTag = dustTag, .normalizedOffset = 0.75F},
    };
    const std::array frame1Events{
        SpriteAnimationEventDesc{.eventTag = hitTag, .normalizedOffset = 0.0F, .name = "sword_hit"},
    };
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteA, .durationSeconds = 0.1F, .events = frame0Events},
        SpriteAnimationFrameDesc{.spriteId = spriteB, .durationSeconds = 0.2F, .events = frame1Events},
        SpriteAnimationFrameDesc{.spriteId = spriteA, .durationSeconds = 0.15F},
    };
    const SpriteAnimationClipPayloadDesc desc{
        .playbackMode = SpriteAnimationPlaybackMode::Loop,
        .frames = frames,
    };

    auto payload = writeSpriteAnimationClipPayloadBytes(desc);
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    EXPECT_EQ(payload->size(), SpriteAnimationClipWire::HeaderBytes +
                                   3U * SpriteAnimationClipWire::FrameBytes +
                                   3U * SpriteAnimationClipWire::EventBytes);

    auto view = parseSpriteAnimationClipPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->totalEventCount, 3U);
    EXPECT_EQ(view->eventsBytes.size(), 3U * SpriteAnimationClipWire::EventBytes);

    // eventStartIndex is an exclusive scan over per-frame event counts.
    ASSERT_TRUE(view->frame(0U).has_value());
    ASSERT_TRUE(view->frame(1U).has_value());
    ASSERT_TRUE(view->frame(2U).has_value());
    EXPECT_EQ(view->frame(0U)->eventStartIndex, 0U);
    EXPECT_EQ(view->frame(0U)->eventCount, 2U);
    EXPECT_EQ(view->frame(1U)->eventStartIndex, 2U);
    EXPECT_EQ(view->frame(1U)->eventCount, 1U);
    EXPECT_EQ(view->frame(2U)->eventStartIndex, 3U);
    EXPECT_EQ(view->frame(2U)->eventCount, 0U);

    // Decoded events resolve through the clip-wide event accessor.
    ASSERT_TRUE(view->event(0U).has_value());
    ASSERT_TRUE(view->event(1U).has_value());
    ASSERT_TRUE(view->event(2U).has_value());
    EXPECT_FALSE(view->event(3U).has_value());
    EXPECT_EQ(view->event(0U)->eventTag, footTag);
    EXPECT_NEAR(view->event(0U)->normalizedOffset, 0.5F, 1.0F / 65535.0F);
    EXPECT_EQ(view->event(1U)->eventTag, dustTag);
    EXPECT_NEAR(view->event(1U)->normalizedOffset, 0.75F, 1.0F / 65535.0F);
    EXPECT_EQ(view->event(2U)->eventTag, hitTag);
    EXPECT_FLOAT_EQ(view->event(2U)->normalizedOffset, 0.0F);

    // Events are stored tag (u32) + offset (u16 fixed-point) + nameStringIndex (u16).
    const Core::usize eventsOffset = SpriteAnimationClipWire::HeaderBytes +
                                     3U * SpriteAnimationClipWire::FrameBytes;
    const auto eventTagAt = [&](Core::u32 eventIndex) {
        const Core::usize offset =
            eventsOffset + static_cast<Core::usize>(eventIndex) * SpriteAnimationClipWire::EventBytes;
        Core::u32 tag = 0;
        for (Core::usize index = 0; index < sizeof(Core::u32); ++index)
        {
            tag |= static_cast<Core::u32>(
                       std::to_integer<unsigned char>((*payload)[offset + index]))
                   << (index * 8U);
        }
        return tag;
    };
    const auto eventFieldU16 = [&](Core::u32 eventIndex, Core::usize fieldOffset) {
        const Core::usize offset = eventsOffset +
                                   static_cast<Core::usize>(eventIndex) *
                                       SpriteAnimationClipWire::EventBytes +
                                   fieldOffset;
        return static_cast<Core::u32>(
            std::to_integer<unsigned char>((*payload)[offset]) |
            (std::to_integer<unsigned char>((*payload)[offset + 1U]) << 8U));
    };
    EXPECT_EQ(eventTagAt(0U), footTag);
    EXPECT_EQ(eventFieldU16(0U, 4U), 32768U); // round(0.5 * 65535)
    EXPECT_EQ(eventFieldU16(0U, 6U), SpriteAnimationClipWire::EventNameIndexNone);
    EXPECT_EQ(eventTagAt(1U), dustTag);
    EXPECT_EQ(eventFieldU16(1U, 4U), 49151U); // round(0.75 * 65535)
    EXPECT_EQ(eventTagAt(2U), hitTag);
    EXPECT_EQ(eventFieldU16(2U, 4U), 0U);
    // Authoring names are parsed but not serialized in schema v2.0.
    EXPECT_EQ(eventFieldU16(2U, 6U), SpriteAnimationClipWire::EventNameIndexNone);

    auto cooked = writeCookedSpriteAnimationClipAsset(*Core::AssetId::fromBytes(idBytes(9U)), desc);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().assetTypeVersion, SpriteAnimationClipWire::SchemaVersion);
    EXPECT_TRUE(verifyCookedAssetContentHash(*asset).has_value());
}

TEST(SpriteAnimationClipPayloadTests, PreservesEventOffsetEndpoints)
{
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    const std::array events{
        SpriteAnimationEventDesc{.eventTag = 1U, .normalizedOffset = 0.0F},
        SpriteAnimationEventDesc{.eventTag = 2U, .normalizedOffset = 1.0F},
    };
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F, .events = events},
    };
    auto payload = writeSpriteAnimationClipPayloadBytes(
        SpriteAnimationClipPayloadDesc{.frames = frames});
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    auto view = parseSpriteAnimationClipPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;

    // Both endpoints must survive the fixed-point round trip exactly.
    ASSERT_TRUE(view->event(0U).has_value());
    ASSERT_TRUE(view->event(1U).has_value());
    EXPECT_FLOAT_EQ(view->event(0U)->normalizedOffset, 0.0F);
    EXPECT_FLOAT_EQ(view->event(1U)->normalizedOffset, 1.0F);
}

TEST(SpriteAnimationClipPayloadTests, AcceptsMaximumEventsPerFrame)
{
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    std::vector<SpriteAnimationEventDesc> events;
    events.reserve(SpriteAnimationClipWire::MaxEventsPerFrame);
    for (Core::u32 index = 0; index < SpriteAnimationClipWire::MaxEventsPerFrame; ++index)
    {
        events.push_back(SpriteAnimationEventDesc{
            .eventTag = index + 1U,
            .normalizedOffset = static_cast<float>(index) /
                                static_cast<float>(SpriteAnimationClipWire::MaxEventsPerFrame),
        });
    }
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F, .events = events},
    };
    auto payload = writeSpriteAnimationClipPayloadBytes(
        SpriteAnimationClipPayloadDesc{.frames = frames});
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    auto view = parseSpriteAnimationClipPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->totalEventCount, SpriteAnimationClipWire::MaxEventsPerFrame);
    ASSERT_TRUE(view->frame(0U).has_value());
    EXPECT_EQ(view->frame(0U)->eventCount, SpriteAnimationClipWire::MaxEventsPerFrame);
    // Duplicate offsets are permitted and must not trip the sorted-order check.
    const std::vector<SpriteAnimationEventDesc> tiedEvents(
        4U, SpriteAnimationEventDesc{.eventTag = 7U, .normalizedOffset = 0.5F});
    const std::array tiedFrames{
        SpriteAnimationFrameDesc{
            .spriteId = spriteId, .durationSeconds = 0.1F, .events = tiedEvents},
    };
    auto tiedPayload = writeSpriteAnimationClipPayloadBytes(
        SpriteAnimationClipPayloadDesc{.frames = tiedFrames});
    ASSERT_TRUE(tiedPayload.has_value()) << tiedPayload.error().message;
    EXPECT_TRUE(parseSpriteAnimationClipPayload(*tiedPayload).has_value());
}

TEST(SpriteAnimationClipPayloadTests, RejectsInvalidEventDescriptions)
{
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    std::array events{
        SpriteAnimationEventDesc{.eventTag = 1U, .normalizedOffset = 0.25F},
        SpriteAnimationEventDesc{.eventTag = 2U, .normalizedOffset = 0.75F},
    };
    std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F, .events = events},
    };
    const auto desc = SpriteAnimationClipPayloadDesc{.frames = frames};
    ASSERT_TRUE(writeSpriteAnimationClipPayloadBytes(desc).has_value());

    const auto expectWriteError = [&](Core::ErrorCode expectedCode) {
        auto written = writeSpriteAnimationClipPayloadBytes(desc);
        ASSERT_FALSE(written.has_value());
        EXPECT_EQ(written.error().code, expectedCode);
    };

    events[0].eventTag = 0U;
    expectWriteError(AssetFormatErrorCode::InvalidLayout);
    events[0].eventTag = 1U;

    events[1].normalizedOffset = 1.5F;
    expectWriteError(AssetFormatErrorCode::InvalidLayout);
    events[1].normalizedOffset = -0.25F;
    expectWriteError(AssetFormatErrorCode::InvalidLayout);
    events[1].normalizedOffset = std::numeric_limits<float>::quiet_NaN();
    expectWriteError(AssetFormatErrorCode::InvalidLayout);
    events[1].normalizedOffset = 0.75F;

    // Descending offsets violate the sorted-by-offset authoring contract.
    events[0].normalizedOffset = 0.9F;
    expectWriteError(AssetFormatErrorCode::InvalidLayout);
    events[0].normalizedOffset = 0.25F;

    const std::vector<SpriteAnimationEventDesc> tooManyEvents(
        SpriteAnimationClipWire::MaxEventsPerFrame + 1U,
        SpriteAnimationEventDesc{.eventTag = 1U, .normalizedOffset = 0.5F});
    const std::array overflowFrames{
        SpriteAnimationFrameDesc{
            .spriteId = spriteId, .durationSeconds = 0.1F, .events = tooManyEvents},
    };
    auto overflow = writeSpriteAnimationClipPayloadBytes(
        SpriteAnimationClipPayloadDesc{.frames = overflowFrames});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, AssetFormatErrorCode::SizeLimitExceeded);
}

TEST(SpriteAnimationClipPayloadTests, RejectsCorruptEventBlock)
{
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(1U));
    const std::array events{
        SpriteAnimationEventDesc{.eventTag = 0x11223344U, .normalizedOffset = 0.25F},
        SpriteAnimationEventDesc{.eventTag = 0x55667788U, .normalizedOffset = 0.75F},
    };
    const std::array frames{
        SpriteAnimationFrameDesc{.spriteId = spriteId, .durationSeconds = 0.1F, .events = events},
    };
    auto payload = writeSpriteAnimationClipPayloadBytes(
        SpriteAnimationClipPayloadDesc{.frames = frames});
    ASSERT_TRUE(payload.has_value()) << payload.error().message;

    constexpr Core::usize eventsOffset =
        SpriteAnimationClipWire::HeaderBytes + SpriteAnimationClipWire::FrameBytes;

    auto corrupted = *payload;
    putU32(corrupted, eventsOffset, 0U);
    expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);

    // nameStringIndex must stay 0xFFFF until a string table exists (v2.1+).
    corrupted = *payload;
    corrupted[eventsOffset + 6U] = std::byte{0};
    corrupted[eventsOffset + 7U] = std::byte{0};
    expectParseError(corrupted, AssetFormatErrorCode::UnsupportedValue);

    // Swap the two offsets so the frame's event run is no longer ascending.
    corrupted = *payload;
    corrupted[eventsOffset + 4U] = std::byte{0xFF};
    corrupted[eventsOffset + 5U] = std::byte{0xFF};
    expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);

    // eventCount beyond totalEventCount must be rejected.
    corrupted = *payload;
    corrupted[SpriteAnimationClipWire::HeaderBytes + 10U] = std::byte{9};
    expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);

    // A frame that leaves part of the event block unreferenced breaks the exclusive scan.
    corrupted = *payload;
    corrupted[SpriteAnimationClipWire::HeaderBytes + 10U] = std::byte{1};
    expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);

    // A non-zero start index on the first frame breaks the exclusive scan as well.
    corrupted = *payload;
    corrupted[SpriteAnimationClipWire::HeaderBytes + 8U] = std::byte{1};
    corrupted[SpriteAnimationClipWire::HeaderBytes + 10U] = std::byte{1};
    expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);

    // Declared totalEventCount must match the actual payload size.
    corrupted = *payload;
    putU32(corrupted, 12U, 3U);
    expectParseError(corrupted, AssetFormatErrorCode::InvalidLayout);

    corrupted = *payload;
    putU32(corrupted, 12U, SpriteAnimationClipWire::MaxTotalEvents + 1U);
    expectParseError(corrupted, AssetFormatErrorCode::SizeLimitExceeded);
}

} // namespace
} // namespace Tina::AssetFormat
