#include <tina/asset_format/AudioClipPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <cstring>
#include <limits>

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

[[nodiscard]] bool checkedMultiply(u32 a, u32 b, u32& out) noexcept
{
    const auto wide = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
    if (wide > (std::numeric_limits<u32>::max)())
    {
        return false;
    }
    out = static_cast<u32>(wide);
    return true;
}

[[nodiscard]] Core::Status validateClipGeometry(u16 channels, u32 sampleRate, u32 frameCount) noexcept
{
    if (channels == 0 || channels > AudioClipWire::MaxChannels)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip channels out of range");
    }
    if (sampleRate < AudioClipWire::MinSampleRate || sampleRate > AudioClipWire::MaxSampleRate)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip sampleRate out of range");
    }
    if (frameCount == 0 || frameCount > AudioClipWire::MaxFrameCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip frameCount out of range");
    }
    return Core::success();
}

} // namespace

Core::Result<std::vector<std::byte>> writeAudioClipPayloadBytes(const AudioClipPayloadDesc& desc)
{
    if (Core::Status status = validateClipGeometry(desc.channels, desc.sampleRate, desc.frameCount); !status)
    {
        return Core::failure(status.error());
    }
    u32 sampleCount = 0;
    if (!checkedMultiply(desc.frameCount, desc.channels, sampleCount))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "audio clip sample count overflow");
    }
    u32 pcmBytes = 0;
    if (!checkedMultiply(sampleCount, static_cast<u32>(sizeof(float)), pcmBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "audio clip pcm byte size overflow");
    }
    if (desc.interleavedPcm.size() != sampleCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip pcm buffer size mismatch");
    }

    try
    {
        std::vector<std::byte> bytes(AudioClipWire::HeaderBytes + pcmBytes, std::byte{0});
        writeU16(bytes, 0U, AudioClipWire::SchemaVersion);
        writeU16(bytes, 2U, desc.channels);
        writeU32(bytes, 4U, desc.sampleRate);
        writeU32(bytes, 8U, desc.frameCount);
        writeU32(bytes, 12U, 0U);
        std::memcpy(bytes.data() + AudioClipWire::HeaderBytes, desc.interleavedPcm.data(), pcmBytes);
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "audio clip payload allocation failed");
    }
}

Core::Result<AudioClipPayloadView> parseAudioClipPayload(std::span<const std::byte> payload)
{
    if (payload.size() < AudioClipWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip payload too short");
    }

    AudioClipPayloadView view{};
    view.schemaVersion = readU16(payload, 0U);
    view.channels = readU16(payload, 2U);
    view.sampleRate = readU32(payload, 4U);
    view.frameCount = readU32(payload, 8U);
    const u32 reserved = readU32(payload, 12U);
    if (view.schemaVersion != AudioClipWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported audio clip schema version");
    }
    if (reserved != 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip reserved field must be zero");
    }
    if (Core::Status status = validateClipGeometry(view.channels, view.sampleRate, view.frameCount); !status)
    {
        return Core::failure(status.error());
    }

    u32 sampleCount = 0;
    if (!checkedMultiply(view.frameCount, view.channels, sampleCount))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "audio clip sample count overflow");
    }
    u32 pcmBytes = 0;
    if (!checkedMultiply(sampleCount, static_cast<u32>(sizeof(float)), pcmBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "audio clip pcm byte size overflow");
    }
    if (payload.size() != AudioClipWire::HeaderBytes + pcmBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip payload size mismatch");
    }
    if ((AudioClipWire::HeaderBytes % alignof(float)) != 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip pcm alignment invalid");
    }

    const auto* samples = reinterpret_cast<const float*>(payload.data() + AudioClipWire::HeaderBytes);
    view.interleavedPcm = std::span<const float>{samples, sampleCount};
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedAudioClipAsset(Core::AssetId assetId, const AudioClipPayloadDesc& desc,
                                                              TargetPlatform platform)
{
    auto payload = writeAudioClipPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(payload.error());
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::AudioClip,
        .assetTypeVersion = AudioClipWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
