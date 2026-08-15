#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/SkinnedMeshPayload.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <vector>

namespace Tina::AssetFormat {

enum class AnimationClip3DPlaybackMode : Core::u8 {
    Once = 1,
    Loop = 2,
    PingPong = 3,
};

enum class AnimationChannel : Core::u8 {
    Invalid = 0,
    Translation = 1,
    Rotation = 2,
    Scale = 3,
};

// v1 supports LINEAR and STEP only. CUBICSPLINE deliberately has no enumerator so it
// cannot be encoded even by accident; the cook rejects it explicitly.
enum class AnimationInterpolation : Core::u8 {
    Invalid = 0,
    Linear = 1,
    Step = 2,
};

// AnimationClip3D cooked payload schema v1 (little-endian, after CookedAsset header).
// Layout:
//   u16 schemaVersion        (=1)
//   u8  playbackMode         (Once/Loop/PingPong)
//   u8  flags                (=0 reserved)
//   u16 jointCount           (1..MaxJointCount)
//   u16 trackCount           (1..MaxTracks)
//   u32 totalKeyframeCount   (1..MaxTotalKeyframes)
//   u32 totalValueFloatCount (1..MaxTotalValueFloats; must match the track channels)
//   f32 durationSeconds      (positive finite; exactly the max last-key time)
//   u32 reserved0/1/2        (=0)
//   AnimationTrackWire[trackCount] (16B each):
//     u16 jointIndex     (< jointCount)
//     u8  channel        (Translation/Rotation/Scale)
//     u8  interpolation  (Linear/Step)
//     u16 keyCount       (1..MaxKeyframesPerTrack)
//     u16 reserved       (=0)
//     u32 keyStartIndex     // exclusive scan over keyCount
//     u32 valueStartIndex   // exclusive scan over keyCount * componentCount
//   f32 times[totalKeyframeCount]
//   f32 values[totalValueFloatCount]
//
// Tracks are strictly increasing by (jointIndex, channel), and their key/value ranges
// partition the times and values blocks exactly. Both properties make the encoding
// canonical, which is what content-hash determinism requires.
//
// A clip has no SkinnedMesh dependency: glTF animations target nodes and one animation
// may drive several skins. It carries its own jointCount, and Animator3D rejects a
// bind where clip.jointCount != skinnedMesh.jointCount.
//
// Rotation tracks with Linear interpolation mean SLERP at runtime;
// linear-then-normalize and SLERP differ visibly across wide-angle keys.
namespace AnimationClip3DWire {
inline constexpr Core::u16 SchemaVersion = 1;
inline constexpr Core::u32 HeaderBytes = 32;
inline constexpr Core::u32 TrackBytes = 16;
inline constexpr Core::u16 MaxJointCount = SkinnedMeshWire::MaxJointCount;
inline constexpr Core::u16 MaxChannelsPerJoint = 3;
inline constexpr Core::u16 MaxTracks = 768;
inline constexpr Core::u16 MaxKeyframesPerTrack = 4096;
inline constexpr Core::u32 MaxTotalKeyframes = 262'144;
inline constexpr Core::u32 MaxTotalValueFloats = 1'048'576;
// Beyond an hour an f32 timestamp stops resolving sub-millisecond steps.
inline constexpr float MaxDurationSeconds = 3600.0F;

// Every block starts on a 4-byte boundary, so no padding is encoded between them.
static_assert(HeaderBytes % 4 == 0);
static_assert(TrackBytes % 4 == 0);
// trackCount is encoded as u16 and at most one track exists per (joint, channel) pair.
static_assert(MaxTracks == static_cast<Core::u16>(MaxJointCount * MaxChannelsPerJoint));
static_assert(MaxTracks <= 0xFFFFU);
// keyCount is encoded as u16.
static_assert(MaxKeyframesPerTrack <= 0xFFFFU);
// Rotation is the widest channel, so the value block cannot exceed 4 floats per key.
static_assert(MaxTotalValueFloats == MaxTotalKeyframes * 4U);
} // namespace AnimationClip3DWire

[[nodiscard]] constexpr Core::u16 animationChannelComponentCount(AnimationChannel channel) noexcept
{
    switch (channel)
    {
    case AnimationChannel::Translation:
    case AnimationChannel::Scale:
        return 3;
    case AnimationChannel::Rotation:
        return 4;
    case AnimationChannel::Invalid:
    default:
        return 0;
    }
}

struct AnimationTrackDesc final {
    Core::u16 jointIndex = 0;
    AnimationChannel channel = AnimationChannel::Invalid;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    // Strictly increasing, non-negative, and within durationSeconds.
    std::span<const float> times{};
    // times.size() * animationChannelComponentCount(channel) floats.
    std::span<const float> values{};
};

struct AnimationClip3DPayloadDesc final {
    AnimationClip3DPlaybackMode playbackMode = AnimationClip3DPlaybackMode::Loop;
    Core::u16 jointCount = 0;
    // Must equal the maximum last-key time across all tracks, bit-exactly.
    float durationSeconds = 0.0F;
    // Strictly increasing by (jointIndex, channel).
    std::span<const AnimationTrackDesc> tracks{};
};

struct AnimationTrackPayloadView final {
    Core::u16 jointIndex = 0;
    AnimationChannel channel = AnimationChannel::Invalid;
    AnimationInterpolation interpolation = AnimationInterpolation::Invalid;
    Core::u32 keyCount = 0;
    Core::u32 keyStartIndex = 0;
    Core::u32 valueStartIndex = 0;
};

struct AnimationClip3DPayloadView final {
    Core::u16 schemaVersion = 0;
    AnimationClip3DPlaybackMode playbackMode = AnimationClip3DPlaybackMode::Loop;
    Core::u16 jointCount = 0;
    Core::u16 trackCount = 0;
    Core::u32 totalKeyframeCount = 0;
    Core::u32 totalValueFloatCount = 0;
    float durationSeconds = 0.0F;
    std::span<const std::byte> tracksBytes{};
    std::span<const float> times{};
    std::span<const float> values{};

    // The wire track record is not layout-compatible with the decoded view, so tracks
    // are decoded on demand rather than exposed as a zero-copy span.
    [[nodiscard]] std::optional<AnimationTrackPayloadView> track(Core::u16 index) const noexcept;

    // Key times of one track, or empty when index is out of range.
    [[nodiscard]] std::span<const float> trackTimes(Core::u16 index) const noexcept;

    // Key values of one track (keyCount * componentCount floats), or empty when out of range.
    [[nodiscard]] std::span<const float> trackValues(Core::u16 index) const noexcept;
};

[[nodiscard]] Core::Result<std::vector<std::byte>>
writeAnimationClip3DPayloadBytes(const AnimationClip3DPayloadDesc& desc);

// Borrows payload bytes from a CookedAssetView / raw payload span. All returned views
// alias into `payload` storage, which must outlive the view unchanged.
[[nodiscard]] Core::Result<AnimationClip3DPayloadView>
parseAnimationClip3DPayload(std::span<const std::byte> payload);

// Convenience: full cooked AnimationClip3D asset file (no dependencies).
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeCookedAnimationClip3DAsset(Core::AssetId assetId, const AnimationClip3DPayloadDesc& desc,
                                TargetPlatform platform = TargetPlatform::WindowsX64);

} // namespace Tina::AssetFormat
