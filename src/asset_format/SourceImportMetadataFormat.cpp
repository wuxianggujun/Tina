#include <tina/asset_format/SourceImportMetadataFormat.hpp>

#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <limits>
#include <new>

namespace Tina::AssetFormat {
namespace {

using Core::u16;
using Core::u32;
using Core::u64;
using Core::u8;
using Core::usize;

[[nodiscard]] bool matchesMagic(std::span<const std::byte> bytes) noexcept
{
    return bytes.size() >= SourceImportWire::Magic.size() &&
           std::equal(SourceImportWire::Magic.begin(), SourceImportWire::Magic.end(), bytes.begin());
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

template <typename Bytes> [[nodiscard]] Bytes readFixed(std::span<const std::byte> bytes, usize offset) noexcept
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
    if (left != 0U && right > (std::numeric_limits<u64>::max)() / left)
    {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] constexpr bool isKnownTargetPlatform(TargetPlatform platform) noexcept
{
    return platform >= TargetPlatform::Any && platform <= TargetPlatform::LinuxX64;
}

[[nodiscard]] constexpr bool isKnownAssetKind(AssetKind kind) noexcept
{
    return kind >= AssetKind::Texture2D && kind <= AssetKind::EnvironmentMap;
}

[[nodiscard]] constexpr bool isKnownInputFlags(SourceImportInputFlags flags) noexcept
{
    constexpr auto Known = static_cast<u16>(SourceImportInputFlags::Primary);
    return (static_cast<u16>(flags) & ~Known) == 0U;
}

[[nodiscard]] bool validLimits(const SourceImportMetadataLimits& limits) noexcept
{
    return limits.maxFileBytes > 0U && limits.maxFileBytes <= SourceImportWire::MaxFileBytes &&
           limits.maxStringBytes <= SourceImportWire::MaxStringBytes && limits.maxPathBytes > 0U &&
           limits.maxPathBytes <= SourceImportWire::MaxPathBytes && limits.maxSources <= SourceImportWire::MaxSources &&
           limits.maxUnits <= SourceImportWire::MaxUnits && limits.maxUnitInputs <= SourceImportWire::MaxUnitInputs &&
           limits.maxOutputs <= SourceImportWire::MaxOutputs;
}

[[nodiscard]] int comparePathBytes(std::string_view left, std::string_view right) noexcept
{
    const auto sharedBytes = (std::min)(left.size(), right.size());
    for (usize index = 0; index < sharedBytes; ++index)
    {
        const auto leftByte = static_cast<unsigned char>(left[index]);
        const auto rightByte = static_cast<unsigned char>(right[index]);
        if (leftByte < rightByte)
        {
            return -1;
        }
        if (leftByte > rightByte)
        {
            return 1;
        }
    }
    if (left.size() < right.size())
    {
        return -1;
    }
    if (left.size() > right.size())
    {
        return 1;
    }
    return 0;
}

[[nodiscard]] bool isNormalizedSourcePath(std::string_view path) noexcept
{
    if (path.empty() || !Core::isStrictUtf8WithoutNul(path) || path.front() == '/' || path.back() == '/')
    {
        return false;
    }

    usize segmentBegin = 0;
    for (usize index = 0; index <= path.size(); ++index)
    {
        if (index < path.size() && path[index] != '/')
        {
            if (path[index] == '\\' || path[index] == ':')
            {
                return false;
            }
            continue;
        }

        const auto segment = path.substr(segmentBegin, index - segmentBegin);
        if (segment.empty() || segment == "." || segment == "..")
        {
            return false;
        }
        segmentBegin = index + 1U;
    }
    return true;
}

[[nodiscard]] usize sourceOffset(const SourceImportMetadataHeader& header, u32 index) noexcept
{
    return static_cast<usize>(header.sourcesOffset) + static_cast<usize>(index) * SourceImportWire::SourceEntryBytes;
}

[[nodiscard]] usize unitOffset(const SourceImportMetadataHeader& header, u32 index) noexcept
{
    return static_cast<usize>(header.unitsOffset) + static_cast<usize>(index) * SourceImportWire::UnitEntryBytes;
}

[[nodiscard]] usize unitInputOffset(const SourceImportMetadataHeader& header, u32 index) noexcept
{
    return static_cast<usize>(header.unitInputsOffset) +
           static_cast<usize>(index) * SourceImportWire::UnitInputEntryBytes;
}

[[nodiscard]] usize outputOffset(const SourceImportMetadataHeader& header, u32 index) noexcept
{
    return static_cast<usize>(header.outputsOffset) + static_cast<usize>(index) * SourceImportWire::OutputEntryBytes;
}

[[nodiscard]] std::optional<SourceImportSource> decodeSource(std::span<const std::byte> bytes, usize offset) noexcept
{
    const auto contentHash = Core::ContentHash::fromBytes(readFixed<Core::ContentHash::Bytes>(bytes, offset));
    if (!contentHash)
    {
        return std::nullopt;
    }
    return SourceImportSource{
        .contentHash = *contentHash,
        .fileBytes = readU64(bytes, offset + 16U),
        .pathOffset = readU64(bytes, offset + 24U),
        .pathBytes = readU32(bytes, offset + 32U),
    };
}

[[nodiscard]] std::optional<SourceImportUnit> decodeUnit(std::span<const std::byte> bytes, usize offset) noexcept
{
    const auto unitId = SourceImportUnitId::fromBytes(readFixed<SourceImportUnitId::Bytes>(bytes, offset));
    const auto settingsHash =
        Core::ContentHash::fromBytes(readFixed<Core::ContentHash::Bytes>(bytes, offset + 24U));
    if (!unitId || !settingsHash)
    {
        return std::nullopt;
    }
    return SourceImportUnit{
        .unitId = *unitId,
        .importerKind = readU32(bytes, offset + 16U),
        .importerVersion = readU32(bytes, offset + 20U),
        .settingsHash = *settingsHash,
        .inputFirst = readU32(bytes, offset + 40U),
        .inputCount = readU32(bytes, offset + 44U),
        .outputFirst = readU32(bytes, offset + 48U),
        .outputCount = readU32(bytes, offset + 52U),
    };
}

[[nodiscard]] SourceImportUnitInput decodeUnitInput(std::span<const std::byte> bytes, usize offset) noexcept
{
    return SourceImportUnitInput{
        .sourceIndex = readU32(bytes, offset),
        .flags = static_cast<SourceImportInputFlags>(readU16(bytes, offset + 4U)),
    };
}

[[nodiscard]] std::optional<SourceImportOutput> decodeOutput(std::span<const std::byte> bytes,
                                                             usize offset) noexcept
{
    const auto assetId = Core::AssetId::fromBytes(readFixed<Core::AssetId::Bytes>(bytes, offset));
    if (!assetId)
    {
        return std::nullopt;
    }
    return SourceImportOutput{
        .assetId = *assetId,
        .assetKind = static_cast<AssetKind>(readU16(bytes, offset + 16U)),
    };
}

[[nodiscard]] std::string_view decodePath(std::span<const std::byte> bytes,
                                          const SourceImportSource& source) noexcept
{
    const auto* data = reinterpret_cast<const char*>(bytes.data() + static_cast<usize>(source.pathOffset));
    return {data, source.pathBytes};
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

void writeU64(std::vector<std::byte>& bytes, usize offset, u64 value)
{
    for (usize index = 0; index < 8U; ++index)
    {
        writeU8(bytes, offset + index, static_cast<u8>((value >> (index * 8U)) & 0xFFU));
    }
}

template <usize Size>
void writeFixed(std::vector<std::byte>& bytes, usize offset, const std::array<std::byte, Size>& value)
{
    std::copy(value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

} // namespace

std::optional<SourceImportSource> SourceImportMetadataView::source(u32 index) const noexcept
{
    if (index >= m_header.sourceCount)
    {
        return std::nullopt;
    }
    return decodeSource(m_bytes, sourceOffset(m_header, index));
}

std::optional<std::string_view> SourceImportMetadataView::sourcePath(u32 index) const noexcept
{
    const auto sourceEntry = source(index);
    if (!sourceEntry)
    {
        return std::nullopt;
    }
    return decodePath(m_bytes, *sourceEntry);
}

std::optional<SourceImportUnit> SourceImportMetadataView::unit(u32 index) const noexcept
{
    if (index >= m_header.unitCount)
    {
        return std::nullopt;
    }
    return decodeUnit(m_bytes, unitOffset(m_header, index));
}

std::optional<SourceImportUnitInput> SourceImportMetadataView::unitInput(u32 index) const noexcept
{
    if (index >= m_header.unitInputCount)
    {
        return std::nullopt;
    }
    return decodeUnitInput(m_bytes, unitInputOffset(m_header, index));
}

std::optional<SourceImportUnitInput> SourceImportMetadataView::unitInputForUnit(u32 unitIndex,
                                                                                u32 inputIndex) const noexcept
{
    const auto unitEntry = unit(unitIndex);
    if (!unitEntry || inputIndex >= unitEntry->inputCount)
    {
        return std::nullopt;
    }
    return unitInput(unitEntry->inputFirst + inputIndex);
}

std::optional<SourceImportOutput> SourceImportMetadataView::output(u32 index) const noexcept
{
    if (index >= m_header.outputCount)
    {
        return std::nullopt;
    }
    return decodeOutput(m_bytes, outputOffset(m_header, index));
}

std::optional<SourceImportOutput> SourceImportMetadataView::outputForUnit(u32 unitIndex,
                                                                          u32 outputIndex) const noexcept
{
    const auto unitEntry = unit(unitIndex);
    if (!unitEntry || outputIndex >= unitEntry->outputCount)
    {
        return std::nullopt;
    }
    return output(unitEntry->outputFirst + outputIndex);
}

Core::Result<SourceImportMetadataView> parseSourceImportMetadataView(std::span<const std::byte> bytes,
                                                                     SourceImportMetadataLimits limits)
{
    if (!validLimits(limits))
    {
        return Core::failure(AssetFormatErrorCode::InvalidLimits, "invalid source import metadata limits");
    }
    if (bytes.size() < SourceImportWire::HeaderBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader, "source import metadata header is truncated");
    }
    if (bytes.size() > limits.maxFileBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "source import metadata exceeds file limit");
    }
    if (!matchesMagic(bytes))
    {
        return Core::failure(AssetFormatErrorCode::InvalidMagic, "invalid source import metadata magic");
    }

    const auto schemaMajor = readU16(bytes, 8U);
    const auto schemaMinor = readU16(bytes, 10U);
    if (schemaMajor != SourceImportWire::SchemaMajor || schemaMinor != SourceImportWire::SchemaMinor)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedSchema,
                             "unsupported source import metadata schema");
    }
    if (readU32(bytes, 12U) != SourceImportWire::HeaderBytes ||
        readU32(bytes, 52U) != SourceImportWire::SourceEntryBytes ||
        readU32(bytes, 68U) != SourceImportWire::UnitEntryBytes ||
        readU32(bytes, 84U) != SourceImportWire::UnitInputEntryBytes ||
        readU32(bytes, 100U) != SourceImportWire::OutputEntryBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader,
                             "invalid source import metadata fixed field size");
    }

    const auto manifestDigest =
        Core::ContentHash::fromBytes(readFixed<Core::ContentHash::Bytes>(bytes, 24U));
    SourceImportMetadataHeader header{
        .schemaMajor = schemaMajor,
        .schemaMinor = schemaMinor,
        .targetPlatform = static_cast<TargetPlatform>(readU16(bytes, 16U)),
        .hashAlgorithm = static_cast<HashAlgorithm>(readU8(bytes, 19U)),
        .manifestRevision = SourceImportManifestRevision{
            .manifestDigest = manifestDigest.value_or(Core::ContentHash{}),
            .manifestBytes = readU64(bytes, 40U),
        },
        .sourceCount = readU32(bytes, 48U),
        .unitCount = readU32(bytes, 64U),
        .unitInputCount = readU32(bytes, 80U),
        .outputCount = readU32(bytes, 96U),
        .sourcesOffset = readU64(bytes, 56U),
        .unitsOffset = readU64(bytes, 72U),
        .unitInputsOffset = readU64(bytes, 88U),
        .outputsOffset = readU64(bytes, 104U),
        .stringsOffset = readU64(bytes, 120U),
        .stringBytes = readU64(bytes, 112U),
        .fileBytes = readU64(bytes, 128U),
    };

    if (readU8(bytes, 18U) != static_cast<u8>(EndianTag::Little) ||
        !isKnownTargetPlatform(header.targetPlatform) || header.hashAlgorithm != HashAlgorithm::Xxh3_128V1)
    {
        return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                             "unsupported source import metadata enum value");
    }
    if (readU32(bytes, 20U) != 0U || readU64(bytes, 136U) != 0U)
    {
        return Core::failure(AssetFormatErrorCode::InvalidHeader,
                             "source import metadata reserved header fields are non-zero");
    }
    if (!header.manifestRevision.manifestDigest || header.manifestRevision.manifestBytes == 0U ||
        header.manifestRevision.manifestBytes > Wire::MaxManifestFileBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                             "source import metadata manifest revision is invalid");
    }
    if (header.sourceCount > limits.maxSources || header.unitCount > limits.maxUnits ||
        header.unitInputCount > limits.maxUnitInputs || header.outputCount > limits.maxOutputs ||
        header.stringBytes > limits.maxStringBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "source import metadata count or string limit exceeded");
    }

    u64 sourceBytes = 0;
    u64 unitBytes = 0;
    u64 unitInputBytes = 0;
    u64 outputBytes = 0;
    u64 expectedUnitsOffset = 0;
    u64 expectedUnitInputsOffset = 0;
    u64 expectedOutputsOffset = 0;
    u64 expectedStringsOffset = 0;
    u64 expectedFileBytes = 0;
    if (!checkedMultiply(header.sourceCount, SourceImportWire::SourceEntryBytes, sourceBytes) ||
        !checkedMultiply(header.unitCount, SourceImportWire::UnitEntryBytes, unitBytes) ||
        !checkedMultiply(header.unitInputCount, SourceImportWire::UnitInputEntryBytes, unitInputBytes) ||
        !checkedMultiply(header.outputCount, SourceImportWire::OutputEntryBytes, outputBytes) ||
        !checkedAdd(SourceImportWire::HeaderBytes, sourceBytes, expectedUnitsOffset) ||
        !checkedAdd(expectedUnitsOffset, unitBytes, expectedUnitInputsOffset) ||
        !checkedAdd(expectedUnitInputsOffset, unitInputBytes, expectedOutputsOffset) ||
        !checkedAdd(expectedOutputsOffset, outputBytes, expectedStringsOffset) ||
        !checkedAdd(expectedStringsOffset, header.stringBytes, expectedFileBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow,
                             "source import metadata layout overflow");
    }
    if (header.sourcesOffset != SourceImportWire::HeaderBytes || header.unitsOffset != expectedUnitsOffset ||
        header.unitInputsOffset != expectedUnitInputsOffset || header.outputsOffset != expectedOutputsOffset ||
        header.stringsOffset != expectedStringsOffset || header.fileBytes != expectedFileBytes ||
        header.fileBytes != bytes.size())
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "non-canonical source import metadata layout");
    }

    std::string_view previousPath;
    bool hasPreviousPath = false;
    u64 expectedPathOffset = header.stringsOffset;
    for (u32 index = 0; index < header.sourceCount; ++index)
    {
        const auto offset = sourceOffset(header, index);
        const auto sourceEntry = decodeSource(bytes, offset);
        if (!sourceEntry)
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity, "source content hash is zero");
        }
        if (readU32(bytes, offset + 36U) != 0U || sourceEntry->pathBytes == 0U ||
            sourceEntry->pathBytes > limits.maxPathBytes || sourceEntry->pathOffset != expectedPathOffset)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout, "source row is not canonical");
        }
        if (!checkedAdd(expectedPathOffset, sourceEntry->pathBytes, expectedPathOffset) ||
            expectedPathOffset > header.fileBytes)
        {
            return Core::failure(AssetFormatErrorCode::ArithmeticOverflow, "source path range overflow");
        }
        const auto path = decodePath(bytes, *sourceEntry);
        if (!isNormalizedSourcePath(path) || (hasPreviousPath && comparePathBytes(previousPath, path) >= 0))
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "source paths are invalid or not strictly sorted");
        }
        previousPath = path;
        hasPreviousPath = true;
    }
    if (expectedPathOffset != header.fileBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "source paths do not fully cover the string table");
    }

    std::optional<SourceImportUnitId> previousUnitId;
    u32 expectedInputFirst = 0;
    u32 expectedOutputFirst = 0;
    for (u32 index = 0; index < header.unitCount; ++index)
    {
        const auto offset = unitOffset(header, index);
        const auto unitEntry = decodeUnit(bytes, offset);
        if (!unitEntry)
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                                 "source import unit identity or settings hash is zero");
        }
        if (unitEntry->importerKind == 0U || unitEntry->importerVersion == 0U ||
            readU64(bytes, offset + 56U) != 0U)
        {
            return Core::failure(AssetFormatErrorCode::UnsupportedValue,
                                 "source import unit metadata is invalid");
        }
        if (previousUnitId && unitEntry->unitId <= *previousUnitId)
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                                 "source import units are not strictly sorted");
        }
        if (unitEntry->inputCount == 0U || unitEntry->outputCount == 0U ||
            unitEntry->inputFirst != expectedInputFirst ||
            unitEntry->inputCount > header.unitInputCount - expectedInputFirst ||
            unitEntry->outputFirst != expectedOutputFirst ||
            unitEntry->outputCount > header.outputCount - expectedOutputFirst)
        {
            return Core::failure(AssetFormatErrorCode::InvalidLayout,
                                 "source import unit ranges are not contiguous");
        }
        expectedInputFirst += unitEntry->inputCount;
        expectedOutputFirst += unitEntry->outputCount;
        previousUnitId = unitEntry->unitId;
    }
    if (expectedInputFirst != header.unitInputCount || expectedOutputFirst != header.outputCount)
    {
        return Core::failure(AssetFormatErrorCode::InvalidLayout,
                             "source import unit tables are not fully covered");
    }

    try
    {
        std::vector<u8> sourceReferenced(header.sourceCount, 0U);
        std::vector<Core::AssetId> ownedAssetIds;
        ownedAssetIds.reserve(header.outputCount);

        for (u32 unitIndex = 0; unitIndex < header.unitCount; ++unitIndex)
        {
            const auto unitEntry = *decodeUnit(bytes, unitOffset(header, unitIndex));
            std::optional<u32> previousSourceIndex;
            u32 primaryCount = 0;
            for (u32 localIndex = 0; localIndex < unitEntry.inputCount; ++localIndex)
            {
                const auto inputIndex = unitEntry.inputFirst + localIndex;
                const auto offset = unitInputOffset(header, inputIndex);
                const auto input = decodeUnitInput(bytes, offset);
                if (input.sourceIndex >= header.sourceCount || !isKnownInputFlags(input.flags) ||
                    readU16(bytes, offset + 6U) != 0U ||
                    (previousSourceIndex && input.sourceIndex <= *previousSourceIndex))
                {
                    return Core::failure(AssetFormatErrorCode::InvalidDependency,
                                         "source import unit input is invalid or not strictly sorted");
                }
                primaryCount += hasSourceImportInputFlag(input.flags, SourceImportInputFlags::Primary) ? 1U : 0U;
                sourceReferenced[input.sourceIndex] = 1U;
                previousSourceIndex = input.sourceIndex;
            }
            if (primaryCount != 1U)
            {
                return Core::failure(AssetFormatErrorCode::InvalidDependency,
                                     "source import unit must have exactly one primary input");
            }

            std::optional<Core::AssetId> previousAssetId;
            for (u32 localIndex = 0; localIndex < unitEntry.outputCount; ++localIndex)
            {
                const auto outputIndex = unitEntry.outputFirst + localIndex;
                const auto offset = outputOffset(header, outputIndex);
                const auto outputEntry = decodeOutput(bytes, offset);
                if (!outputEntry)
                {
                    return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                                         "source import output asset id is zero");
                }
                if (!isKnownAssetKind(outputEntry->assetKind) || readU16(bytes, offset + 18U) != 0U ||
                    readU32(bytes, offset + 20U) != 0U ||
                    (previousAssetId && outputEntry->assetId <= *previousAssetId))
                {
                    return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                                         "source import outputs are invalid or not strictly sorted");
                }
                ownedAssetIds.push_back(outputEntry->assetId);
                previousAssetId = outputEntry->assetId;
            }
        }

        if (std::find(sourceReferenced.begin(), sourceReferenced.end(), 0U) != sourceReferenced.end())
        {
            return Core::failure(AssetFormatErrorCode::InvalidDependency,
                                 "source table contains an unreferenced row");
        }
        std::sort(ownedAssetIds.begin(), ownedAssetIds.end());
        if (std::adjacent_find(ownedAssetIds.begin(), ownedAssetIds.end()) != ownedAssetIds.end())
        {
            return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                                 "an output asset is owned by more than one source import unit");
        }
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "source import metadata validation allocation failed");
    }

    return SourceImportMetadataView(bytes, header);
}

Core::Result<std::vector<std::byte>>
writeSourceImportMetadataBytes(const SourceImportMetadataWriteDesc& desc)
{
    if (!isKnownTargetPlatform(desc.targetPlatform) || !desc.manifestRevision.manifestDigest ||
        desc.manifestRevision.manifestBytes == 0U ||
        desc.manifestRevision.manifestBytes > Wire::MaxManifestFileBytes)
    {
        return Core::failure(AssetFormatErrorCode::InvalidIdentity,
                             "source import metadata write requires a valid manifest revision");
    }
    if (desc.sources.size() > SourceImportWire::MaxSources || desc.units.size() > SourceImportWire::MaxUnits)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "source import metadata source or unit count exceeds max");
    }

    u64 stringBytes = 0;
    for (const auto& source : desc.sources)
    {
        if (source.path.size() > SourceImportWire::MaxPathBytes ||
            !checkedAdd(stringBytes, static_cast<u64>(source.path.size()), stringBytes))
        {
            return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                                 "source import metadata path table exceeds max");
        }
    }
    if (stringBytes > SourceImportWire::MaxStringBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "source import metadata string table exceeds max");
    }

    u64 unitInputCount = 0;
    u64 outputCount = 0;
    for (const auto& unit : desc.units)
    {
        if (!checkedAdd(unitInputCount, static_cast<u64>(unit.inputs.size()), unitInputCount) ||
            !checkedAdd(outputCount, static_cast<u64>(unit.outputs.size()), outputCount))
        {
            return Core::failure(AssetFormatErrorCode::ArithmeticOverflow,
                                 "source import metadata row count overflow");
        }
    }
    if (unitInputCount > SourceImportWire::MaxUnitInputs || outputCount > SourceImportWire::MaxOutputs)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "source import metadata edge or output count exceeds max");
    }

    const u64 sourcesOffset = SourceImportWire::HeaderBytes;
    u64 unitsOffset = 0;
    u64 unitInputsOffset = 0;
    u64 outputsOffset = 0;
    u64 stringsOffset = 0;
    u64 fileBytes = 0;
    if (!checkedAdd(sourcesOffset, static_cast<u64>(desc.sources.size()) * SourceImportWire::SourceEntryBytes,
                    unitsOffset) ||
        !checkedAdd(unitsOffset, static_cast<u64>(desc.units.size()) * SourceImportWire::UnitEntryBytes,
                    unitInputsOffset) ||
        !checkedAdd(unitInputsOffset, unitInputCount * SourceImportWire::UnitInputEntryBytes, outputsOffset) ||
        !checkedAdd(outputsOffset, outputCount * SourceImportWire::OutputEntryBytes, stringsOffset) ||
        !checkedAdd(stringsOffset, stringBytes, fileBytes))
    {
        return Core::failure(AssetFormatErrorCode::ArithmeticOverflow,
                             "source import metadata write layout overflow");
    }
    if (fileBytes > SourceImportWire::MaxFileBytes)
    {
        return Core::failure(AssetFormatErrorCode::SizeLimitExceeded,
                             "source import metadata write exceeds max file bytes");
    }

    try
    {
        std::vector<std::byte> bytes(static_cast<usize>(fileBytes), std::byte{0});
        writeFixed(bytes, 0U, SourceImportWire::Magic);
        writeU16(bytes, 8U, SourceImportWire::SchemaMajor);
        writeU16(bytes, 10U, SourceImportWire::SchemaMinor);
        writeU32(bytes, 12U, SourceImportWire::HeaderBytes);
        writeU16(bytes, 16U, static_cast<u16>(desc.targetPlatform));
        writeU8(bytes, 18U, static_cast<u8>(EndianTag::Little));
        writeU8(bytes, 19U, static_cast<u8>(HashAlgorithm::Xxh3_128V1));
        writeFixed(bytes, 24U, desc.manifestRevision.manifestDigest.bytes());
        writeU64(bytes, 40U, desc.manifestRevision.manifestBytes);
        writeU32(bytes, 48U, static_cast<u32>(desc.sources.size()));
        writeU32(bytes, 52U, SourceImportWire::SourceEntryBytes);
        writeU64(bytes, 56U, sourcesOffset);
        writeU32(bytes, 64U, static_cast<u32>(desc.units.size()));
        writeU32(bytes, 68U, SourceImportWire::UnitEntryBytes);
        writeU64(bytes, 72U, unitsOffset);
        writeU32(bytes, 80U, static_cast<u32>(unitInputCount));
        writeU32(bytes, 84U, SourceImportWire::UnitInputEntryBytes);
        writeU64(bytes, 88U, unitInputsOffset);
        writeU32(bytes, 96U, static_cast<u32>(outputCount));
        writeU32(bytes, 100U, SourceImportWire::OutputEntryBytes);
        writeU64(bytes, 104U, outputsOffset);
        writeU64(bytes, 112U, stringBytes);
        writeU64(bytes, 120U, stringsOffset);
        writeU64(bytes, 128U, fileBytes);

        u64 pathOffset = stringsOffset;
        for (usize index = 0; index < desc.sources.size(); ++index)
        {
            const auto& source = desc.sources[index];
            const auto offset = static_cast<usize>(sourcesOffset) + index * SourceImportWire::SourceEntryBytes;
            writeFixed(bytes, offset, source.contentHash.bytes());
            writeU64(bytes, offset + 16U, source.fileBytes);
            writeU64(bytes, offset + 24U, pathOffset);
            writeU32(bytes, offset + 32U, static_cast<u32>(source.path.size()));
            std::copy(source.path.begin(), source.path.end(),
                      reinterpret_cast<char*>(bytes.data() + static_cast<usize>(pathOffset)));
            pathOffset += source.path.size();
        }

        u32 inputFirst = 0;
        u32 outputFirst = 0;
        for (usize unitIndex = 0; unitIndex < desc.units.size(); ++unitIndex)
        {
            const auto& unit = desc.units[unitIndex];
            const auto offset = static_cast<usize>(unitsOffset) + unitIndex * SourceImportWire::UnitEntryBytes;
            writeFixed(bytes, offset, unit.unitId.bytes());
            writeU32(bytes, offset + 16U, unit.importerKind);
            writeU32(bytes, offset + 20U, unit.importerVersion);
            writeFixed(bytes, offset + 24U, unit.settingsHash.bytes());
            writeU32(bytes, offset + 40U, inputFirst);
            writeU32(bytes, offset + 44U, static_cast<u32>(unit.inputs.size()));
            writeU32(bytes, offset + 48U, outputFirst);
            writeU32(bytes, offset + 52U, static_cast<u32>(unit.outputs.size()));

            for (usize localIndex = 0; localIndex < unit.inputs.size(); ++localIndex)
            {
                const auto inputOffset = static_cast<usize>(unitInputsOffset) +
                                         (static_cast<usize>(inputFirst) + localIndex) *
                                             SourceImportWire::UnitInputEntryBytes;
                writeU32(bytes, inputOffset, unit.inputs[localIndex].sourceIndex);
                writeU16(bytes, inputOffset + 4U, static_cast<u16>(unit.inputs[localIndex].flags));
            }
            for (usize localIndex = 0; localIndex < unit.outputs.size(); ++localIndex)
            {
                const auto outputOffset = static_cast<usize>(outputsOffset) +
                                          (static_cast<usize>(outputFirst) + localIndex) *
                                              SourceImportWire::OutputEntryBytes;
                writeFixed(bytes, outputOffset, unit.outputs[localIndex].assetId.bytes());
                writeU16(bytes, outputOffset + 16U, static_cast<u16>(unit.outputs[localIndex].assetKind));
            }
            inputFirst += static_cast<u32>(unit.inputs.size());
            outputFirst += static_cast<u32>(unit.outputs.size());
        }

        const auto validated = parseSourceImportMetadataView(bytes);
        if (!validated)
        {
            return Core::failure(validated.error());
        }
        return bytes;
    } catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "source import metadata write allocation failed");
    }
}

} // namespace Tina::AssetFormat
