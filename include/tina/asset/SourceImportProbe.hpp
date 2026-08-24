#pragma once

#include <tina/asset/SourceImportCapture.hpp>
#include <tina/asset_format/SourceImportMetadataFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/hash/ContentHash.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Asset {

struct GltfCookIds;

enum class SourceImportProbeState : Core::u8 {
    Clean = 0,
    Dirty = 1,
    NoBaseline = 2,
};

enum class SourceImportProbeReason : Core::u8 {
    None = 0,
    StateNotFound = 1,
    CatalogRevisionChanged = 2,
    UnitNotFound = 3,
    ImporterKindChanged = 4,
    ImporterVersionChanged = 5,
    SettingsChanged = 6,
    PrimarySourceChanged = 7,
    SourceSizeChanged = 8,
    SourceContentChanged = 9,
    UnitSetChanged = 10,
    StateSchemaChanged = 11,
    CatalogNotFound = 12,
    CatalogOutputChanged = 13,
};

[[nodiscard]] constexpr std::string_view
sourceImportProbeReasonName(SourceImportProbeReason reason) noexcept
{
    switch (reason)
    {
    case SourceImportProbeReason::None:
        return "none";
    case SourceImportProbeReason::StateNotFound:
        return "state-not-found";
    case SourceImportProbeReason::CatalogRevisionChanged:
        return "catalog-revision-changed";
    case SourceImportProbeReason::UnitNotFound:
        return "unit-not-found";
    case SourceImportProbeReason::ImporterKindChanged:
        return "importer-kind-changed";
    case SourceImportProbeReason::ImporterVersionChanged:
        return "importer-version-changed";
    case SourceImportProbeReason::SettingsChanged:
        return "settings-changed";
    case SourceImportProbeReason::PrimarySourceChanged:
        return "primary-source-changed";
    case SourceImportProbeReason::SourceSizeChanged:
        return "source-size-changed";
    case SourceImportProbeReason::SourceContentChanged:
        return "source-content-changed";
    case SourceImportProbeReason::UnitSetChanged:
        return "unit-set-changed";
    case SourceImportProbeReason::StateSchemaChanged:
        return "state-schema-changed";
    case SourceImportProbeReason::CatalogNotFound:
        return "catalog-not-found";
    case SourceImportProbeReason::CatalogOutputChanged:
        return "catalog-output-changed";
    }
    return "unknown";
}

struct SourceImportUnitContract final {
    AssetFormat::SourceImportUnitId unitId{};
    SourceImporterKind importerKind = SourceImporterKind::CatalogRecipe;
    Core::u32 importerVersion = 0;
    Core::ContentHash settingsHash{};
};

struct SourceImportUnitProbeDesc final {
    std::string sourceRootUtf8{};
    std::string primarySourceUtf8Path{};
    std::string normalizedPrimarySourcePath{};
    SourceImportUnitContract expected{};
};

struct SourceImportProbeResult final {
    SourceImportProbeState state = SourceImportProbeState::Dirty;
    SourceImportProbeReason reason = SourceImportProbeReason::None;
    Core::u32 cleanUnitCount = 0;
    Core::u32 cleanObjectCount = 0;
};

struct SourceImportBatchProbeResult final {
    // One result per input description, in caller order.
    std::vector<SourceImportProbeResult> units{};
    Core::u32 cleanUnitCount = 0;
    Core::u32 dirtyUnitCount = 0;
    Core::u32 removedUnitCount = 0;
    Core::u32 cleanObjectCount = 0;
};

// These helpers are the single current importer-contract source for both the Cooker and clean
// probe. normalizedPrimarySourcePath is the canonical source-root-relative metadata locator.
[[nodiscard]] Core::Result<SourceImportUnitContract>
currentCatalogRecipeSourceImportContract(
    std::string_view normalizedPrimarySourcePath,
    AssetFormat::TargetPlatform targetPlatform);

[[nodiscard]] Core::Result<SourceImportUnitContract>
currentGltfSourceImportContract(std::string_view normalizedPrimarySourcePath,
                                const GltfCookIds& ids);

// Plain media importers: one image file (Texture2D, directly usable by Sprite2D)
// or one PCM WAV file (AudioClip), with deterministic path-derived output
// AssetIds.
[[nodiscard]] Core::Result<SourceImportUnitContract>
currentTextureSourceImportContract(std::string_view normalizedPrimarySourcePath);

[[nodiscard]] Core::Result<SourceImportUnitContract>
currentAudioSourceImportContract(std::string_view normalizedPrimarySourcePath);

[[nodiscard]] Core::Result<SourceImportUnitProbeDesc>
makeCatalogRecipeSourceImportProbeDesc(
    std::string_view sourceRootUtf8,
    std::string_view primarySourceUtf8Path,
    AssetFormat::TargetPlatform targetPlatform);

[[nodiscard]] Core::Result<SourceImportUnitProbeDesc>
makeGltfSourceImportProbeDesc(std::string_view sourceRootUtf8,
                              std::string_view primarySourceUtf8Path,
                              const GltfCookIds& ids);

[[nodiscard]] Core::Result<SourceImportUnitProbeDesc>
makeTextureSourceImportProbeDesc(std::string_view sourceRootUtf8,
                                 std::string_view primarySourceUtf8Path);

[[nodiscard]] Core::Result<SourceImportUnitProbeDesc>
makeAudioSourceImportProbeDesc(std::string_view sourceRootUtf8,
                               std::string_view primarySourceUtf8Path);

// Contract mismatches and source fingerprint changes are normal Dirty results. Invalid input,
// unsafe final paths, source IO failures, and replacement during a snapshot are structured errors.
[[nodiscard]] Core::Result<SourceImportProbeResult>
probeSourceImportUnit(const AssetFormat::SourceImportMetadataView& baseline,
                      AssetFormat::SourceImportManifestRevision currentCatalogRevision,
                      const SourceImportUnitProbeDesc& desc);

// Probes an intended unit set against one baseline. The intended set may be empty to remove every
// baseline unit. Expected UnitIds must be unique. Units absent from the intended set are counted as
// removed; matching units may still remain clean and reusable.
[[nodiscard]] Core::Result<SourceImportBatchProbeResult>
probeSourceImportUnits(const AssetFormat::SourceImportMetadataView& baseline,
                       AssetFormat::SourceImportManifestRevision currentCatalogRevision,
                       std::span<const SourceImportUnitProbeDesc> descs);

// Reads and validates the current-only state file. A missing state path is NoBaseline; malformed or
// unreadable state is an error. The returned metadata view never escapes this call.
[[nodiscard]] Core::Result<SourceImportProbeResult>
probeSourceImportState(std::string_view stateUtf8Path,
                       AssetFormat::SourceImportManifestRevision currentCatalogRevision,
                       const SourceImportUnitProbeDesc& desc);

// Recipe settings include TargetPlatform. Before parsing the recipe, this wrapper obtains it from
// the validated baseline; a platform edit necessarily changes the primary source fingerprint.
[[nodiscard]] Core::Result<SourceImportProbeResult>
probeCatalogRecipeSourceImportState(
    std::string_view stateUtf8Path,
    AssetFormat::SourceImportManifestRevision currentCatalogRevision,
    std::string_view sourceRootUtf8,
    std::string_view primarySourceUtf8Path);

[[nodiscard]] Core::Result<SourceImportProbeResult>
probeGltfSourceImportState(std::string_view stateUtf8Path,
                           AssetFormat::SourceImportManifestRevision currentCatalogRevision,
                           std::string_view sourceRootUtf8,
                           std::string_view primarySourceUtf8Path,
                           const GltfCookIds& ids);

[[nodiscard]] Core::Result<SourceImportProbeResult>
probeGltfSourceImportState(std::string_view stateUtf8Path,
                           AssetFormat::SourceImportManifestRevision currentCatalogRevision,
                           std::string_view sourceRootUtf8,
                           std::string_view primarySourceUtf8Path);

} // namespace Tina::Asset
