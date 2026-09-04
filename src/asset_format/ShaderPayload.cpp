#include <tina/asset_format/ShaderPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <cstring>
#include <limits>
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
    return static_cast<u16>(static_cast<u16>(readU8(bytes, offset)) |
                            static_cast<u16>(static_cast<u16>(readU8(bytes, offset + 1U)) << 8U));
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

void writeU16(std::vector<std::byte>& bytes, usize offset, u16 value) noexcept
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void writeU32(std::vector<std::byte>& bytes, usize offset, u32 value) noexcept
{
    for (usize index = 0; index < 4U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] bool isSupportedShaderKind(ShaderKind kind) noexcept
{
    return kind == ShaderKind::Sprite2D || kind == ShaderKind::Mesh3D;
}

[[nodiscard]] bool isSupportedShaderStage(ShaderStage stage) noexcept
{
    return stage == ShaderStage::Fragment;
}

[[nodiscard]] bool isSupportedShaderBinaryProfile(ShaderBinaryProfile profile) noexcept
{
    switch (profile)
    {
    case ShaderBinaryProfile::Glsl120:
    case ShaderBinaryProfile::SpirV:
    case ShaderBinaryProfile::Dxbc50:
    case ShaderBinaryProfile::Essl300:
        return true;
    case ShaderBinaryProfile::Invalid:
        break;
    }
    return false;
}

// Returns the total blob byte count so the caller does not re-sum a table it just validated.
[[nodiscard]] Core::Result<u32> validateLayout(const ShaderPayloadDesc& desc)
{
    if (!isSupportedShaderKind(desc.shaderKind))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "shader payload kind is not a supported engine program");
    }
    if (!isSupportedShaderStage(desc.stage))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "shader payload stage is not a replaceable pipeline stage");
    }
    if (desc.blobs.empty() || desc.blobs.size() > ShaderWire::MaxBlobCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "shader payload must carry between one and MaxBlobCount binaries");
    }

    u32 blobBytesTotal = 0;
    auto previousProfile = ShaderBinaryProfile::Invalid;
    for (const ShaderBlobDesc& blob : desc.blobs)
    {
        if (!isSupportedShaderBinaryProfile(blob.profile))
        {
            return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                                 "shader payload carries an unsupported renderer profile");
        }
        // Strictly ascending, which rules out duplicates and pins one encoding per binary set.
        if (static_cast<u16>(blob.profile) <= static_cast<u16>(previousProfile))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "shader payload blobs must be sorted by ascending profile");
        }
        previousProfile = blob.profile;

        if (blob.bytes.empty() || blob.bytes.size() > ShaderWire::MaxBlobBytes)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "shader payload blob byte count is outside the schema limit");
        }
        const auto blobBytes = static_cast<u32>(blob.bytes.size());
        if (blobBytesTotal > (std::numeric_limits<u32>::max)() - blobBytes)
        {
            return Core::failure(AssetFormatErrorCode::ArithmeticOverflow,
                                 "shader payload blob byte total overflows");
        }
        blobBytesTotal += blobBytes;
    }

    const usize tableBytes = desc.blobs.size() * ShaderWire::BlobEntryBytes;
    const usize payloadBytes = ShaderWire::HeaderBytes + tableBytes + blobBytesTotal;
    if (payloadBytes > ShaderWire::MaxPayloadBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "shader payload exceeds the schema byte limit");
    }
    return blobBytesTotal;
}

} // namespace

std::string_view shaderBinaryProfileName(ShaderBinaryProfile profile) noexcept
{
    switch (profile)
    {
    case ShaderBinaryProfile::Glsl120:
        return "glsl120";
    case ShaderBinaryProfile::SpirV:
        return "spv";
    case ShaderBinaryProfile::Dxbc50:
        return "dxbc";
    case ShaderBinaryProfile::Essl300:
        return "essl300";
    case ShaderBinaryProfile::Invalid:
        break;
    }
    return {};
}

std::optional<ShaderBinaryProfile> parseShaderBinaryProfileName(std::string_view name) noexcept
{
    if (name == "glsl120")
    {
        return ShaderBinaryProfile::Glsl120;
    }
    if (name == "spv")
    {
        return ShaderBinaryProfile::SpirV;
    }
    if (name == "dxbc")
    {
        return ShaderBinaryProfile::Dxbc50;
    }
    if (name == "essl300")
    {
        return ShaderBinaryProfile::Essl300;
    }
    return std::nullopt;
}

std::span<const std::byte> ShaderPayloadView::blobForProfile(ShaderBinaryProfile profile) const noexcept
{
    for (const ShaderBlobView& blob : blobs())
    {
        if (blob.profile == profile)
        {
            return blob.bytes;
        }
    }
    return {};
}

Core::Result<std::vector<std::byte>> writeShaderPayloadBytes(const ShaderPayloadDesc& desc)
{
    auto blobBytesTotal = validateLayout(desc);
    if (!blobBytesTotal)
    {
        return Core::failure(std::move(blobBytesTotal.error()));
    }
    const auto blobCount = static_cast<u16>(desc.blobs.size());
    const usize tableBytes = static_cast<usize>(blobCount) * ShaderWire::BlobEntryBytes;
    const usize payloadBytes = ShaderWire::HeaderBytes + tableBytes + *blobBytesTotal;
    try
    {
        std::vector<std::byte> bytes(payloadBytes, std::byte{0});
        writeU16(bytes, 0U, ShaderWire::SchemaVersion);
        writeU16(bytes, 2U, static_cast<u16>(desc.shaderKind));
        writeU16(bytes, 4U, static_cast<u16>(desc.stage));
        writeU16(bytes, 6U, blobCount);
        writeU32(bytes, 8U, *blobBytesTotal);
        writeU32(bytes, 12U, 0U);

        usize entryOffset = ShaderWire::HeaderBytes;
        usize blobOffset = ShaderWire::HeaderBytes + tableBytes;
        for (const ShaderBlobDesc& blob : desc.blobs)
        {
            const auto blobBytes = static_cast<u32>(blob.bytes.size());
            writeU16(bytes, entryOffset, static_cast<u16>(blob.profile));
            writeU16(bytes, entryOffset + 2U, 0U);
            writeU32(bytes, entryOffset + 4U, static_cast<u32>(blobOffset));
            writeU32(bytes, entryOffset + 8U, blobBytes);
            std::memcpy(bytes.data() + blobOffset, blob.bytes.data(), blobBytes);
            entryOffset += ShaderWire::BlobEntryBytes;
            blobOffset += blobBytes;
        }
        return bytes;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "shader payload allocation failed");
    }
}

Core::Result<ShaderPayloadView> parseShaderPayload(std::span<const std::byte> payload)
{
    if (payload.size() < ShaderWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader,
                             "shader payload is smaller than its header");
    }
    ShaderPayloadView view{
        .schemaVersion = readU16(payload, 0U),
        .shaderKind = static_cast<ShaderKind>(readU16(payload, 2U)),
        .stage = static_cast<ShaderStage>(readU16(payload, 4U)),
        .blobCount = readU16(payload, 6U),
    };
    if (view.schemaVersion != ShaderWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "unsupported shader payload schema");
    }
    if (readU32(payload, 12U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "shader payload reserved fields must be zero");
    }
    if (!isSupportedShaderKind(view.shaderKind))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "shader payload kind is not a supported engine program");
    }
    if (!isSupportedShaderStage(view.stage))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "shader payload stage is not a replaceable pipeline stage");
    }
    if (view.blobCount == 0U || view.blobCount > ShaderWire::MaxBlobCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "shader payload blob count is outside the schema limit");
    }

    const u32 blobBytesTotal = readU32(payload, 8U);
    const usize tableBytes = static_cast<usize>(view.blobCount) * ShaderWire::BlobEntryBytes;
    const usize expectedBytes = ShaderWire::HeaderBytes + tableBytes + blobBytesTotal;
    if (expectedBytes > ShaderWire::MaxPayloadBytes || payload.size() != expectedBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "shader payload byte count is inconsistent");
    }

    usize entryOffset = ShaderWire::HeaderBytes;
    usize expectedBlobOffset = ShaderWire::HeaderBytes + tableBytes;
    auto previousProfile = ShaderBinaryProfile::Invalid;
    for (u16 index = 0; index < view.blobCount; ++index)
    {
        const auto profile = static_cast<ShaderBinaryProfile>(readU16(payload, entryOffset));
        if (readU16(payload, entryOffset + 2U) != 0U)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "shader payload reserved fields must be zero");
        }
        if (!isSupportedShaderBinaryProfile(profile))
        {
            return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                                 "shader payload carries an unsupported renderer profile");
        }
        if (static_cast<u16>(profile) <= static_cast<u16>(previousProfile))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "shader payload blobs must be sorted by ascending profile");
        }
        previousProfile = profile;

        const u32 byteOffset = readU32(payload, entryOffset + 4U);
        const u32 byteCount = readU32(payload, entryOffset + 8U);
        if (byteCount == 0U || byteCount > ShaderWire::MaxBlobBytes)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "shader payload blob byte count is outside the schema limit");
        }
        // Tightly packed in table order, so the only admissible offset is the running one. A
        // range check alone would accept overlapping blobs that hash differently per cook.
        if (byteOffset != expectedBlobOffset)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "shader payload blob offsets must be tightly packed in table order");
        }
        if (byteCount > payload.size() - byteOffset)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "shader payload blob extends past the payload end");
        }

        view.blobStorage[index] = ShaderBlobView{
            .profile = profile,
            .bytes = payload.subspan(byteOffset, byteCount),
        };
        entryOffset += ShaderWire::BlobEntryBytes;
        expectedBlobOffset += byteCount;
    }
    if (expectedBlobOffset != payload.size())
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "shader payload blob byte total is inconsistent");
    }
    return view;
}

Core::Result<std::vector<std::byte>> writeCookedShaderAsset(Core::AssetId assetId,
                                                            const ShaderPayloadDesc& desc,
                                                            TargetPlatform platform)
{
    auto payload = writeShaderPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::Shader,
        .assetTypeVersion = ShaderWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

} // namespace Tina::AssetFormat
