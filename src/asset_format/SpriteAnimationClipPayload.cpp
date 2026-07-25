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
    return SpriteAnimationFramePayloadView{
        .spriteDependencyIndex = readU32(framesBytes, offset),
        .durationSeconds = readF32(framesBytes, offset + sizeof(u32)),
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
    const usize payloadBytes = SpriteAnimationClipWire::HeaderBytes +
                               static_cast<usize>(frameCount) * SpriteAnimationClipWire::FrameBytes;
    try
    {
        std::vector<std::byte> payload(payloadBytes, std::byte{0});
        writeU16(payload, 0U, SpriteAnimationClipWire::SchemaVersion);
        writeU8(payload, 2U, static_cast<u8>(desc.playbackMode));
        writeU8(payload, 3U, 0U);
        writeU32(payload, 4U, frameCount);
        writeU32(payload, 8U, dependencyCount);
        writeU32(payload, 12U, 0U);

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
    };
    if (view.schemaVersion != SpriteAnimationClipWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema,
                             "unsupported sprite animation payload schema");
    }
    if (!isKnownPlaybackMode(view.playbackMode) || readU8(payload, 3U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "unsupported sprite animation mode or flags");
    }
    if (readU32(payload, 12U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "sprite animation reserved field must be zero");
    }
    if (view.frameCount == 0U || view.frameCount > SpriteAnimationClipWire::MaxFrames ||
        view.spriteDependencyCount == 0U ||
        view.spriteDependencyCount > Wire::MaxDependenciesPerAsset ||
        view.spriteDependencyCount > view.frameCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "sprite animation frame/dependency counts are invalid");
    }

    const usize expectedBytes = SpriteAnimationClipWire::HeaderBytes +
                                static_cast<usize>(view.frameCount) * SpriteAnimationClipWire::FrameBytes;
    if (payload.size() != expectedBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "sprite animation payload size mismatch");
    }
    view.framesBytes = payload.subspan(SpriteAnimationClipWire::HeaderBytes);
    for (u32 index = 0; index < view.frameCount; ++index)
    {
        const auto frame = view.frame(index);
        if (!frame || frame->spriteDependencyIndex >= view.spriteDependencyCount)
        {
            return Core::failure(AssetFormatErrorCode::InvalidDependency,
                                 "sprite animation frame dependency index is out of bounds");
        }
        if (!(frame->durationSeconds > 0.0F) || !std::isfinite(frame->durationSeconds))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "sprite animation frame duration is invalid");
        }
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
