#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <vector>

namespace Tina::AssetFormat {

enum class SpriteAnimationPlaybackMode : Core::u8 {
    Once = 1,
    Loop = 2,
    PingPong = 3,
};

// SpriteAnimationClip cooked payload schema v1 (little-endian).
// Layout:
//   u16 schemaVersion (=1)
//   u8  playbackMode (Once/Loop/PingPong)
//   u8  flags (=0 reserved)
//   u32 frameCount
//   u32 spriteDependencyCount
//   u32 reserved (=0)
//   Frame[frameCount] (8B each):
//     u32 spriteDependencyIndex
//     f32 durationSeconds (positive finite)
//
// Sprite AssetIds are required CookedAsset dependencies. The dependency stream is
// sorted and de-duplicated by AssetId; frame order is retained through dependency indices.
namespace SpriteAnimationClipWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 16;
inline constexpr Core::u32 FrameBytes = 8;
inline constexpr Core::u32 MaxFrames = 4096;
} // namespace SpriteAnimationClipWire

struct SpriteAnimationFrameDesc final {
    Core::AssetId spriteId{};
    float durationSeconds = 0.1F;
};

struct SpriteAnimationClipPayloadDesc final {
    SpriteAnimationPlaybackMode playbackMode = SpriteAnimationPlaybackMode::Loop;
    std::span<const SpriteAnimationFrameDesc> frames{};
};

struct SpriteAnimationFramePayloadView final {
    Core::u32 spriteDependencyIndex = 0;
    float durationSeconds = 0.0F;
};

struct SpriteAnimationClipPayloadView final {
    Core::u16 schemaVersion = 0;
    SpriteAnimationPlaybackMode playbackMode = SpriteAnimationPlaybackMode::Loop;
    Core::u32 frameCount = 0;
    Core::u32 spriteDependencyCount = 0;
    std::span<const std::byte> framesBytes{};

    [[nodiscard]] std::optional<SpriteAnimationFramePayloadView> frame(Core::u32 index) const noexcept;
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
