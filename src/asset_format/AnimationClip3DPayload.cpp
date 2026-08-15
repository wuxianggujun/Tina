#include <tina/asset_format/AnimationClip3DPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <new>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
using Core::u32;
using Core::u8;
using Core::usize;

[[nodiscard]] u8 readU8(std::span<const std::byte> bytes, usize offset) noexcept
{
    return std::to_integer<u8>(bytes[offset]);
}

[[nodiscard]] u16 readU16(std::span<const std::byte> bytes, usize offset) noexcept
{
    return static_cast<u16>(readU8(bytes, offset)) |
           static_cast<u16>(static_cast<u16>(readU8(bytes, offset + 1U)) << 8U);
}

[[nodiscard]] u32 readU32(std::span<const std::byte> bytes, usize offset) noexcept
{
    u32 value = 0;
    for (usize index = 0; index < sizeof(u32); ++index)
    {
        value |= static_cast<u32>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
}

[[nodiscard]] float readF32(std::span<const std::byte> bytes, usize offset) noexcept
{
    return std::bit_cast<float>(readU32(bytes, offset));
}

void writeU8(std::vector<std::byte>& bytes, usize offset, u8 value) noexcept
{
    bytes[offset] = static_cast<std::byte>(value);
}

void writeU16(std::vector<std::byte>& bytes, usize offset, u16 value) noexcept
{
    writeU8(bytes, offset, static_cast<u8>(value & 0xFFU));
    writeU8(bytes, offset + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
}

void writeU32(std::vector<std::byte>& bytes, usize offset, u32 value) noexcept
{
    for (usize index = 0; index < sizeof(u32); ++index)
    {
        writeU8(bytes, offset + index, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}

void writeF32(std::vector<std::byte>& bytes, usize offset, float value) noexcept
{
    writeU32(bytes, offset, std::bit_cast<u32>(value));
}

[[nodiscard]] constexpr bool isKnownPlaybackMode(AnimationClip3DPlaybackMode mode) noexcept
{
    return mode >= AnimationClip3DPlaybackMode::Once && mode <= AnimationClip3DPlaybackMode::PingPong;
}

[[nodiscard]] constexpr bool isKnownChannel(AnimationChannel channel) noexcept
{
    return channel >= AnimationChannel::Translation && channel <= AnimationChannel::Scale;
}

[[nodiscard]] constexpr bool isKnownInterpolation(AnimationInterpolation interpolation) noexcept
{
    return interpolation == AnimationInterpolation::Linear || interpolation == AnimationInterpolation::Step;
}

[[nodiscard]] constexpr u32 trackSortKey(u16 jointIndex, AnimationChannel channel) noexcept
{
    return (static_cast<u32>(jointIndex) << 8U) | static_cast<u8>(channel);
}

[[nodiscard]] Core::Status validateValues(AnimationChannel channel, std::span<const float> values) noexcept
{
    for (const float value : values)
    {
        if (!std::isfinite(value))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "animation track values must be finite");
        }
    }
    if (channel == AnimationChannel::Rotation)
    {
        for (usize offset = 0; offset < values.size(); offset += 4U)
        {
            const float lengthSquared = values[offset + 0U] * values[offset + 0U] +
                                        values[offset + 1U] * values[offset + 1U] +
                                        values[offset + 2U] * values[offset + 2U] +
                                        values[offset + 3U] * values[offset + 3U];
            if (!std::isfinite(lengthSquared) || !(lengthSquared > 1.0e-12F))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "animation rotation keys must be normalizable");
            }
        }
    }
    return Core::success();
}

struct ClipCounts final {
    u16 trackCount = 0;
    u32 totalKeyframeCount = 0;
    u32 totalValueFloatCount = 0;
};

[[nodiscard]] Core::Status validateHeader(AnimationClip3DPlaybackMode playbackMode, u16 jointCount,
                                          const ClipCounts& counts, float durationSeconds) noexcept
{
    if (!isKnownPlaybackMode(playbackMode))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "unsupported 3D animation playback mode");
    }
    if (jointCount == 0U || jointCount > AnimationClip3DWire::MaxJointCount ||
        counts.trackCount == 0U || counts.trackCount > AnimationClip3DWire::MaxTracks ||
        counts.totalKeyframeCount == 0U ||
        counts.totalKeyframeCount > AnimationClip3DWire::MaxTotalKeyframes ||
        counts.totalValueFloatCount == 0U ||
        counts.totalValueFloatCount > AnimationClip3DWire::MaxTotalValueFloats)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "3D animation counts exceed schema limits");
    }
    if (!std::isfinite(durationSeconds) || !(durationSeconds > 0.0F) ||
        durationSeconds > AnimationClip3DWire::MaxDurationSeconds)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "3D animation duration must be positive, finite and bounded");
    }
    return Core::success();
}

} // namespace

std::optional<AnimationTrackPayloadView> AnimationClip3DPayloadView::track(Core::u16 index) const noexcept
{
    if (index >= trackCount)
    {
        return std::nullopt;
    }
    const usize offset = static_cast<usize>(index) * AnimationClip3DWire::TrackBytes;
    if (offset + AnimationClip3DWire::TrackBytes > tracksBytes.size())
    {
        return std::nullopt;
    }
    return AnimationTrackPayloadView{
        .jointIndex = readU16(tracksBytes, offset + 0U),
        .channel = static_cast<AnimationChannel>(readU8(tracksBytes, offset + 2U)),
        .interpolation = static_cast<AnimationInterpolation>(readU8(tracksBytes, offset + 3U)),
        .keyCount = readU16(tracksBytes, offset + 4U),
        .keyStartIndex = readU32(tracksBytes, offset + 8U),
        .valueStartIndex = readU32(tracksBytes, offset + 12U),
    };
}

std::span<const float> AnimationClip3DPayloadView::trackTimes(Core::u16 index) const noexcept
{
    const auto trackView = track(index);
    if (!trackView || trackView->keyStartIndex > times.size() ||
        trackView->keyCount > times.size() - trackView->keyStartIndex)
    {
        return {};
    }
    return times.subspan(trackView->keyStartIndex, trackView->keyCount);
}

std::span<const float> AnimationClip3DPayloadView::trackValues(Core::u16 index) const noexcept
{
    const auto trackView = track(index);
    if (!trackView)
    {
        return {};
    }
    const usize valueCount = static_cast<usize>(trackView->keyCount) *
                             animationChannelComponentCount(trackView->channel);
    if (trackView->valueStartIndex > values.size() ||
        valueCount > values.size() - trackView->valueStartIndex)
    {
        return {};
    }
    return values.subspan(trackView->valueStartIndex, valueCount);
}

Core::Result<std::vector<std::byte>>
writeAnimationClip3DPayloadBytes(const AnimationClip3DPayloadDesc& desc)
{
    if (desc.tracks.size() > AnimationClip3DWire::MaxTracks)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "3D animation track count exceeds MaxTracks");
    }

    ClipCounts counts{.trackCount = static_cast<u16>(desc.tracks.size())};
    u32 previousSortKey = 0;
    bool hasPrevious = false;
    float maximumLastKeyTime = 0.0F;
    for (const AnimationTrackDesc& trackDesc : desc.tracks)
    {
        const u16 componentCount = animationChannelComponentCount(trackDesc.channel);
        if (trackDesc.jointIndex >= desc.jointCount || !isKnownChannel(trackDesc.channel) ||
            !isKnownInterpolation(trackDesc.interpolation))
        {
            return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                                 "3D animation track target or interpolation is unsupported");
        }
        if (trackDesc.times.empty() || trackDesc.times.size() > AnimationClip3DWire::MaxKeyframesPerTrack)
        {
            return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                                 "3D animation key count exceeds per-track limits");
        }
        if (trackDesc.values.size() != trackDesc.times.size() * componentCount)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "3D animation value count does not match channel shape");
        }
        const u32 sortKey = trackSortKey(trackDesc.jointIndex, trackDesc.channel);
        if (hasPrevious && sortKey <= previousSortKey)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "3D animation tracks must be strictly ordered by joint and channel");
        }
        previousSortKey = sortKey;
        hasPrevious = true;

        float previousTime = -1.0F;
        for (const float time : trackDesc.times)
        {
            if (!std::isfinite(time) || !(time >= 0.0F) || !(time > previousTime))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "3D animation key times must be finite and strictly increasing");
            }
            previousTime = time;
        }
        maximumLastKeyTime = (std::max)(maximumLastKeyTime, trackDesc.times.back());
        if (Core::Status status = validateValues(trackDesc.channel, trackDesc.values); !status)
        {
            return Core::failure(status.error());
        }
        if (trackDesc.times.size() > AnimationClip3DWire::MaxTotalKeyframes - counts.totalKeyframeCount ||
            trackDesc.values.size() > AnimationClip3DWire::MaxTotalValueFloats - counts.totalValueFloatCount)
        {
            return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                                 "3D animation aggregate key budget exceeded");
        }
        counts.totalKeyframeCount += static_cast<u32>(trackDesc.times.size());
        counts.totalValueFloatCount += static_cast<u32>(trackDesc.values.size());
    }
    if (Core::Status status = validateHeader(desc.playbackMode, desc.jointCount, counts, desc.durationSeconds);
        !status)
    {
        return Core::failure(status.error());
    }
    if (maximumLastKeyTime != desc.durationSeconds)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "3D animation duration must equal the maximum last-key time");
    }

    const usize trackBytes = static_cast<usize>(counts.trackCount) * AnimationClip3DWire::TrackBytes;
    const usize timeBytes = static_cast<usize>(counts.totalKeyframeCount) * sizeof(float);
    const usize valueBytes = static_cast<usize>(counts.totalValueFloatCount) * sizeof(float);
    const usize totalBytes = AnimationClip3DWire::HeaderBytes + trackBytes + timeBytes + valueBytes;
    if (totalBytes > Wire::MaxPayloadBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "3D animation payload exceeds cooked payload limit");
    }

    try
    {
        std::vector<std::byte> bytes(totalBytes, std::byte{0});
        writeU16(bytes, 0U, AnimationClip3DWire::SchemaVersion);
        writeU8(bytes, 2U, static_cast<u8>(desc.playbackMode));
        writeU8(bytes, 3U, 0U);
        writeU16(bytes, 4U, desc.jointCount);
        writeU16(bytes, 6U, counts.trackCount);
        writeU32(bytes, 8U, counts.totalKeyframeCount);
        writeU32(bytes, 12U, counts.totalValueFloatCount);
        writeF32(bytes, 16U, desc.durationSeconds);
        writeU32(bytes, 20U, 0U);
        writeU32(bytes, 24U, 0U);
        writeU32(bytes, 28U, 0U);

        u32 keyStart = 0;
        u32 valueStart = 0;
        usize timeOffset = AnimationClip3DWire::HeaderBytes + trackBytes;
        usize valueOffset = timeOffset + timeBytes;
        for (usize index = 0; index < desc.tracks.size(); ++index)
        {
            const AnimationTrackDesc& trackDesc = desc.tracks[index];
            const usize trackOffset = AnimationClip3DWire::HeaderBytes +
                                      index * AnimationClip3DWire::TrackBytes;
            writeU16(bytes, trackOffset + 0U, trackDesc.jointIndex);
            writeU8(bytes, trackOffset + 2U, static_cast<u8>(trackDesc.channel));
            writeU8(bytes, trackOffset + 3U, static_cast<u8>(trackDesc.interpolation));
            writeU16(bytes, trackOffset + 4U, static_cast<u16>(trackDesc.times.size()));
            writeU16(bytes, trackOffset + 6U, 0U);
            writeU32(bytes, trackOffset + 8U, keyStart);
            writeU32(bytes, trackOffset + 12U, valueStart);
            for (const float time : trackDesc.times)
            {
                writeF32(bytes, timeOffset, time);
                timeOffset += sizeof(float);
            }
            for (const float value : trackDesc.values)
            {
                writeF32(bytes, valueOffset, value);
                valueOffset += sizeof(float);
            }
            keyStart += static_cast<u32>(trackDesc.times.size());
            valueStart += static_cast<u32>(trackDesc.values.size());
        }
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "3D animation payload allocation failed");
    }
}

Core::Result<AnimationClip3DPayloadView>
parseAnimationClip3DPayload(std::span<const std::byte> payload)
{
    if (payload.size() < AnimationClip3DWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "3D animation payload shorter than header");
    }
    AnimationClip3DPayloadView view{
        .schemaVersion = readU16(payload, 0U),
        .playbackMode = static_cast<AnimationClip3DPlaybackMode>(readU8(payload, 2U)),
        .jointCount = readU16(payload, 4U),
        .trackCount = readU16(payload, 6U),
        .totalKeyframeCount = readU32(payload, 8U),
        .totalValueFloatCount = readU32(payload, 12U),
        .durationSeconds = readF32(payload, 16U),
    };
    if (view.schemaVersion != AnimationClip3DWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema,
                             "unsupported AnimationClip3D payload schema");
    }
    if (readU8(payload, 3U) != 0U || readU32(payload, 20U) != 0U ||
        readU32(payload, 24U) != 0U || readU32(payload, 28U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "3D animation flags and reserved fields must be zero");
    }
    const ClipCounts counts{
        .trackCount = view.trackCount,
        .totalKeyframeCount = view.totalKeyframeCount,
        .totalValueFloatCount = view.totalValueFloatCount,
    };
    if (Core::Status status = validateHeader(view.playbackMode, view.jointCount, counts, view.durationSeconds);
        !status)
    {
        return Core::failure(status.error());
    }

    const usize trackBytes = static_cast<usize>(view.trackCount) * AnimationClip3DWire::TrackBytes;
    const usize timeBytes = static_cast<usize>(view.totalKeyframeCount) * sizeof(float);
    const usize valueBytes = static_cast<usize>(view.totalValueFloatCount) * sizeof(float);
    const usize expectedBytes = AnimationClip3DWire::HeaderBytes + trackBytes + timeBytes + valueBytes;
    if (payload.size() != expectedBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "3D animation payload size mismatch");
    }
    view.tracksBytes = payload.subspan(AnimationClip3DWire::HeaderBytes, trackBytes);
    const usize timesOffset = AnimationClip3DWire::HeaderBytes + trackBytes;
    const usize valuesOffset = timesOffset + timeBytes;
    const auto timesAddress = reinterpret_cast<std::uintptr_t>(payload.data() + timesOffset);
    const auto valuesAddress = reinterpret_cast<std::uintptr_t>(payload.data() + valuesOffset);
    if ((timesAddress % alignof(float)) != 0U || (valuesAddress % alignof(float)) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "3D animation float block alignment invalid");
    }
    view.times = std::span<const float>{reinterpret_cast<const float*>(payload.data() + timesOffset),
                                        view.totalKeyframeCount};
    view.values = std::span<const float>{reinterpret_cast<const float*>(payload.data() + valuesOffset),
                                         view.totalValueFloatCount};

    u32 expectedKeyStart = 0;
    u32 expectedValueStart = 0;
    u32 previousSortKey = 0;
    bool hasPrevious = false;
    float maximumLastKeyTime = 0.0F;
    for (u16 index = 0; index < view.trackCount; ++index)
    {
        const usize offset = static_cast<usize>(index) * AnimationClip3DWire::TrackBytes;
        const auto trackView = view.track(index);
        if (!trackView || readU16(view.tracksBytes, offset + 6U) != 0U ||
            trackView->jointIndex >= view.jointCount || !isKnownChannel(trackView->channel) ||
            !isKnownInterpolation(trackView->interpolation))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "3D animation track record is invalid");
        }
        if (trackView->keyCount == 0U ||
            trackView->keyCount > AnimationClip3DWire::MaxKeyframesPerTrack ||
            trackView->keyStartIndex != expectedKeyStart ||
            trackView->valueStartIndex != expectedValueStart)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "3D animation track ranges are not canonical exclusive scans");
        }
        const u32 componentCount = animationChannelComponentCount(trackView->channel);
        if (trackView->keyCount > view.totalKeyframeCount - expectedKeyStart ||
            static_cast<u32>(trackView->keyCount) * componentCount >
                view.totalValueFloatCount - expectedValueStart)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "3D animation track range exceeds payload blocks");
        }
        const u32 sortKey = trackSortKey(trackView->jointIndex, trackView->channel);
        if (hasPrevious && sortKey <= previousSortKey)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "3D animation tracks are not strictly ordered");
        }
        previousSortKey = sortKey;
        hasPrevious = true;

        const auto trackTimeValues = view.trackTimes(index);
        float previousTime = -1.0F;
        for (const float time : trackTimeValues)
        {
            if (!std::isfinite(time) || !(time >= 0.0F) || !(time > previousTime))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "3D animation key times are invalid");
            }
            previousTime = time;
        }
        maximumLastKeyTime = (std::max)(maximumLastKeyTime, trackTimeValues.back());
        if (Core::Status status = validateValues(trackView->channel, view.trackValues(index)); !status)
        {
            return Core::failure(status.error());
        }
        expectedKeyStart += trackView->keyCount;
        expectedValueStart += static_cast<u32>(trackView->keyCount) * componentCount;
    }
    if (expectedKeyStart != view.totalKeyframeCount || expectedValueStart != view.totalValueFloatCount ||
        maximumLastKeyTime != view.durationSeconds)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "3D animation blocks are not fully partitioned or duration mismatches");
    }
    return view;
}

Core::Result<std::vector<std::byte>>
writeCookedAnimationClip3DAsset(Core::AssetId assetId, const AnimationClip3DPayloadDesc& desc,
                               TargetPlatform platform)
{
    auto payload = writeAnimationClip3DPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(payload.error());
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::AnimationClip3D,
        .assetTypeVersion = AnimationClip3DWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
