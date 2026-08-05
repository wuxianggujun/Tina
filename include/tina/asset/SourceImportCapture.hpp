#pragma once

#include <tina/asset_format/SourceImportMetadataFormat.hpp>
#include <tina/core/error/Result.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Asset {

enum class SourceImporterKind : Core::u32 {
    CatalogRecipe = 1,
    Gltf = 2,
};

struct SourceImportCaptureConfig final {
    // Every captured locator must remain below this authoring root after lexical normalization.
    std::string_view sourceRootUtf8{};
    Core::u32 maxSources = 4096;
};

struct SourceImportCapturedSource final {
    std::string path{};
    Core::ContentHash contentHash{};
    Core::u64 fileBytes = 0;
    AssetFormat::SourceImportReadExtent readExtent = AssetFormat::SourceImportReadExtent::Invalid;
};

struct SourceImportCapturedInput final {
    Core::u32 sourceIndex = 0;
    AssetFormat::SourceImportInputFlags flags = AssetFormat::SourceImportInputFlags::None;
};

struct SourceImportCapturedOutput final {
    Core::AssetId assetId{};
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
};

struct SourceImportCapturedUnit final {
    AssetFormat::SourceImportUnitId unitId{};
    SourceImporterKind importerKind = SourceImporterKind::CatalogRecipe;
    Core::u32 importerVersion = 0;
    Core::ContentHash settingsHash{};
    std::vector<SourceImportCapturedInput> inputs{};
    std::vector<SourceImportCapturedOutput> outputs{};
};

struct SourceImportCandidate final {
    AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::WindowsX64;
    std::vector<SourceImportCapturedSource> sources{};
    std::vector<SourceImportCapturedUnit> units{};
};

// Returns a canonical '/'-separated locator relative to config.sourceRootUtf8. The requested
// locator is normalized lexically, so symlink identity is not silently replaced by a final path.
[[nodiscard]] Core::Result<std::string>
normalizeSourceImportPath(const SourceImportCaptureConfig& config, std::string_view sourceUtf8Path);

// Hashes the exact caller-provided bytes, records their explicit read extent, and
// appends/deduplicates one source. This function never opens or rereads sourceUtf8Path. The returned
// index remains valid until candidate.sources changes.
[[nodiscard]] Core::Result<Core::u32>
captureSourceImportBytes(SourceImportCandidate& candidate,
                         const SourceImportCaptureConfig& config,
                         std::string_view sourceUtf8Path,
                         AssetFormat::SourceImportReadExtent readExtent,
                         std::span<const std::byte> consumedBytes);

// Unit identity is derived only from the importer kind and canonical primary path. Importer
// settings belong in settingsHash, so changing settings triggers Reimport without changing identity.
[[nodiscard]] Core::Result<AssetFormat::SourceImportUnitId>
deriveSourceImportUnitId(SourceImporterKind importerKind, std::string_view primarySourcePath);

[[nodiscard]] Core::Result<Core::ContentHash>
digestSourceImportSettings(std::span<const std::byte> canonicalSettingsBytes);

// Canonicalizes source/unit/input/output ordering, remaps input indexes, then builds the current
// TINAIMPT bytes. candidate remains unchanged.
[[nodiscard]] Core::Result<std::vector<std::byte>>
writeSourceImportCandidateBytes(const SourceImportCandidate& candidate,
                                AssetFormat::SourceImportManifestRevision manifestRevision);

// Atomically replaces stateUtf8Path with a complete current-schema state file. Callers commit only
// after the corresponding Catalog package has completed full validation.
[[nodiscard]] Core::Status
commitSourceImportCandidate(std::string_view stateUtf8Path,
                            const SourceImportCandidate& candidate,
                            AssetFormat::SourceImportManifestRevision manifestRevision);

} // namespace Tina::Asset
