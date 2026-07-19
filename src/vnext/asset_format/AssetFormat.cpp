#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

#include <algorithm>
#include <limits>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
using Core::u32;
using Core::u64;
using Core::u8;
using Core::usize;

[[nodiscard]] bool matchesMagic(std::span<const std::byte> bytes, const std::array<std::byte, 8>& magic) noexcept
{
    return bytes.size() >= magic.size() && std::equal(magic.begin(), magic.end(), bytes.begin());
}

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

[[nodiscard]] u64 readU64(std::span<const std::byte> bytes, usize offset) noexcept
{
    u64 value = 0;
    for (usize index = 0; index < 8U; ++index)
    {
        value |= static_cast<u64>(readU8(bytes, offset + index)) << (index * 8U);
    }
    return value;
}

template <typename Bytes> [[nodiscard]] Bytes readFixedBytes(std::span<const std::byte> bytes, usize offset) noexcept
{
    Bytes result{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), result.size(), result.begin());
    return result;
}

[[nodiscard]] bool checkedAdd(u64 left, u64 right, u64& result) noexcept
{
    if (right > (std::numeric_limits<u64>::max)() - left)
    {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checkedMultiply(u64 left, u64 right, u64& result) noexcept
{
    if (left != 0 && right > (std::numeric_limits<u64>::max)() / left)
    {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] bool checkedAlignUp(u64 value, u32 alignment, u64& result) noexcept
{
    const auto mask = static_cast<u64>(alignment - 1U);
    u64 expanded = 0;
    if (!checkedAdd(value, mask, expanded))
    {
        return false;
    }
    result = expanded & ~mask;
    return true;
}

[[nodiscard]] constexpr bool isPowerOfTwo(u32 value) noexcept
{
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] constexpr bool isKnownAssetKind(AssetKind kind) noexcept
{
    return kind >= AssetKind::Texture2D && kind <= AssetKind::AudioClip;
}

[[nodiscard]] constexpr bool isKnownTargetPlatform(TargetPlatform platform) noexcept
{
    return platform >= TargetPlatform::Any && platform <= TargetPlatform::LinuxX64;
}

[[nodiscard]] constexpr bool isKnownHashAlgorithm(HashAlgorithm algorithm) noexcept
{
    return algorithm == HashAlgorithm::Xxh3_128V1;
}

[[nodiscard]] constexpr bool isRequiredDependencyFlags(DependencyFlags flags) noexcept
{
    return flags == DependencyFlags::Required;
}

[[nodiscard]] bool isZeroPadding(std::span<const std::byte> bytes, u64 begin, u64 end) noexcept
{
    for (u64 offset = begin; offset < end; ++offset)
    {
        if (bytes[static_cast<usize>(offset)] != std::byte{0})
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<Core::AssetId> readAssetId(std::span<const std::byte> bytes, usize offset) noexcept
{
    return Core::AssetId::fromBytes(readFixedBytes<Core::AssetId::Bytes>(bytes, offset));
}

[[nodiscard]] std::optional<Core::ContentHash> readContentHash(std::span<const std::byte> bytes, usize offset) noexcept
{
    return Core::ContentHash::fromBytes(readFixedBytes<Core::ContentHash::Bytes>(bytes, offset));
}

[[nodiscard]] std::optional<AssetDependency> decodeDependency(std::span<const std::byte> bytes, usize offset) noexcept
{
    const auto assetId = readAssetId(bytes, offset);
    if (!assetId)
    {
        return std::nullopt;
    }
    return AssetDependency{
        .assetId = *assetId,
        .expectedKind = static_cast<AssetKind>(readU16(bytes, offset + 16U)),
        .flags = static_cast<DependencyFlags>(readU16(bytes, offset + 18U)),
    };
}

[[nodiscard]] std::optional<CookedManifestEntry> decodeManifestEntry(std::span<const std::byte> bytes,
                                                                     usize offset) noexcept
{
    const auto assetId = readAssetId(bytes, offset);
    const auto contentHash = readContentHash(bytes, offset + 16U);
    if (!assetId || !contentHash)
    {
        return std::nullopt;
    }
    return CookedManifestEntry{
        .assetId = *assetId,
        .contentHash = *contentHash,
        .assetKind = static_cast<AssetKind>(readU16(bytes, offset + 32U)),
        .assetTypeVersion = readU16(bytes, offset + 34U),
        .dependencyFirst = readU32(bytes, offset + 40U),
        .dependencyCount = readU32(bytes, offset + 44U),
        .cookedFileBytes = readU64(bytes, offset + 48U),
    };
}

[[nodiscard]] usize manifestEntryOffset(const CookedManifestHeader& header, u32 index) noexcept
{
    return static_cast<usize>(header.entriesOffset) + static_cast<usize>(index) * Wire::ManifestEntryBytes;
}

[[nodiscard]] usize manifestDependencyOffset(const CookedManifestHeader& header, u32 index) noexcept
{
    return static_cast<usize>(header.dependenciesOffset) + static_cast<usize>(index) * Wire::DependencyEntryBytes;
}

[[nodiscard]] std::optional<CookedManifestEntry>
findManifestEntry(std::span<const std::byte> bytes, const CookedManifestHeader& header, Core::AssetId assetId) noexcept
{
    u32 begin = 0;
    u32 end = header.entryCount;
    while (begin < end)
    {
        const auto middle = begin + (end - begin) / 2U;
        const auto candidate = decodeManifestEntry(bytes, manifestEntryOffset(header, middle));
        if (!candidate)
        {
            return std::nullopt;
        }
        if (candidate->assetId < assetId)
        {
            begin = middle + 1U;
        } else
        {
            end = middle;
        }
    }
    if (begin >= header.entryCount)
    {
        return std::nullopt;
    }
    const auto candidate = decodeManifestEntry(bytes, manifestEntryOffset(header, begin));
    if (!candidate || candidate->assetId != assetId)
    {
        return std::nullopt;
    }
    return candidate;
}

[[nodiscard]] bool validCookedLimits(const CookedAssetLimits& limits) noexcept
{
    return limits.maxFileBytes > 0 && limits.maxFileBytes <= Wire::MaxCookedFileBytes && limits.maxPayloadBytes > 0 &&
           limits.maxPayloadBytes <= Wire::MaxPayloadBytes && limits.maxDependencies <= Wire::MaxDependenciesPerAsset &&
           isPowerOfTwo(limits.maxPayloadAlignment) && limits.maxPayloadAlignment <= Wire::MaxPayloadAlignment;
}

[[nodiscard]] bool validManifestLimits(const CookedManifestLimits& limits) noexcept
{
    return limits.maxFileBytes > 0 && limits.maxFileBytes <= Wire::MaxManifestFileBytes &&
           limits.maxEntries <= Wire::MaxManifestEntries && limits.maxDependencies <= Wire::MaxManifestDependencies &&
           limits.maxDependenciesPerAsset <= Wire::MaxDependenciesPerAsset && limits.maxCookedAssetBytes > 0 &&
           limits.maxCookedAssetBytes <= Wire::MaxCookedFileBytes;
}

} // namespace

std::optional<AssetDependency> CookedAssetView::dependency(u32 index) const noexcept
{
    if (index >= m_header.dependencyCount)
    {
        return std::nullopt;
    }
    const auto offset =
        static_cast<usize>(m_header.dependencyOffset) + static_cast<usize>(index) * Wire::DependencyEntryBytes;
    return decodeDependency(m_bytes, offset);
}

std::span<const std::byte> CookedAssetView::payload() const noexcept
{
    if (m_bytes.empty())
    {
        return {};
    }
    return m_bytes.subspan(static_cast<usize>(m_header.payloadOffset), static_cast<usize>(m_header.payloadBytes));
}

std::optional<CookedManifestEntry> CookedManifestView::entry(u32 index) const noexcept
{
    if (index >= m_header.entryCount)
    {
        return std::nullopt;
    }
    return decodeManifestEntry(m_bytes, manifestEntryOffset(m_header, index));
}

std::optional<AssetDependency> CookedManifestView::dependency(u32 index) const noexcept
{
    if (index >= m_header.dependencyCount)
    {
        return std::nullopt;
    }
    return decodeDependency(m_bytes, manifestDependencyOffset(m_header, index));
}

std::optional<AssetDependency> CookedManifestView::dependencyForEntry(u32 entryIndex,
                                                                      u32 dependencyIndex) const noexcept
{
    const auto manifestEntry = entry(entryIndex);
    if (!manifestEntry || dependencyIndex >= manifestEntry->dependencyCount)
    {
        return std::nullopt;
    }
    return dependency(manifestEntry->dependencyFirst + dependencyIndex);
}

Core::Result<CookedAssetView> parseCookedAssetView(std::span<const std::byte> bytes, CookedAssetLimits limits)
{
    if (!validCookedLimits(limits))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLimits, "invalid cooked asset limits");
    }
    if (bytes.size() < Wire::CookedAssetHeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "cooked asset header is truncated");
    }
    if (bytes.size() > limits.maxFileBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded, "cooked asset exceeds file limit");
    }
    if (!matchesMagic(bytes, Wire::CookedAssetMagic))
    {
        return Core::failure(AssetFormatErrorCode::InvalidMagic, "invalid cooked asset magic");
    }

    const auto schemaMajor = readU16(bytes, 8U);
    const auto schemaMinor = readU16(bytes, 10U);
    if (schemaMajor != Wire::SchemaMajor || schemaMinor != Wire::SchemaMinor)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "unsupported cooked asset schema");
    }
    if (readU32(bytes, 12U) != Wire::CookedAssetHeaderBytes || readU32(bytes, 76U) != Wire::DependencyEntryBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "invalid cooked asset fixed field size");
    }

    CookedAssetHeader header{
        .schemaMajor = schemaMajor,
        .schemaMinor = schemaMinor,
        .assetKind = static_cast<AssetKind>(readU16(bytes, 16U)),
        .assetTypeVersion = readU16(bytes, 18U),
        .targetPlatform = static_cast<TargetPlatform>(readU16(bytes, 20U)),
        .hashAlgorithm = static_cast<HashAlgorithm>(readU8(bytes, 23U)),
        .dependencyOffset = readU64(bytes, 64U),
        .dependencyCount = readU32(bytes, 72U),
        .payloadOffset = readU64(bytes, 80U),
        .payloadBytes = readU64(bytes, 88U),
        .payloadAlignment = readU32(bytes, 96U),
        .fileBytes = readU64(bytes, 104U),
    };

    if (readU8(bytes, 22U) != static_cast<u8>(EndianTag::Little) || !isKnownAssetKind(header.assetKind) ||
        !isKnownTargetPlatform(header.targetPlatform) || !isKnownHashAlgorithm(header.hashAlgorithm))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported cooked asset enum value");
    }
    if (header.assetTypeVersion == 0 || readU32(bytes, 24U) != 0 || readU32(bytes, 28U) != 0 ||
        readU32(bytes, 100U) != 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "invalid cooked asset flags or reserved field");
    }

    const auto assetId = readAssetId(bytes, 32U);
    const auto contentHash = readContentHash(bytes, 48U);
    if (!assetId || !contentHash)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity, "cooked asset identity or hash is zero");
    }
    header.assetId = *assetId;
    header.contentHash = *contentHash;

    if (header.dependencyCount > limits.maxDependencies)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded, "cooked asset dependency limit exceeded");
    }
    if (!isPowerOfTwo(header.payloadAlignment) || header.payloadAlignment > limits.maxPayloadAlignment)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "invalid cooked payload alignment");
    }
    if (header.payloadBytes == 0 || header.payloadBytes > limits.maxPayloadBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded, "invalid cooked payload size");
    }

    u64 payloadEnd = 0;
    if (!checkedAdd(header.payloadOffset, header.payloadBytes, payloadEnd))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "cooked payload range overflow");
    }
    u64 dependencyBytes = 0;
    u64 dependencyEnd = 0;
    if (!checkedMultiply(header.dependencyCount, Wire::DependencyEntryBytes, dependencyBytes) ||
        !checkedAdd(header.dependencyOffset, dependencyBytes, dependencyEnd))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "cooked dependency range overflow");
    }
    u64 expectedPayloadOffset = 0;
    if (!checkedAlignUp(dependencyEnd, header.payloadAlignment, expectedPayloadOffset))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "cooked payload alignment overflow");
    }

    if (header.dependencyOffset != Wire::CookedAssetHeaderBytes || header.payloadOffset != expectedPayloadOffset ||
        header.fileBytes != bytes.size() || payloadEnd != header.fileBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "non-canonical cooked asset layout");
    }
    if (!isZeroPadding(bytes, dependencyEnd, header.payloadOffset))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "cooked asset padding must be zero");
    }

    std::optional<Core::AssetId> previousDependency;
    for (u32 index = 0; index < header.dependencyCount; ++index)
    {
        const auto offset =
            static_cast<usize>(header.dependencyOffset) + static_cast<usize>(index) * Wire::DependencyEntryBytes;
        const auto dependency = decodeDependency(bytes, offset);
        if (!dependency)
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity, "cooked dependency id is zero");
        }
        if (readU32(bytes, offset + 20U) != 0 || !isKnownAssetKind(dependency->expectedKind) ||
            !isRequiredDependencyFlags(dependency->flags) || dependency->assetId == header.assetId ||
            (previousDependency && dependency->assetId <= *previousDependency))
        {
            return Core::failure(AssetFormatErrorCode::InvalidDependency, "invalid cooked dependency");
        }
        previousDependency = dependency->assetId;
    }

    return CookedAssetView(bytes, header);
}

Core::Result<CookedManifestView> parseCookedManifestView(std::span<const std::byte> bytes, CookedManifestLimits limits)
{
    if (!validManifestLimits(limits))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLimits, "invalid cooked manifest limits");
    }
    if (bytes.size() < Wire::CookedManifestHeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "cooked manifest header is truncated");
    }
    if (bytes.size() > limits.maxFileBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded, "cooked manifest exceeds file limit");
    }
    if (!matchesMagic(bytes, Wire::CookedManifestMagic))
    {
        return Core::failure(AssetFormatErrorCode::InvalidMagic, "invalid cooked manifest magic");
    }

    const auto schemaMajor = readU16(bytes, 8U);
    const auto schemaMinor = readU16(bytes, 10U);
    if (schemaMajor != Wire::SchemaMajor || schemaMinor != Wire::SchemaMinor)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema, "unsupported cooked manifest schema");
    }
    if (readU32(bytes, 12U) != Wire::CookedManifestHeaderBytes || readU32(bytes, 28U) != Wire::ManifestEntryBytes ||
        readU32(bytes, 36U) != Wire::DependencyEntryBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "invalid cooked manifest fixed field size");
    }

    CookedManifestHeader header{
        .schemaMajor = schemaMajor,
        .schemaMinor = schemaMinor,
        .targetPlatform = static_cast<TargetPlatform>(readU16(bytes, 16U)),
        .hashAlgorithm = static_cast<HashAlgorithm>(readU8(bytes, 19U)),
        .entryCount = readU32(bytes, 24U),
        .dependencyCount = readU32(bytes, 32U),
        .entriesOffset = readU64(bytes, 40U),
        .dependenciesOffset = readU64(bytes, 48U),
        .fileBytes = readU64(bytes, 56U),
    };

    if (readU8(bytes, 18U) != static_cast<u8>(EndianTag::Little) || !isKnownTargetPlatform(header.targetPlatform) ||
        !isKnownHashAlgorithm(header.hashAlgorithm))
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported cooked manifest enum value");
    }
    if (readU32(bytes, 20U) != 0)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "invalid cooked manifest flags");
    }
    if (header.entryCount > limits.maxEntries || header.dependencyCount > limits.maxDependencies)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded, "cooked manifest count limit exceeded");
    }

    u64 entryBytes = 0;
    u64 dependencyBytes = 0;
    u64 expectedDependenciesOffset = 0;
    u64 expectedFileBytes = 0;
    if (!checkedMultiply(header.entryCount, Wire::ManifestEntryBytes, entryBytes) ||
        !checkedMultiply(header.dependencyCount, Wire::DependencyEntryBytes, dependencyBytes) ||
        !checkedAdd(Wire::CookedManifestHeaderBytes, entryBytes, expectedDependenciesOffset) ||
        !checkedAdd(expectedDependenciesOffset, dependencyBytes, expectedFileBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "cooked manifest layout overflow");
    }
    if (header.entriesOffset != Wire::CookedManifestHeaderBytes ||
        header.dependenciesOffset != expectedDependenciesOffset || header.fileBytes != expectedFileBytes ||
        header.fileBytes != bytes.size())
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout, "non-canonical cooked manifest layout");
    }

    std::optional<Core::AssetId> previousAssetId;
    u32 expectedDependencyFirst = 0;
    for (u32 index = 0; index < header.entryCount; ++index)
    {
        const auto offset = manifestEntryOffset(header, index);
        const auto manifestEntry = decodeManifestEntry(bytes, offset);
        if (!manifestEntry)
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity, "manifest entry identity or hash is zero");
        }
        if (!isKnownAssetKind(manifestEntry->assetKind) || manifestEntry->assetTypeVersion == 0 ||
            readU32(bytes, offset + 36U) != 0)
        {
            return Core::failure(AssetFormatErrorCode::UnsupportedValue, "invalid manifest entry metadata");
        }
        if (manifestEntry->cookedFileBytes == 0 || manifestEntry->cookedFileBytes > limits.maxCookedAssetBytes ||
            manifestEntry->dependencyCount > limits.maxDependenciesPerAsset)
        {
            return Core::failure(AssetFormatErrorCode::SizeLimitExceeded, "manifest entry limit exceeded");
        }
        if (previousAssetId && manifestEntry->assetId <= *previousAssetId)
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity, "manifest entries are not strictly sorted");
        }
        if (manifestEntry->dependencyFirst != expectedDependencyFirst ||
            manifestEntry->dependencyCount > header.dependencyCount - expectedDependencyFirst)
        {
            return Core::failure(AssetFormatErrorCode::InvalidDependency,
                                 "manifest dependency ranges are not contiguous");
        }
        expectedDependencyFirst += manifestEntry->dependencyCount;
        previousAssetId = manifestEntry->assetId;
    }
    if (expectedDependencyFirst != header.dependencyCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidDependency, "manifest dependency table is not fully covered");
    }

    for (u32 entryIndex = 0; entryIndex < header.entryCount; ++entryIndex)
    {
        const auto manifestEntry = *decodeManifestEntry(bytes, manifestEntryOffset(header, entryIndex));
        std::optional<Core::AssetId> previousDependency;
        for (u32 localIndex = 0; localIndex < manifestEntry.dependencyCount; ++localIndex)
        {
            const auto dependencyIndex = manifestEntry.dependencyFirst + localIndex;
            const auto offset = manifestDependencyOffset(header, dependencyIndex);
            const auto dependency = decodeDependency(bytes, offset);
            if (!dependency)
            {
                return Core::failure(AssetFormatErrorCode::InvalidIdentity, "manifest dependency id is zero");
            }
            if (readU32(bytes, offset + 20U) != 0 || !isKnownAssetKind(dependency->expectedKind) ||
                !isRequiredDependencyFlags(dependency->flags) || dependency->assetId == manifestEntry.assetId ||
                (previousDependency && dependency->assetId <= *previousDependency))
            {
                return Core::failure(AssetFormatErrorCode::InvalidDependency, "invalid manifest dependency");
            }
            const auto targetEntry = findManifestEntry(bytes, header, dependency->assetId);
            if (!targetEntry)
            {
                return Core::failure(AssetFormatErrorCode::MissingDependency, "manifest dependency target is missing");
            }
            if (targetEntry->assetKind != dependency->expectedKind)
            {
                return Core::failure(AssetFormatErrorCode::DependencyTypeMismatch,
                                     "manifest dependency kind does not match target");
            }
            previousDependency = dependency->assetId;
        }
    }

    return CookedManifestView(bytes, header);
}

Core::Result<CookedArtifactPath> makeCookedArtifactPath(AssetKind assetKind, Core::AssetId assetId)
{
    if (!isKnownAssetKind(assetKind) || !assetId)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity, "artifact path requires valid kind and id");
    }

    constexpr char Digits[] = "0123456789abcdef";
    constexpr std::string_view Prefix = "objects/";
    constexpr std::string_view Extension = ".tasset";
    CookedArtifactPath path{};
    std::copy(Prefix.begin(), Prefix.end(), path.storage.begin());

    const auto kind = static_cast<u16>(assetKind);
    for (usize index = 0; index < 4U; ++index)
    {
        const auto shift = static_cast<unsigned int>((3U - index) * 4U);
        path.storage[8U + index] = Digits[(kind >> shift) & 0x0FU];
    }
    path.storage[12U] = '/';

    const auto idText = assetId.canonicalText();
    path.storage[13U] = idText[0];
    path.storage[14U] = idText[1];
    path.storage[15U] = '/';
    std::copy(idText.begin(), idText.end(), path.storage.begin() + 16);
    std::copy(Extension.begin(), Extension.end(), path.storage.begin() + 48);
    path.storage[CookedArtifactPath::CharacterCount] = '\0';
    return path;
}

Core::Status verifyCookedAssetContentHash(const CookedAssetView& asset)
{
    if (!asset)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "cooked asset view is empty");
    }
    if (asset.header().hashAlgorithm != HashAlgorithm::Xxh3_128V1)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue, "unsupported cooked content hash algorithm");
    }

    const auto digest = Core::digestContentHashV1(asset.payload());
    if (!digest)
    {
        return Core::failure(std::move(digest.error()));
    }
    if (*digest != asset.header().contentHash)
    {
        return Core::failure(AssetFormatErrorCode::ContentHashMismatch, "cooked payload content hash mismatch");
    }
    return Core::success();
}

} // namespace Tina::AssetFormat
