#include <tina/asset_format/Texture2DPayload.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>

#include <array>
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

[[nodiscard]] bool checkedAdd(u32 a, u32 b, u32& out) noexcept
{
    const auto wide = static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b);
    if (wide > (std::numeric_limits<u32>::max)())
    {
        return false;
    }
    out = static_cast<u32>(wide);
    return true;
}

[[nodiscard]] bool isKnownPixelFormat(Texture2DPixelFormat format) noexcept
{
    switch (format)
    {
    case Texture2DPixelFormat::Rgba8Unorm:
    case Texture2DPixelFormat::Bc1Rgba:
    case Texture2DPixelFormat::Bc3Rgba:
    case Texture2DPixelFormat::Bc7Rgba:
    case Texture2DPixelFormat::Astc4x4Rgba:
        return true;
    case Texture2DPixelFormat::Invalid:
        break;
    }
    return false;
}

[[nodiscard]] bool isKnownColorSpace(Texture2DColorSpace space) noexcept
{
    return space == Texture2DColorSpace::Linear || space == Texture2DColorSpace::Srgb;
}

[[nodiscard]] bool isKnownWrapMode(Texture2DWrapMode mode) noexcept
{
    switch (mode)
    {
    case Texture2DWrapMode::Repeat:
    case Texture2DWrapMode::Mirror:
    case Texture2DWrapMode::Clamp:
    case Texture2DWrapMode::Border:
        return true;
    case Texture2DWrapMode::Invalid:
        break;
    }
    return false;
}

[[nodiscard]] bool isKnownFilterMode(Texture2DFilterMode mode) noexcept
{
    switch (mode)
    {
    case Texture2DFilterMode::Point:
    case Texture2DFilterMode::Linear:
    case Texture2DFilterMode::Anisotropic:
        return true;
    case Texture2DFilterMode::Invalid:
        break;
    }
    return false;
}

[[nodiscard]] bool isKnownMipFilterMode(Texture2DMipFilterMode mode) noexcept
{
    switch (mode)
    {
    case Texture2DMipFilterMode::None:
    case Texture2DMipFilterMode::Point:
    case Texture2DMipFilterMode::Linear:
        return true;
    }
    return false;
}

[[nodiscard]] u16 nextMipExtent(u16 extent) noexcept
{
    return extent > 1U ? static_cast<u16>(extent / 2U) : static_cast<u16>(1);
}

} // namespace

bool isBlockCompressedTexture2DFormat(Texture2DPixelFormat format) noexcept
{
    switch (format)
    {
    case Texture2DPixelFormat::Bc1Rgba:
    case Texture2DPixelFormat::Bc3Rgba:
    case Texture2DPixelFormat::Bc7Rgba:
    case Texture2DPixelFormat::Astc4x4Rgba:
        return true;
    case Texture2DPixelFormat::Rgba8Unorm:
    case Texture2DPixelFormat::Invalid:
        break;
    }
    return false;
}

u32 texture2DLevelByteSize(Texture2DPixelFormat format, u16 width, u16 height) noexcept
{
    if (width == 0 || height == 0 || !isKnownPixelFormat(format))
    {
        return 0;
    }
    if (!isBlockCompressedTexture2DFormat(format))
    {
        u32 size = 0;
        if (!checkedMultiply(width, height, size) || !checkedMultiply(size, 4U, size))
        {
            return 0;
        }
        return size;
    }

    // Compressed levels are whole 4x4 blocks, so a 1x1 tail level still costs one
    // full block. Rounding down would under-allocate the smallest mips.
    const u32 blocksX = (static_cast<u32>(width) + 3U) / 4U;
    const u32 blocksY = (static_cast<u32>(height) + 3U) / 4U;
    const u32 bytesPerBlock = format == Texture2DPixelFormat::Bc1Rgba ? 8U : 16U;
    u32 blocks = 0;
    u32 size = 0;
    if (!checkedMultiply(blocksX, blocksY, blocks) || !checkedMultiply(blocks, bytesPerBlock, size))
    {
        return 0;
    }
    return size;
}

u8 texture2DFullMipLevelCount(u16 width, u16 height) noexcept
{
    if (width == 0 || height == 0 || width > Texture2DWire::MaxDimension ||
        height > Texture2DWire::MaxDimension)
    {
        return 0;
    }
    u8 count = 1;
    u16 levelWidth = width;
    u16 levelHeight = height;
    while (levelWidth > 1U || levelHeight > 1U)
    {
        levelWidth = nextMipExtent(levelWidth);
        levelHeight = nextMipExtent(levelHeight);
        ++count;
    }
    return count;
}

Core::Status validateTexture2DSamplerDesc(const Texture2DSamplerDesc& sampler) noexcept
{
    if (!isKnownWrapMode(sampler.wrapU) || !isKnownWrapMode(sampler.wrapV))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "texture sampler wrap mode is not a known value");
    }
    if (!isKnownFilterMode(sampler.minFilter) || !isKnownFilterMode(sampler.magFilter) ||
        !isKnownMipFilterMode(sampler.mipFilter))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "texture sampler filter mode is not a known value");
    }
    const bool anisotropicMin = sampler.minFilter == Texture2DFilterMode::Anisotropic;
    const bool anisotropicMag = sampler.magFilter == Texture2DFilterMode::Anisotropic;
    // Several bgfx renderers normalize anisotropy to both stages when either bit is
    // present. Requiring the pair keeps the cooked contract portable across backends.
    if (anisotropicMin != anisotropicMag)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "texture sampler anisotropic min and mag filters must be enabled together");
    }
    return Core::success();
}

Core::Result<std::vector<std::byte>> writeTexture2DPayloadBytes(const Texture2DPayloadDesc& desc)
{
    if (!isKnownPixelFormat(desc.pixelFormat))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported texture pixel format");
    }
    if (!isKnownColorSpace(desc.colorSpace))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported texture color space");
    }
    if (auto status = validateTexture2DSamplerDesc(desc.sampler); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (desc.levels.empty() || desc.levels.size() > Texture2DWire::MaxLevelCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "texture payload requires 1 to 15 mip levels");
    }
    if ((desc.levels.size() == 1U && desc.sampler.mipFilter != Texture2DMipFilterMode::None) ||
        (desc.levels.size() > 1U && desc.sampler.mipFilter == Texture2DMipFilterMode::None))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "texture mip filter must match whether a mip chain is present");
    }

    const Texture2DLevelDesc& base = desc.levels.front();
    if (base.width == 0 || base.height == 0 || base.width > Texture2DWire::MaxDimension ||
        base.height > Texture2DWire::MaxDimension)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture dimensions out of range");
    }
    // A partial chain is rejected rather than padded: a backend told "5 levels" would
    // sample garbage for the missing tail, and which levels are missing is invisible.
    if (desc.levels.size() > 1U &&
        desc.levels.size() != texture2DFullMipLevelCount(base.width, base.height))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "texture mip chain must run from the base level down to 1x1");
    }

    u32 levelBytes = 0;
    u16 expectedWidth = base.width;
    u16 expectedHeight = base.height;
    for (const Texture2DLevelDesc& level : desc.levels)
    {
        if (level.width != expectedWidth || level.height != expectedHeight)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "texture mip level extent does not halve the previous level");
        }
        const u32 expectedSize = texture2DLevelByteSize(desc.pixelFormat, level.width, level.height);
        if (expectedSize == 0)
        {
            return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "texture level size overflow");
        }
        if (level.bytes.size() != expectedSize)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "texture level byte count does not match its extent and format");
        }
        if (!checkedAdd(levelBytes, expectedSize, levelBytes))
        {
            return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "texture payload size overflow");
        }
        expectedWidth = nextMipExtent(expectedWidth);
        expectedHeight = nextMipExtent(expectedHeight);
    }

    const auto levelCount = static_cast<u8>(desc.levels.size());
    u32 descriptorBytes = 0;
    u32 totalBytes = 0;
    if (!checkedMultiply(levelCount, Texture2DWire::LevelDescriptorBytes, descriptorBytes) ||
        !checkedAdd(Texture2DWire::HeaderBytes, descriptorBytes, totalBytes) ||
        !checkedAdd(totalBytes, levelBytes, totalBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "texture payload size overflow");
    }
    if (totalBytes > Wire::MaxPayloadBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "texture payload exceeds the global payload byte limit");
    }

    try
    {
        std::vector<std::byte> bytes(totalBytes, std::byte{0});
        writeU16(bytes, 0U, Texture2DWire::SchemaVersion);
        writeU16(bytes, 2U, base.width);
        writeU16(bytes, 4U, base.height);
        writeU16(bytes, 6U, static_cast<u16>(desc.pixelFormat));
        writeU8(bytes, 8U, static_cast<u8>(desc.colorSpace));
        writeU8(bytes, 9U, levelCount);
        writeU8(bytes, 10U, static_cast<u8>(desc.sampler.wrapU));
        writeU8(bytes, 11U, static_cast<u8>(desc.sampler.wrapV));
        writeU8(bytes, 12U, static_cast<u8>(desc.sampler.minFilter));
        writeU8(bytes, 13U, static_cast<u8>(desc.sampler.magFilter));
        writeU8(bytes, 14U, static_cast<u8>(desc.sampler.mipFilter));
        writeU8(bytes, 15U, 0U);
        writeU32(bytes, 16U, levelBytes);
        writeU32(bytes, 20U, 0U);
        writeU32(bytes, 24U, 0U);
        writeU32(bytes, 28U, 0U);

        u32 dataOffset = Texture2DWire::HeaderBytes + descriptorBytes;
        for (usize index = 0; index < desc.levels.size(); ++index)
        {
            const Texture2DLevelDesc& level = desc.levels[index];
            const usize descriptorOffset =
                Texture2DWire::HeaderBytes + index * Texture2DWire::LevelDescriptorBytes;
            const auto levelSize = static_cast<u32>(level.bytes.size());
            writeU32(bytes, descriptorOffset, dataOffset);
            writeU32(bytes, descriptorOffset + 4U, levelSize);
            if (levelSize > 0)
            {
                std::memcpy(bytes.data() + dataOffset, level.bytes.data(), levelSize);
            }
            dataOffset += levelSize;
        }
        return bytes;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "texture payload allocation failed");
    }
}

Core::Result<std::vector<std::byte>>
writeTexture2DPayloadBytesRgba8(Core::u16 width, Core::u16 height,
                                std::span<const std::byte> rgba8Pixels,
                                Texture2DColorSpace colorSpace)
{
    const std::array<Texture2DLevelDesc, 1> levels{
        Texture2DLevelDesc{.width = width, .height = height, .bytes = rgba8Pixels}};
    return writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .pixelFormat = Texture2DPixelFormat::Rgba8Unorm,
        .colorSpace = colorSpace,
        // A single-level texture has no mip chain to select from, and claiming Linear
        // mip selection over one level would advertise filtering the payload cannot do.
        .sampler = {.mipFilter = Texture2DMipFilterMode::None},
        .levels = levels,
    });
}

Core::Result<Texture2DPayloadView>
parseTexture2DPayload(std::span<const std::byte> payload)
{
    if (payload.size() > Wire::MaxPayloadBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "texture payload exceeds the global payload byte limit");
    }
    if (payload.size() < Texture2DWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "texture payload too small");
    }
    Texture2DPayloadView view{};
    view.schemaVersion = readU16(payload, 0U);
    if (view.schemaVersion != Texture2DWire::SchemaVersion)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "unsupported texture payload schema");
    }
    view.width = readU16(payload, 2U);
    view.height = readU16(payload, 4U);
    view.pixelFormat = static_cast<Texture2DPixelFormat>(readU16(payload, 6U));
    view.colorSpace = static_cast<Texture2DColorSpace>(readU8(payload, 8U));
    view.levelCount = readU8(payload, 9U);
    view.sampler.wrapU = static_cast<Texture2DWrapMode>(readU8(payload, 10U));
    view.sampler.wrapV = static_cast<Texture2DWrapMode>(readU8(payload, 11U));
    view.sampler.minFilter = static_cast<Texture2DFilterMode>(readU8(payload, 12U));
    view.sampler.magFilter = static_cast<Texture2DFilterMode>(readU8(payload, 13U));
    view.sampler.mipFilter = static_cast<Texture2DMipFilterMode>(readU8(payload, 14U));
    view.levelBytes = readU32(payload, 16U);
    if (readU8(payload, 15U) != 0U || readU32(payload, 20U) != 0U ||
        readU32(payload, 24U) != 0U || readU32(payload, 28U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture payload reserved must be zero");
    }
    if (view.width == 0 || view.height == 0 || view.width > Texture2DWire::MaxDimension ||
        view.height > Texture2DWire::MaxDimension)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture dimensions invalid");
    }
    if (!isKnownPixelFormat(view.pixelFormat))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported texture pixel format");
    }
    if (!isKnownColorSpace(view.colorSpace))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported texture color space");
    }
    if (auto status = validateTexture2DSamplerDesc(view.sampler); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (view.levelCount == 0 || view.levelCount > Texture2DWire::MaxLevelCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture level count invalid");
    }
    if ((view.levelCount == 1U && view.sampler.mipFilter != Texture2DMipFilterMode::None) ||
        (view.levelCount > 1U && view.sampler.mipFilter == Texture2DMipFilterMode::None))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "texture mip filter does not match the stored mip chain");
    }
    if (view.levelCount > 1U && view.levelCount != texture2DFullMipLevelCount(view.width, view.height))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "texture mip chain must run from the base level down to 1x1");
    }

    u32 descriptorBytes = 0;
    u32 expectedTotal = 0;
    if (!checkedMultiply(view.levelCount, Texture2DWire::LevelDescriptorBytes, descriptorBytes) ||
        !checkedAdd(Texture2DWire::HeaderBytes, descriptorBytes, expectedTotal) ||
        !checkedAdd(expectedTotal, view.levelBytes, expectedTotal))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "texture payload size overflow");
    }
    if (payload.size() != expectedTotal)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture payload size mismatch");
    }

    u32 walkingOffset = Texture2DWire::HeaderBytes + descriptorBytes;
    u16 levelWidth = view.width;
    u16 levelHeight = view.height;
    u32 accumulated = 0;
    for (u8 index = 0; index < view.levelCount; ++index)
    {
        const usize descriptorOffset =
            Texture2DWire::HeaderBytes + static_cast<usize>(index) * Texture2DWire::LevelDescriptorBytes;
        const u32 byteOffset = readU32(payload, descriptorOffset);
        const u32 byteSize = readU32(payload, descriptorOffset + 4U);
        const u32 expectedSize = texture2DLevelByteSize(view.pixelFormat, levelWidth, levelHeight);
        if (expectedSize == 0)
        {
            return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "texture level size overflow");
        }
        // Levels must be tightly packed in order. Arbitrary offsets would let two
        // levels alias the same bytes, or point outside the payload.
        if (byteOffset != walkingOffset || byteSize != expectedSize)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "texture level descriptor does not match its packed extent");
        }
        u32 levelEnd = 0;
        if (!checkedAdd(byteOffset, byteSize, levelEnd) || levelEnd > payload.size())
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture level exceeds payload bounds");
        }
        view.levelTable[index] = Texture2DPayloadLevelView{
            .width = levelWidth,
            .height = levelHeight,
            .bytes = payload.subspan(byteOffset, byteSize),
        };
        if (!checkedAdd(accumulated, byteSize, accumulated))
        {
            return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "texture payload size overflow");
        }
        walkingOffset = levelEnd;
        levelWidth = nextMipExtent(levelWidth);
        levelHeight = nextMipExtent(levelHeight);
    }
    if (accumulated != view.levelBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "texture level bytes mismatch");
    }

    return view;
}

Core::Result<std::vector<std::byte>> writeCookedTexture2DAsset(Core::AssetId assetId, const Texture2DPayloadDesc& desc,
                                                               TargetPlatform platform)
{
    auto payload = writeTexture2DPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    return writeCookedAssetBytes(CookedAssetWriteDesc{
        .assetKind = AssetKind::Texture2D,
        .assetTypeVersion = Texture2DWire::SchemaVersion,
        .targetPlatform = platform,
        .assetId = assetId,
        .payload = *payload,
        .payloadAlignment = 16,
        .computeContentHash = true,
    });
}

Core::Result<std::vector<std::byte>>
writeCookedTexture2DAssetRgba8(Core::AssetId assetId, Core::u16 width, Core::u16 height,
                               std::span<const std::byte> rgba8Pixels, Texture2DColorSpace colorSpace,
                               TargetPlatform platform)
{
    const std::array<Texture2DLevelDesc, 1> levels{
        Texture2DLevelDesc{.width = width, .height = height, .bytes = rgba8Pixels}};
    return writeCookedTexture2DAsset(assetId,
                                     Texture2DPayloadDesc{
                                         .pixelFormat = Texture2DPixelFormat::Rgba8Unorm,
                                         .colorSpace = colorSpace,
                                         .sampler = {.mipFilter = Texture2DMipFilterMode::None},
                                         .levels = levels,
                                     },
                                     platform);
}

} // namespace Tina::AssetFormat
