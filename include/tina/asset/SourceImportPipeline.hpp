#pragma once

#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/GltfCook.hpp>
#include <tina/asset/SourceImportProbe.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <stop_token>
#include <string>
#include <string_view>

namespace Tina::Asset {

enum class SourceImportPipelineUnitKind : Core::u8 {
    CatalogRecipe = 0,
    Gltf = 1,
};

struct SourceImportPipelineUnit final {
    SourceImportPipelineUnitKind kind = SourceImportPipelineUnitKind::CatalogRecipe;
    std::string_view sourceUtf8Path{};
    GltfCookIds gltfIds{};
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
    SourceImportPipelineMode mode = SourceImportPipelineMode::FullRecook;
    SourceImportProbeState probeState = SourceImportProbeState::NoBaseline;
    SourceImportProbeReason probeReason = SourceImportProbeReason::StateNotFound;
    Core::u32 unitsTotal = 0;
    Core::u32 unitsRecooked = 0;
    Core::u32 unitsRemoved = 0;
    Core::u32 objectsReused = 0;
    Core::u32 objectsCooked = 0;
    Core::u32 catalogEntries = 0;
    Core::u32 catalogDependencies = 0;
    bool importStateCommitted = false;
    bool stageCreated = false;
    std::string catalogRootUtf8{};
    std::string stateUtf8Path{};
};

// Synchronously probes the complete intended unit set, cooks only dirty units, builds and fully
// validates an immutable fresh stage, then commits state bound to that stage revision. The function
// does not replace the live Catalog or touch AssetSystem/UI; hosts may run it on a worker and reload
// result.catalogRootUtf8 at a later owner-thread safe point.
[[nodiscard]] Core::Result<SourceImportPipelineResult>
executeSourceImportPipeline(const SourceImportPipelineRequest& request,
                            std::stop_token stopToken = {}) noexcept;

} // namespace Tina::Asset
