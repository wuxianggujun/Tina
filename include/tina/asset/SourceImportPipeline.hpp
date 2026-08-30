#pragma once

#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset/SourceImportProbe.hpp>
#include <tina/core/base/CancellationSignal.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Asset {

enum class SourceImportPipelineUnitKind : Core::u8 {
    CatalogRecipe = 0,
    Gltf = 1,
    Texture = 2,
    Audio = 3,
};

struct SourceImportPipelineUnit final {
    SourceImportPipelineUnitKind kind = SourceImportPipelineUnitKind::CatalogRecipe;
    std::string_view sourceUtf8Path{};
    GltfCookIds gltfIds{};
    // Optional stable output identity for one-output Texture/Audio importers.
    // Invalid keeps the default source-relative-path-derived identity.
    Core::AssetId mediaAssetId{};
};

struct SourceImportPipelineOutput final {
    Core::AssetId assetId{};
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
};

// Value-owned mapping for one intended source-import unit. The source path is the exact path
// supplied in SourceImportPipelineUnit (typically an absolute authoring path), while outputs are
// the committed Catalog objects owned by this unit. This mapping is returned for clean reuse as
// well as recook modes so callers never need to derive AssetIds or reopen import-state metadata.
struct SourceImportPipelineUnitOutput final {
    AssetFormat::SourceImportUnitId unitId{};
    std::string sourceUtf8Path{};
    std::vector<SourceImportPipelineOutput> outputs{};
};

enum class SourceImportPipelineMode : Core::u8 {
    CleanReuse = 0,
    FullRecook = 1,
    IncrementalRecook = 2,
};

struct SourceImportPipelineRequest final {
    std::string_view sourceRootUtf8{};
    AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::Invalid;
    // May be empty only to remove every unit from a valid baseline.
    std::span<const SourceImportPipelineUnit> units{};
    std::string_view baselineCatalogRootUtf8{};
    std::string_view baselineStateUtf8Path{};
    // Required together when a baseline Catalog already exists and the import is dirty.
    // The direct parent of stageCatalogRootUtf8 must already exist; the pipeline creates only
    // the Catalog root itself so callers can reserve and validate exclusive stage ownership.
    std::string_view stageCatalogRootUtf8{};
    std::string_view stageStateUtf8Path{};
    CatalogPackageStageConfig stageConfig{};
};

struct SourceImportPipelineResult final {
    CatalogSnapshot catalog{};
    SourceImportPipelineMode mode = SourceImportPipelineMode::FullRecook;
    SourceImportProbeState probeState = SourceImportProbeState::NoBaseline;
    SourceImportProbeReason probeReason = SourceImportProbeReason::StateNotFound;
    Core::u32 unitsTotal = 0;
    Core::u32 unitsRecooked = 0;
    Core::u32 unitsRemoved = 0;
    Core::u32 objectsReused = 0;
    Core::u32 objectsCooked = 0;
    Core::u64 cookedPayloadBytes = 0;
    Core::u32 catalogEntries = 0;
    Core::u32 catalogDependencies = 0;
    bool importStateCommitted = false;
    bool stageCreated = false;
    std::string catalogRootUtf8{};
    std::string stateUtf8Path{};
    std::vector<SourceImportPipelineUnitOutput> unitOutputs{};
};

// Synchronously probes the complete intended unit set, cooks only dirty units, builds and fully
// validates an immutable fresh stage, then commits state bound to that stage revision. The returned
// CatalogSnapshot is the exact fully validated package and can be moved into AssetSystem at an
// owner-thread safe point without reopening the package. The function does not touch AssetSystem/UI.
// Cancellation is a Core::CancellationToken rather than a std::stop_token because libc++
// keeps stop_token experimental through NDK 28; an empty token never cancels.
[[nodiscard]] Core::Result<SourceImportPipelineResult>
executeSourceImportPipeline(const SourceImportPipelineRequest& request,
                            Core::CancellationToken cancellation = {}) noexcept;

} // namespace Tina::Asset
