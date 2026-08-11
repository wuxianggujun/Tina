#include <tina/asset_format/SpriteAnimationClipPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <new>
#include <utility>

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
    for (usize index = 0; index < 4U; ++index)
    {
        value |= static_cast<u32>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
}

[[nodiscard]] float readF32(std::span<const std::byte> bytes, usize offset) noexcept
{
    return std::bit_cast<float>(readU32(bytes, offset));
}

void writeU8(std::vector<std::byte>& bytes, usize offset, u8 value)
{
    bytes.at(offset) = static_cast<std::byte>(value);
}

void writeU16(std::vector<std::byte>& bytes, usize offset, u16 value)
{
    writeU8(bytes, offset, static_cast<u8>(value & 0xFFU));
    writeU8(bytes, offset + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
}

void writeU32(std::vector<std::byte>& bytes, usize offset, u32 value)
{
    for (usize index = 0; index < 4U; ++index)
    {
        writeU8(bytes, offset + index, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}

void writeF32(std::vector<std::byte>& bytes, usize offset, float value)
{
    writeU32(bytes, offset, std::bit_cast<u32>(value));
}

// Normalized event offsets are stored as u16 fixed-point: 0 maps to 0.0 and 65535 to 1.0.
constexpr u16 EventOffsetScale = 65535U;

[[nodiscard]] constexpr bool isKnownPlaybackMode(SpriteAnimationPlaybackMode mode) noexcept
{
    return mode >= SpriteAnimationPlaybackMode::Once && mode <= SpriteAnimationPlaybackMode::PingPong;
}

[[nodiscard]] Core::Status validateDesc(const SpriteAnimationClipPayloadDesc& desc) noexcept
{
    if (!isKnownPlaybackMode(desc.playbackMode))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "unsupported sprite animation playback mode");
    }
    if (desc.frames.empty())
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "sprite animation clip requires at least one frame");
    }
    if (desc.frames.size() > SpriteAnimationClipWire::MaxFrames)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "sprite animation frame count exceeds MaxFrames");
    }

    u32 totalEventCount = 0;
    for (const auto& frame : desc.frames)
    {
        if (!frame.spriteId)
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                                 "sprite animation frame requires a Sprite AssetId");
        }
        if (!(frame.durationSeconds > 0.0F) || !std::isfinite(frame.durationSeconds))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "sprite animation frame duration must be positive finite");
        }
        if (frame.events.size() > SpriteAnimationClipWire::MaxEventsPerFrame)
        {
            return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                                 "frame event count exceeds MaxEventsPerFrame (64)");
        }

        totalEventCount += static_cast<u32>(frame.events.size());
        if (totalEventCount > SpriteAnimationClipWire::MaxTotalEvents)
        {
            return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                                 "total event count exceeds MaxTotalEvents (16384)");
        }

        for (usize eventIndex = 0; eventIndex < frame.events.size(); ++eventIndex)
        {
            const auto& event = frame.events[eventIndex];
            if (event.eventTag == 0U)
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "event tag must be non-zero");
            }
            if (!(event.normalizedOffset >= 0.0F && event.normalizedOffset <= 1.0F) ||
                !std::isfinite(event.normalizedOffset))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "event offset must be in [0.0, 1.0]");
            }
            if (eventIndex > 0 &&
                frame.events[eventIndex - 1].normalizedOffset > event.normalizedOffset)
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "frame events must be sorted by offset");
            }
        }
    }
    return Core::success();
}

} // namespace

std::optional<SpriteAnimationFramePayloadView>
SpriteAnimationClipPayloadView::frame(Core::u32 index) const noexcept
{
    if (index >= frameCount)
    {
        return std::nullopt;
    }
    const usize offset = static_cast<usize>(index) * SpriteAnimationClipWire::FrameBytes;
    if (offset + SpriteAnimationClipWire::FrameBytes > framesBytes.size())
    {
        return std::nullopt;
    }

    const u32 spriteDependencyIndex = readU32(framesBytes, offset);
    const float durationSeconds = readF32(framesBytes, offset + sizeof(u32));
    const u16 eventStartIndex = readU16(framesBytes, offset + 2U * sizeof(u32));
    const u16 eventCount = readU16(framesBytes, offset + 2U * sizeof(u32) + sizeof(u16));

    // parseSpriteAnimationClipPayload already rejects out-of-range ranges; recheck so that
    // hand-built views cannot hand callers an event range that overruns the event block.
    if (static_cast<u32>(eventStartIndex) + static_cast<u32>(eventCount) > totalEventCount)
    {
        return std::nullopt;
    }

    return SpriteAnimationFramePayloadView{
        .spriteDependencyIndex = spriteDependencyIndex,
        .durationSeconds = durationSeconds,
        .eventStartIndex = eventStartIndex,
        .eventCount = eventCount,
    };
}

std::optional<SpriteAnimationEventPayloadView>
SpriteAnimationClipPayloadView::event(Core::u32 index) const noexcept
{
    if (index >= totalEventCount)
    {
        return std::nullopt;
    }
    const usize offset = static_cast<usize>(index) * SpriteAnimationClipWire::EventBytes;
    if (offset + SpriteAnimationClipWire::EventBytes > eventsBytes.size())
    {
        return std::nullopt;
    }
    return SpriteAnimationEventPayloadView{
        .eventTag = readU32(eventsBytes, offset),
        .normalizedOffset = static_cast<float>(readU16(eventsBytes, offset + sizeof(u32))) /
                            static_cast<float>(EventOffsetScale),
    };
}

Core::Result<std::vector<CookedAssetWriteDependency>>
makeSpriteAnimationClipDependencies(const SpriteAnimationClipPayloadDesc& desc)
{
    if (auto status = validateDesc(desc); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    try
    {
        std::vector<Core::AssetId> spriteIds;
        spriteIds.reserve(desc.frames.size());
        for (const auto& frame : desc.frames)
        {
            spriteIds.push_back(frame.spriteId);
        }
        std::sort(spriteIds.begin(), spriteIds.end());
        spriteIds.erase(std::unique(spriteIds.begin(), spriteIds.end()), spriteIds.end());

        std::vector<CookedAssetWriteDependency> dependencies;
        dependencies.reserve(spriteIds.size());
        for (const auto spriteId : spriteIds)
        {
            dependencies.push_back(CookedAssetWriteDependency{
                .assetId = spriteId,
                .expectedKind = AssetKind::Sprite,
                .flags = DependencyFlags::Required,
            });
        }
        return dependencies;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "sprite animation dependency allocation failed");
    }
}

Core::Result<std::vector<std::byte>>
writeSpriteAnimationClipPayloadBytes(const SpriteAnimationClipPayloadDesc& desc)
{
    auto dependencies = makeSpriteAnimationClipDependencies(desc);
    if (!dependencies)
    {
        return Core::failure(std::move(dependencies.error()));
    }

    const auto frameCount = static_cast<u32>(desc.frames.size());
    const auto dependencyCount = static_cast<u32>(dependencies->size());

    u32 totalEventCount = 0;
    for (const auto& frame : desc.frames)
    {
        totalEventCount += static_cast<u32>(frame.events.size());
    }

    const usize payloadBytes = SpriteAnimationClipWire::HeaderBytes +
                               static_cast<usize>(frameCount) * SpriteAnimationClipWire::FrameBytes +
                               static_cast<usize>(totalEventCount) * SpriteAnimationClipWire::EventBytes;
    try
    {
        std::vector<std::byte> payload(payloadBytes, std::byte{0});
        writeU16(payload, 0U, SpriteAnimationClipWire::SchemaVersion);
        writeU8(payload, 2U, static_cast<u8>(desc.playbackMode));
        writeU8(payload, 3U, 0U);
        writeU32(payload, 4U, frameCount);
        writeU32(payload, 8U, dependencyCount);
        writeU32(payload, 12U, totalEventCount);
        writeU32(payload, 16U, 0U);
        writeU32(payload, 20U, 0U);
        writeU32(payload, 24U, 0U);
        writeU32(payload, 28U, 0U);

        u32 currentEventIndex = 0;
        for (usize index = 0; index < desc.frames.size(); ++index)
        {
            const auto& frame = desc.frames[index];
            const auto dependency = std::lower_bound(
                dependencies->begin(), dependencies->end(), frame.spriteId,
                [](const CookedAssetWriteDependency& candidate, Core::AssetId spriteId) {
                    return candidate.assetId < spriteId;
                });
            if (dependency == dependencies->end() || dependency->assetId != frame.spriteId)
            {
                return Core::failure(AssetFormatErrorCode::InvalidDependency,
                                     "sprite animation frame dependency mapping failed");
            }
            const auto dependencyIndex = static_cast<u32>(std::distance(dependencies->begin(), dependency));
            const usize offset = SpriteAnimationClipWire::HeaderBytes +
                                 index * SpriteAnimationClipWire::FrameBytes;
            writeU32(payload, offset, dependencyIndex);
            writeF32(payload, offset + sizeof(u32), frame.durationSeconds);
            writeU16(payload, offset + 2U * sizeof(u32), static_cast<u16>(currentEventIndex));
            writeU16(payload, offset + 2U * sizeof(u32) + sizeof(u16), static_cast<u16>(frame.events.size()));

            currentEventIndex += static_cast<u32>(frame.events.size());
        }

        const usize eventsOffset = SpriteAnimationClipWire::HeaderBytes +
                                   static_cast<usize>(frameCount) * SpriteAnimationClipWire::FrameBytes;
        usize eventWriteIndex = 0;
        for (const auto& frame : desc.frames)
        {
            for (const auto& event : frame.events)
            {
                const usize eventOffset = eventsOffset + eventWriteIndex * SpriteAnimationClipWire::EventBytes;
                constexpr auto scale = static_cast<float>(EventOffsetScale);
                const u16 offsetU16 = static_cast<u16>(
                    std::clamp(std::round(event.normalizedOffset * scale), 0.0F, scale));
                writeU32(payload, eventOffset, event.eventTag);
                writeU16(payload, eventOffset + sizeof(u32), offsetU16);
                writeU16(payload, eventOffset + sizeof(u32) + sizeof(u16),
                         SpriteAnimationClipWire::EventNameIndexNone);
                ++eventWriteIndex;
            }
        }

        return payload;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "sprite animation payload allocation failed");
    }
}

Core::Result<SpriteAnimationClipPayloadView>
parseSpriteAnimationClipPayload(std::span<const std::byte> payload)
{
    // schemaVersion occupies bytes [0,2) in every schema, so it is checked before the
    // v2 header-size test. A v1 payload with one or two frames is shorter than the v2
    // header, and reporting InvalidHeader there would hide the real cause.
    if (payload.size() < sizeof(u16))
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader,
                             "sprite animation payload shorter than header");
    }
    if (const u16 schemaVersion = readU16(payload, 0U);
        schemaVersion != SpriteAnimationClipWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema,
                             "unsupported SpriteAnimationClip payload schema; re-author in v2");
    }
    if (payload.size() < SpriteAnimationClipWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader,
                             "sprite animation payload shorter than header");
    }

    SpriteAnimationClipPayloadView view{
        .schemaVersion = readU16(payload, 0U),
        .playbackMode = static_cast<SpriteAnimationPlaybackMode>(readU8(payload, 2U)),
        .frameCount = readU32(payload, 4U),
        .spriteDependencyCount = readU32(payload, 8U),
        .totalEventCount = readU32(payload, 12U),
    };
    if (!isKnownPlaybackMode(view.playbackMode) || readU8(payload, 3U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "unsupported sprite animation mode or flags");
    }
    if (readU32(payload, 16U) != 0U || readU32(payload, 20U) != 0U ||
        readU32(payload, 24U) != 0U || readU32(payload, 28U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "sprite animation reserved fields must be zero");
    }
    if (view.frameCount == 0U || view.frameCount > SpriteAnimationClipWire::MaxFrames ||
        view.spriteDependencyCount == 0U ||
        view.spriteDependencyCount > Wire::MaxDependenciesPerAsset ||
        view.spriteDependencyCount > view.frameCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "sprite animation frame/dependency counts are invalid");
    }
    if (view.totalEventCount > SpriteAnimationClipWire::MaxTotalEvents)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "sprite animation event count exceeds MaxTotalEvents");
    }

    const usize expectedBytes = SpriteAnimationClipWire::HeaderBytes +
                                static_cast<usize>(view.frameCount) * SpriteAnimationClipWire::FrameBytes +
                                static_cast<usize>(view.totalEventCount) * SpriteAnimationClipWire::EventBytes;
    if (payload.size() != expectedBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "sprite animation payload size mismatch");
    }

    const usize framesEnd = SpriteAnimationClipWire::HeaderBytes +
                            static_cast<usize>(view.frameCount) * SpriteAnimationClipWire::FrameBytes;
    view.framesBytes = payload.subspan(SpriteAnimationClipWire::HeaderBytes,
                                       static_cast<usize>(view.frameCount) * SpriteAnimationClipWire::FrameBytes);
    view.eventsBytes = payload.subspan(framesEnd,
                                       static_cast<usize>(view.totalEventCount) * SpriteAnimationClipWire::EventBytes);

    u32 nextExpectedEventIndex = 0;
    for (u32 index = 0; index < view.frameCount; ++index)
    {
        const usize offset = static_cast<usize>(index) * SpriteAnimationClipWire::FrameBytes;
        const u32 spriteDependencyIndex = readU32(view.framesBytes, offset);
        const float durationSeconds = readF32(view.framesBytes, offset + sizeof(u32));
        const u16 eventStartIndex = readU16(view.framesBytes, offset + 2U * sizeof(u32));
        const u16 eventCount = readU16(view.framesBytes, offset + 2U * sizeof(u32) + sizeof(u16));

        if (spriteDependencyIndex >= view.spriteDependencyCount)
        {
            return Core::failure(AssetFormatErrorCode::InvalidDependency,
                                 "sprite animation frame dependency index is out of bounds");
        }
        if (!(durationSeconds > 0.0F) || !std::isfinite(durationSeconds))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "sprite animation frame duration is invalid");
        }
        if (eventCount > SpriteAnimationClipWire::MaxEventsPerFrame)
        {
            return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                                 "frame event count exceeds MaxEventsPerFrame");
        }
        if (static_cast<u32>(eventStartIndex) + static_cast<u32>(eventCount) > view.totalEventCount)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "frame event indices are out of bounds");
        }
        // eventStartIndex is an exclusive scan over frame event counts, so frame ranges must
        // tile the event block exactly. Rejecting gaps keeps the encoding canonical and ensures
        // every event below is reached by validation instead of sitting unchecked.
        if (eventStartIndex != nextExpectedEventIndex)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "frame event start index is not a contiguous exclusive scan");
        }
        nextExpectedEventIndex += eventCount;

        // Compared as raw fixed-point; the u16 to normalized mapping is monotonic, so this is
        // equivalent to comparing decoded offsets without the division.
        u16 previousOffsetFixed = 0;
        for (u16 eventIndex = 0; eventIndex < eventCount; ++eventIndex)
        {
            const usize eventOffset = (static_cast<usize>(eventStartIndex) + eventIndex) *
                                      SpriteAnimationClipWire::EventBytes;
            const u32 eventTag = readU32(view.eventsBytes, eventOffset);
            const u16 offsetFixed = readU16(view.eventsBytes, eventOffset + sizeof(u32));
            const u16 nameStringIndex = readU16(view.eventsBytes, eventOffset + sizeof(u32) + sizeof(u16));

            if (eventTag == 0U)
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "event tag must be non-zero");
            }
            if (nameStringIndex != SpriteAnimationClipWire::EventNameIndexNone)
            {
                return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                                     "event nameStringIndex must be 0xFFFF in schema v2.0");
            }
            if (eventIndex > 0 && offsetFixed < previousOffsetFixed)
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "frame events must be sorted by offset");
            }
            previousOffsetFixed = offsetFixed;
        }
    }
    if (nextExpectedEventIndex != view.totalEventCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "sprite animation events are not fully covered by frames");
    }
    return view;
}

Core::Result<std::vector<std::byte>>
writeCookedSpriteAnimationClipAsset(Core::AssetId assetId,
                                    const SpriteAnimationClipPayloadDesc& desc,
                                    TargetPlatform platform)
{
    if (!assetId)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                             "sprite animation clip requires an asset id");
    }
    auto dependencies = makeSpriteAnimationClipDependencies(desc);
    if (!dependencies)
    {
        return Core::failure(std::move(dependencies.error()));
    }
    for (const auto& dependency : *dependencies)
    {
        if (dependency.assetId == assetId)
        {
            return Core::failure(AssetFormatErrorCode::InvalidDependency,
                                 "sprite animation clip cannot depend on itself");
        }
    }
    auto payload = writeSpriteAnimationClipPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::SpriteAnimationClip,
        .assetTypeVersion = SpriteAnimationClipWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .dependencies = *dependencies,
        .payload = *payload,
        .payloadAlignment = 4,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
