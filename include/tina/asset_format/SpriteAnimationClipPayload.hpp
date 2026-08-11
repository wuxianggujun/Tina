#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::AssetFormat {

enum class SpriteAnimationPlaybackMode : Core::u8 {
    Once = 1,
    Loop = 2,
    PingPong = 3,
};

// SpriteAnimationClip cooked payload schema v2 (little-endian).
// Layout:
//   u16 schemaVersion (=2)
//   u8  playbackMode (Once/Loop/PingPong)
//   u8  flags (=0 reserved)
//   u32 frameCount
//   u32 spriteDependencyCount
//   u32 totalEventCount
//   u32 reserved0 (=0)
//   u32 reserved1 (=0)
//   u32 reserved2 (=0)
//   u32 reserved3 (=0)
//   Frame[frameCount] (12B each):
//     u32 spriteDependencyIndex
//     f32 durationSeconds (positive finite)
//     u16 eventStartIndex
//     u16 eventCount
//   Event[totalEventCount] (8B each):
//     u32 eventTag (non-zero, user-defined runtime identity)
//     u16 offset (fixed-point: 0..65535 → [0.0, 1.0])
//     u16 nameStringIndex (=0xFFFF, reserved for future string table)
//
// Sprite AssetIds are required CookedAsset dependencies. The dependency stream is
// sorted and de-duplicated by AssetId; frame order is retained through dependency indices.
namespace SpriteAnimationClipWire {
inline constexpr Core::u16 SchemaVersion = 2;
inline constexpr Core::u32 HeaderBytes = 32;
inline constexpr Core::u32 FrameBytes = 12;
inline constexpr Core::u32 EventBytes = 8;
inline constexpr Core::u32 MaxFrames = 4096;
inline constexpr Core::u32 MaxEventsPerFrame = 64;
inline constexpr Core::u32 MaxTotalEvents = 16384;
inline constexpr Core::u16 EventNameIndexNone = 0xFFFF;

// Every block starts on a 4-byte boundary, so no padding is encoded between them.
static_assert(HeaderBytes % 4 == 0);
static_assert(FrameBytes % 4 == 0);
static_assert(EventBytes % 4 == 0);
// eventStartIndex and eventCount are encoded as u16, so both limits must stay addressable.
static_assert(MaxTotalEvents <= 0xFFFFU);
static_assert(MaxEventsPerFrame <= 0xFFFFU);
} // namespace SpriteAnimationClipWire

struct SpriteAnimationEventDesc final {
    Core::u32 eventTag = 0;
    float normalizedOffset = 0.0F;
    std::string_view name{};
};

struct SpriteAnimationFrameDesc final {
    Core::AssetId spriteId{};
    float durationSeconds = 0.1F;
    std::span<const SpriteAnimationEventDesc> events{};
};

struct SpriteAnimationClipPayloadDesc final {
    SpriteAnimationPlaybackMode playbackMode = SpriteAnimationPlaybackMode::Loop;
    std::span<const SpriteAnimationFrameDesc> frames{};
};

struct SpriteAnimationEventPayloadView final {
    Core::u32 eventTag = 0;
    float normalizedOffset = 0.0F;
};

// Events are addressed as a [eventStartIndex, eventStartIndex + eventCount) range into the
// clip-wide event block. The wire event (u32 tag, u16 fixed-point offset, u16 nameStringIndex)
// does not share a layout with the decoded view, so events are decoded on demand through
// SpriteAnimationClipPayloadView::event rather than exposed as a zero-copy span.
struct SpriteAnimationFramePayloadView final {
    Core::u32 spriteDependencyIndex = 0;
    float durationSeconds = 0.0F;
    Core::u32 eventStartIndex = 0;
    Core::u32 eventCount = 0;
};

struct SpriteAnimationClipPayloadView final {
    Core::u16 schemaVersion = 0;
    SpriteAnimationPlaybackMode playbackMode = SpriteAnimationPlaybackMode::Loop;
    Core::u32 frameCount = 0;
    Core::u32 spriteDependencyCount = 0;
    Core::u32 totalEventCount = 0;
    std::span<const std::byte> framesBytes{};
    std::span<const std::byte> eventsBytes{};

    [[nodiscard]] std::optional<SpriteAnimationFramePayloadView> frame(Core::u32 index) const noexcept;

    // Decodes a single event by clip-wide index, as reported by frame().eventStartIndex.
    [[nodiscard]] std::optional<SpriteAnimationEventPayloadView> event(Core::u32 index) const noexcept;
};

// Builds the deterministic required Sprite dependency stream used by both the
// payload dependency indices and the outer CookedAsset table.
[[nodiscard]] Core::Result<std::vector<CookedAssetWriteDependency>>
makeSpriteAnimationClipDependencies(const SpriteAnimationClipPayloadDesc& desc);

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeSpriteAnimationClipPayloadBytes(const SpriteAnimationClipPayloadDesc& desc);

[[nodiscard]] Core::Result<SpriteAnimationClipPayloadView>
parseSpriteAnimationClipPayload(std::span<const std::byte> payload);

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedSpriteAnimationClipAsset(Core::AssetId assetId, const SpriteAnimationClipPayloadDesc& desc,
                                    TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
