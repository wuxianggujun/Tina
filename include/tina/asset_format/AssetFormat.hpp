#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/hash/ContentHash.hpp>
#include <tina/core/id/AssetId.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Tina::AssetFormat {

namespace Wire {

inline constexpr std::array<std::byte, 8> CookedAssetMagic{std::byte{'T'}, std::byte{'I'}, std::byte{'N'},
                                                           std::byte{'A'}, std::byte{'A'}, std::byte{'S'},
                                                           std::byte{'S'}, std::byte{'T'}};
inline constexpr std::array<std::byte, 8> CookedManifestMagic{std::byte{'T'}, std::byte{'I'}, std::byte{'N'},
                                                              std::byte{'A'}, std::byte{'M'}, std::byte{'N'},
                                                              std::byte{'F'}, std::byte{'T'}};

inline constexpr Core::u16 SchemaMajor = 1;
inline constexpr Core::u16 SchemaMinor = 0;
inline constexpr Core::u32 CookedAssetHeaderBytes = 112;
inline constexpr Core::u32 CookedManifestHeaderBytes = 64;
inline constexpr Core::u32 ManifestEntryBytes = 56;
inline constexpr Core::u32 DependencyEntryBytes = 24;

inline constexpr Core::u64 MaxCookedFileBytes = 1024ULL * 1024ULL * 1024ULL;
inline constexpr Core::u64 MaxPayloadBytes = 1024ULL * 1024ULL * 1024ULL;
inline constexpr Core::u64 MaxManifestFileBytes = 256ULL * 1024ULL * 1024ULL;
inline constexpr Core::u32 MaxDependenciesPerAsset = 4096;
inline constexpr Core::u32 MaxManifestEntries = 1'000'000;
inline constexpr Core::u32 MaxManifestDependencies = 4'000'000;
inline constexpr Core::u32 MaxPayloadAlignment = 4096;

} // namespace Wire

enum class AssetKind : Core::u16 {
    Invalid = 0,
    Texture2D = 1,
    Shader = 2,
    Font = 3,
    Sprite = 4,
    Tileset = 5,
    TileMap = 6,
    StaticMesh = 7,
    Material = 8,
    Prefab = 9,
    AudioClip = 10,
    SpriteAnimationClip = 11,
};

enum class TargetPlatform : Core::u16 {
    Invalid = 0,
    Any = 1,
    WindowsX64 = 2,
    LinuxX64 = 3,
};

enum class EndianTag : Core::u8 {
    Invalid = 0,
    Little = 1,
};

enum class HashAlgorithm : Core::u8 {
    Invalid = 0,
    Xxh3_128V1 = 1,
};

enum class DependencyFlags : Core::u16 {
    None = 0,
    Required = 1U << 0U,
};

struct CookedAssetLimits final {
    Core::u64 maxFileBytes = Wire::MaxCookedFileBytes;
    Core::u64 maxPayloadBytes = Wire::MaxPayloadBytes;
    Core::u32 maxDependencies = Wire::MaxDependenciesPerAsset;
    Core::u32 maxPayloadAlignment = Wire::MaxPayloadAlignment;
};

struct CookedManifestLimits final {
    Core::u64 maxFileBytes = Wire::MaxManifestFileBytes;
    Core::u32 maxEntries = Wire::MaxManifestEntries;
    Core::u32 maxDependencies = Wire::MaxManifestDependencies;
    Core::u32 maxDependenciesPerAsset = Wire::MaxDependenciesPerAsset;
    Core::u64 maxCookedAssetBytes = Wire::MaxCookedFileBytes;
};

struct AssetDependency final {
    Core::AssetId assetId;
    AssetKind expectedKind = AssetKind::Invalid;
    DependencyFlags flags = DependencyFlags::None;
};

struct CookedAssetHeader final {
    Core::u16 schemaMajor = 0;
    Core::u16 schemaMinor = 0;
    AssetKind assetKind = AssetKind::Invalid;
    Core::u16 assetTypeVersion = 0;
    TargetPlatform targetPlatform = TargetPlatform::Invalid;
    HashAlgorithm hashAlgorithm = HashAlgorithm::Invalid;
    Core::AssetId assetId;
    Core::ContentHash contentHash;
    Core::u64 dependencyOffset = 0;
    Core::u32 dependencyCount = 0;
    Core::u64 payloadOffset = 0;
    Core::u64 payloadBytes = 0;
    Core::u32 payloadAlignment = 0;
    Core::u64 fileBytes = 0;
};

struct CookedManifestHeader final {
    Core::u16 schemaMajor = 0;
    Core::u16 schemaMinor = 0;
    TargetPlatform targetPlatform = TargetPlatform::Invalid;
    HashAlgorithm hashAlgorithm = HashAlgorithm::Invalid;
    Core::u32 entryCount = 0;
    Core::u32 dependencyCount = 0;
    Core::u64 entriesOffset = 0;
    Core::u64 dependenciesOffset = 0;
    Core::u64 fileBytes = 0;
};

struct CookedManifestEntry final {
    Core::AssetId assetId;
    Core::ContentHash contentHash;
    AssetKind assetKind = AssetKind::Invalid;
    Core::u16 assetTypeVersion = 0;
    Core::u32 dependencyFirst = 0;
    Core::u32 dependencyCount = 0;
    Core::u64 cookedFileBytes = 0;
};

struct CookedArtifactPath final {
    static constexpr Core::usize CharacterCount = 55;

    std::array<char, CharacterCount + 1U> storage{};

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return {storage.data(), CharacterCount};
    }
};

class CookedAssetView final {
  public:
    CookedAssetView() noexcept = default;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return !m_bytes.empty();
    }
    [[nodiscard]] constexpr const CookedAssetHeader& header() const noexcept
    {
        return m_header;
    }
    [[nodiscard]] std::optional<AssetDependency> dependency(Core::u32 index) const noexcept;
    [[nodiscard]] std::span<const std::byte> payload() const noexcept;

  private:
    friend Core::Result<CookedAssetView> parseCookedAssetView(std::span<const std::byte>, CookedAssetLimits);

    CookedAssetView(std::span<const std::byte> bytes, CookedAssetHeader header) noexcept
        : m_bytes(bytes), m_header(header)
    {
    }

    std::span<const std::byte> m_bytes;
    CookedAssetHeader m_header;
};

class CookedManifestView final {
  public:
    CookedManifestView() noexcept = default;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return !m_bytes.empty();
    }
    [[nodiscard]] constexpr const CookedManifestHeader& header() const noexcept
    {
        return m_header;
    }
    [[nodiscard]] std::optional<CookedManifestEntry> entry(Core::u32 index) const noexcept;
    [[nodiscard]] std::optional<AssetDependency> dependency(Core::u32 index) const noexcept;
    [[nodiscard]] std::optional<AssetDependency> dependencyForEntry(Core::u32 entryIndex,
                                                                    Core::u32 dependencyIndex) const noexcept;

  private:
    friend Core::Result<CookedManifestView> parseCookedManifestView(std::span<const std::byte>, CookedManifestLimits);

    CookedManifestView(std::span<const std::byte> bytes, CookedManifestHeader header) noexcept
        : m_bytes(bytes), m_header(header)
    {
    }

    std::span<const std::byte> m_bytes;
    CookedManifestHeader m_header;
};

// Returned views borrow bytes. The caller must keep the complete byte span alive and unchanged.
[[nodiscard]] Core::Result<CookedAssetView> parseCookedAssetView(std::span<const std::byte> bytes,
                                                                 CookedAssetLimits limits = {});

[[nodiscard]] Core::Result<CookedManifestView> parseCookedManifestView(std::span<const std::byte> bytes,
                                                                       CookedManifestLimits limits = {});

[[nodiscard]] Core::Result<CookedArtifactPath> makeCookedArtifactPath(AssetKind assetKind, Core::AssetId assetId);

// Verifies that the cooked payload matches header.contentHash for Xxh3_128V1. Does not re-parse
// wire layout; the view must already come from parseCookedAssetView.
[[nodiscard]] Core::Status verifyCookedAssetContentHash(const CookedAssetView& asset);

// ---- Writer inputs (M10-A11 minimal Cooker-facing wire builders) ----

struct CookedAssetWriteDependency final {
    Core::AssetId assetId{};
    AssetKind expectedKind = AssetKind::Invalid;
    DependencyFlags flags = DependencyFlags::Required;
};

struct CookedAssetWriteDesc final {
    AssetKind assetKind = AssetKind::Invalid;
    Core::u16 assetTypeVersion = 1;
    TargetPlatform targetPlatform = TargetPlatform::WindowsX64;
    Core::AssetId assetId{};
    std::span<const CookedAssetWriteDependency> dependencies{};
    std::span<const std::byte> payload{};
    Core::u32 payloadAlignment = 16;
    // When true, contentHash is computed via Core XXH3-128 v1 over payload.
    bool computeContentHash = true;
    // Used only when computeContentHash == false.
    Core::ContentHash contentHash{};
};

struct CookedManifestWriteEntry final {
    Core::AssetId assetId{};
    Core::ContentHash contentHash{};
    AssetKind assetKind = AssetKind::Invalid;
    Core::u16 assetTypeVersion = 1;
    Core::u64 cookedFileBytes = 0;
    std::span<const CookedAssetWriteDependency> dependencies{};
};

struct CookedManifestWriteDesc final {
    TargetPlatform targetPlatform = TargetPlatform::WindowsX64;
    // Entries must already be sorted by ascending AssetId.
    std::span<const CookedManifestWriteEntry> entries{};
};

// Builds a complete little-endian cooked asset file. Uses new[]/delete[] owning buffer via vector.
[[nodiscard]] Core::Result<std::vector<std::byte>> writeCookedAssetBytes(const CookedAssetWriteDesc& desc);

// Builds a complete little-endian cooked manifest file. Entries must be AssetId-sorted.
[[nodiscard]] Core::Result<std::vector<std::byte>> writeCookedManifestBytes(const CookedManifestWriteDesc& desc);

} // namespace Tina::AssetFormat
