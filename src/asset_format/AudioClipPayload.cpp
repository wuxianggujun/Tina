#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/core/base/Types.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
using Core::u32;
using Core::u64;
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
    const auto wide = static_cast<u64>(a) * static_cast<u64>(b);
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

[[nodiscard]] Core::Status validateStorageCodec(AudioClipStorage storage, AudioClipCodec codec,
                                                u32 encodedBytes) noexcept
{
    if (storage == AudioClipStorage::MemoryPcm)
    {
        if (codec != AudioClipCodec::PcmF32 || encodedBytes != 0)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "MemoryPcm audio clips must use PcmF32 and encodedBytes=0");
        }
        return Core::success();
    }
    if (storage != AudioClipStorage::EncodedStream)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported audio clip storage");
    }
    if (codec != AudioClipCodec::Opus || encodedBytes == 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "EncodedStream audio clips must carry an Ogg Opus payload");
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
    const auto encodedBytes = static_cast<u32>(desc.encoded.size());
    if (desc.encoded.size() > (std::numeric_limits<u32>::max)())
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "audio clip encoded size overflow");
    }
    if (Core::Status status = validateStorageCodec(desc.storage, desc.codec, encodedBytes); !status)
    {
        return Core::failure(status.error());
    }

    u32 sampleCount = 0;
    u32 pcmBytes = 0;
    if (desc.storage == AudioClipStorage::MemoryPcm)
    {
        if (!checkedMultiply(desc.frameCount, desc.channels, sampleCount))
        {
            return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "audio clip sample count overflow");
        }
        if (!checkedMultiply(sampleCount, static_cast<u32>(sizeof(float)), pcmBytes))
        {
            return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "audio clip pcm byte size overflow");
        }
        if (static_cast<u64>(pcmBytes) > AudioClipWire::MaxMemoryPcmBytes)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "MemoryPcm audio clip exceeds the 16 MiB decoded budget");
        }
        if (desc.interleavedPcm.size() != sampleCount || !desc.encoded.empty())
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip pcm buffer size mismatch");
        }
        for (const float sample : desc.interleavedPcm)
        {
            if (!std::isfinite(sample))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "audio clip PCM samples must be finite");
            }
        }
    }
    else if (!desc.interleavedPcm.empty() || desc.encoded.empty() || desc.encoded.data() == nullptr)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "EncodedStream audio clip requires encoded bytes and no PCM");
    }

    const u32 payloadBytes = desc.storage == AudioClipStorage::MemoryPcm ? pcmBytes : encodedBytes;
    try
    {
        std::vector<std::byte> bytes(AudioClipWire::HeaderBytes + payloadBytes, std::byte{0});
        writeU16(bytes, 0U, AudioClipWire::SchemaVersion);
        writeU16(bytes, 2U, desc.channels);
        writeU32(bytes, 4U, desc.sampleRate);
        writeU32(bytes, 8U, desc.frameCount);
        writeU16(bytes, 12U, static_cast<u16>(desc.storage));
        writeU16(bytes, 14U, static_cast<u16>(desc.codec));
        writeU32(bytes, 16U, encodedBytes);
        writeU32(bytes, 20U, 0U);
        if (payloadBytes != 0)
        {
            const void* source = desc.storage == AudioClipStorage::MemoryPcm
                                     ? static_cast<const void*>(desc.interleavedPcm.data())
                                     : static_cast<const void*>(desc.encoded.data());
            std::memcpy(bytes.data() + AudioClipWire::HeaderBytes, source, payloadBytes);
        }
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
    view.storage = static_cast<AudioClipStorage>(readU16(payload, 12U));
    view.codec = static_cast<AudioClipCodec>(readU16(payload, 14U));
    const u32 encodedBytes = readU32(payload, 16U);
    const u32 reserved = readU32(payload, 20U);
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
    if (Core::Status status = validateStorageCodec(view.storage, view.codec, encodedBytes); !status)
    {
        return Core::failure(status.error());
    }

    if (view.storage == AudioClipStorage::MemoryPcm)
    {
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
        if (static_cast<u64>(pcmBytes) > AudioClipWire::MaxMemoryPcmBytes)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "MemoryPcm audio clip exceeds the 16 MiB decoded budget");
        }
        if (payload.size() != AudioClipWire::HeaderBytes + pcmBytes)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip payload size mismatch");
        }
        const auto pcmAddress = reinterpret_cast<Tina::Core::uintptr>(
            payload.data() + AudioClipWire::HeaderBytes);
        if ((pcmAddress % alignof(float)) != 0U)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip pcm alignment invalid");
        }
        const auto* samples = reinterpret_cast<const float*>(payload.data() + AudioClipWire::HeaderBytes);
        view.interleavedPcm = std::span<const float>{samples, sampleCount};
        for (const float sample : view.interleavedPcm)
        {
            if (!std::isfinite(sample))
            {
                return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                     "audio clip PCM samples must be finite");
            }
        }
        return view;
    }

    if (payload.size() != AudioClipWire::HeaderBytes + encodedBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "audio clip payload size mismatch");
    }
    view.encoded = payload.subspan(AudioClipWire::HeaderBytes, encodedBytes);
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
