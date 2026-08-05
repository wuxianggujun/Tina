#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/hash/ContentHash.hpp>
#include <tina/core/id/AssetId.hpp>

#include <array>
#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::AssetFormat {

namespace SourceImportWire {

inline constexpr std::array<std::byte, 8> Magic{std::byte{'T'}, std::byte{'I'}, std::byte{'N'}, std::byte{'A'},
                                                std::byte{'I'}, std::byte{'M'}, std::byte{'P'}, std::byte{'T'}};

inline constexpr Core::u16 SchemaMajor = 1;
inline constexpr Core::u16 SchemaMinor = 1;
inline constexpr Core::u32 HeaderBytes = 144;
inline constexpr Core::u32 SourceEntryBytes = 40;
inline constexpr Core::u32 UnitEntryBytes = 64;
inline constexpr Core::u32 UnitInputEntryBytes = 8;
inline constexpr Core::u32 OutputEntryBytes = 24;

inline constexpr Core::u64 MaxFileBytes = 512ULL * 1024ULL * 1024ULL;
inline constexpr Core::u64 MaxStringBytes = 256ULL * 1024ULL * 1024ULL;
inline constexpr Core::u32 MaxPathBytes = 4096;
inline constexpr Core::u32 MaxSources = 1'000'000;
inline constexpr Core::u32 MaxUnits = 1'000'000;
inline constexpr Core::u32 MaxUnitInputs = 4'000'000;
inline constexpr Core::u32 MaxOutputs = 1'000'000;

} // namespace SourceImportWire

class SourceImportUnitId final {
  public:
    using Bytes = std::array<std::byte, 16>;
    using CanonicalText = std::array<char, 32>;

    constexpr SourceImportUnitId() noexcept = default;

    [[nodiscard]] static constexpr std::optional<SourceImportUnitId> fromBytes(Bytes bytes) noexcept
    {
        if (isZero(bytes))
        {
            return std::nullopt;
        }
        return SourceImportUnitId(bytes);
    }

    [[nodiscard]] static constexpr std::optional<SourceImportUnitId> parseCanonical(std::string_view text) noexcept
    {
        if (text.size() != 32U)
        {
            return std::nullopt;
        }

        Bytes bytes{};
        for (Core::usize index = 0; index < bytes.size(); ++index)
        {
            const auto high = hexValue(text[index * 2U]);
            const auto low = hexValue(text[index * 2U + 1U]);
            if (!high || !low)
            {
                return std::nullopt;
            }
            bytes[index] = static_cast<std::byte>((*high << 4U) | *low);
        }
        return fromBytes(bytes);
    }

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return !isZero(m_bytes);
    }
    explicit constexpr operator bool() const noexcept
    {
        return hasValue();
    }

    [[nodiscard]] constexpr const Bytes& bytes() const noexcept
    {
        return m_bytes;
    }

    [[nodiscard]] constexpr CanonicalText canonicalText() const noexcept
    {
        constexpr char Digits[] = "0123456789abcdef";
        CanonicalText text{};
        for (Core::usize index = 0; index < m_bytes.size(); ++index)
        {
            const auto value = std::to_integer<unsigned int>(m_bytes[index]);
            text[index * 2U] = Digits[(value >> 4U) & 0x0FU];
            text[index * 2U + 1U] = Digits[value & 0x0FU];
        }
        return text;
    }

    auto operator<=>(const SourceImportUnitId&) const = default;

  private:
    [[nodiscard]] static constexpr bool isZero(const Bytes& bytes) noexcept
    {
        for (const auto value : bytes)
        {
            if (value != std::byte{0})
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr std::optional<unsigned int> hexValue(char value) noexcept
    {
        if (value >= '0' && value <= '9')
        {
            return static_cast<unsigned int>(value - '0');
        }
        if (value >= 'a' && value <= 'f')
        {
            return static_cast<unsigned int>(value - 'a') + 10U;
        }
        return std::nullopt;
    }

    explicit constexpr SourceImportUnitId(Bytes bytes) noexcept : m_bytes(bytes)
    {
    }

    Bytes m_bytes{};
};

enum class SourceImportInputFlags : Core::u16 {
    None = 0,
    Primary = 1U << 0U,
};

enum class SourceImportReadExtent : Core::u32 {
    Invalid = 0,
    WholeFile = 1,
    Prefix = 2,
};

[[nodiscard]] constexpr bool hasSourceImportInputFlag(SourceImportInputFlags value,
                                                       SourceImportInputFlags flag) noexcept
{
    return (static_cast<Core::u16>(value) & static_cast<Core::u16>(flag)) == static_cast<Core::u16>(flag);
}

struct SourceImportMetadataLimits final {
    Core::u64 maxFileBytes = SourceImportWire::MaxFileBytes;
    Core::u64 maxStringBytes = SourceImportWire::MaxStringBytes;
    Core::u32 maxPathBytes = SourceImportWire::MaxPathBytes;
    Core::u32 maxSources = SourceImportWire::MaxSources;
    Core::u32 maxUnits = SourceImportWire::MaxUnits;
    Core::u32 maxUnitInputs = SourceImportWire::MaxUnitInputs;
    Core::u32 maxOutputs = SourceImportWire::MaxOutputs;
};

struct SourceImportManifestRevision final {
    Core::ContentHash manifestDigest{};
    Core::u64 manifestBytes = 0;

    auto operator<=>(const SourceImportManifestRevision&) const = default;
};

struct SourceImportMetadataHeader final {
    Core::u16 schemaMajor = 0;
    Core::u16 schemaMinor = 0;
    TargetPlatform targetPlatform = TargetPlatform::Invalid;
    HashAlgorithm hashAlgorithm = HashAlgorithm::Invalid;
    SourceImportManifestRevision manifestRevision{};
    Core::u32 sourceCount = 0;
    Core::u32 unitCount = 0;
    Core::u32 unitInputCount = 0;
    Core::u32 outputCount = 0;
    Core::u64 sourcesOffset = 0;
    Core::u64 unitsOffset = 0;
    Core::u64 unitInputsOffset = 0;
    Core::u64 outputsOffset = 0;
    Core::u64 stringsOffset = 0;
    Core::u64 stringBytes = 0;
    Core::u64 fileBytes = 0;
};

struct SourceImportSource final {
    Core::ContentHash contentHash{};
    Core::u64 fileBytes = 0;
    Core::u64 pathOffset = 0;
    Core::u32 pathBytes = 0;
    SourceImportReadExtent readExtent = SourceImportReadExtent::Invalid;
};

struct SourceImportUnit final {
    SourceImportUnitId unitId{};
    Core::u32 importerKind = 0;
    Core::u32 importerVersion = 0;
    Core::ContentHash settingsHash{};
    Core::u32 inputFirst = 0;
    Core::u32 inputCount = 0;
    Core::u32 outputFirst = 0;
    Core::u32 outputCount = 0;
};

struct SourceImportUnitInput final {
    Core::u32 sourceIndex = 0;
    SourceImportInputFlags flags = SourceImportInputFlags::None;
};

struct SourceImportOutput final {
    Core::AssetId assetId{};
    AssetKind assetKind = AssetKind::Invalid;
};

class SourceImportMetadataView final {
  public:
    SourceImportMetadataView() noexcept = default;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return !m_bytes.empty();
    }
    [[nodiscard]] constexpr const SourceImportMetadataHeader& header() const noexcept
    {
        return m_header;
    }
    [[nodiscard]] std::optional<SourceImportSource> source(Core::u32 index) const noexcept;
    [[nodiscard]] std::optional<std::string_view> sourcePath(Core::u32 index) const noexcept;
    [[nodiscard]] std::optional<SourceImportUnit> unit(Core::u32 index) const noexcept;
    [[nodiscard]] std::optional<SourceImportUnitInput> unitInput(Core::u32 index) const noexcept;
    [[nodiscard]] std::optional<SourceImportUnitInput> unitInputForUnit(Core::u32 unitIndex,
                                                                       Core::u32 inputIndex) const noexcept;
    [[nodiscard]] std::optional<SourceImportOutput> output(Core::u32 index) const noexcept;
    [[nodiscard]] std::optional<SourceImportOutput> outputForUnit(Core::u32 unitIndex,
                                                                  Core::u32 outputIndex) const noexcept;

  private:
    friend Core::Result<SourceImportMetadataView>
    parseSourceImportMetadataView(std::span<const std::byte>, SourceImportMetadataLimits);

    SourceImportMetadataView(std::span<const std::byte> bytes, SourceImportMetadataHeader header) noexcept
        : m_bytes(bytes), m_header(header)
    {
    }

    std::span<const std::byte> m_bytes;
    SourceImportMetadataHeader m_header;
};

// Returned views borrow bytes. The caller must keep the complete byte span alive and unchanged.
[[nodiscard]] Core::Result<SourceImportMetadataView>
parseSourceImportMetadataView(std::span<const std::byte> bytes, SourceImportMetadataLimits limits = {});

struct SourceImportMetadataWriteSource final {
    std::string_view path;
    Core::ContentHash contentHash{};
    Core::u64 fileBytes = 0;
    SourceImportReadExtent readExtent = SourceImportReadExtent::Invalid;
};

struct SourceImportMetadataWriteInput final {
    Core::u32 sourceIndex = 0;
    SourceImportInputFlags flags = SourceImportInputFlags::None;
};

struct SourceImportMetadataWriteOutput final {
    Core::AssetId assetId{};
    AssetKind assetKind = AssetKind::Invalid;
};

struct SourceImportMetadataWriteUnit final {
    SourceImportUnitId unitId{};
    Core::u32 importerKind = 0;
    Core::u32 importerVersion = 0;
    Core::ContentHash settingsHash{};
    // Inputs must be strictly sourceIndex-sorted and contain exactly one Primary edge.
    std::span<const SourceImportMetadataWriteInput> inputs{};
    // Outputs must be strictly AssetId-sorted. Asset ownership must be unique across all units.
    std::span<const SourceImportMetadataWriteOutput> outputs{};
};

struct SourceImportMetadataWriteDesc final {
    TargetPlatform targetPlatform = TargetPlatform::WindowsX64;
    SourceImportManifestRevision manifestRevision{};
    // Sources must be strictly sorted by normalized UTF-8 path bytes.
    std::span<const SourceImportMetadataWriteSource> sources{};
    // Units must be strictly SourceImportUnitId-sorted.
    std::span<const SourceImportMetadataWriteUnit> units{};
};

// Builds a complete canonical little-endian import-state.tmeta file.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeSourceImportMetadataBytes(const SourceImportMetadataWriteDesc& desc);

} // namespace Tina::AssetFormat
