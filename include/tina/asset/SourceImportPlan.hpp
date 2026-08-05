#pragma once

#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset_format/SourceImportMetadataFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>
#include <vector>

namespace Tina::Asset {

enum class SourceImportChangeKind : Core::u8 {
    Added = 0,
    Removed = 1,
    Reimport = 2,
};

struct SourceImportChange final {
    AssetFormat::SourceImportUnitId unitId{};
    SourceImportChangeKind kind = SourceImportChangeKind::Added;
};

struct SourceImportPlan final {
    std::pmr::vector<SourceImportChange> changes{};
    Core::u32 addedCount = 0;
    Core::u32 removedCount = 0;
    Core::u32 reimportCount = 0;
};

struct SourceImportPlanConfig final {
    std::pmr::memory_resource* memoryResource = nullptr;
    // Maximum number of rows across all change kinds. Zero permits only an empty plan.
    Core::u32 maxChanges = 0;
};

// Rejects import state that was produced for a different Catalog manifest. Call this before
// reusing any old cooked object; a mismatch requires a full current-schema recook.
[[nodiscard]] Core::Status validateSourceImportCatalogBinding(
    const AssetFormat::SourceImportMetadataView& metadata,
    AssetFormat::SourceImportManifestRevision catalogRevision);

// Requires every import-state output to match one Catalog entry by AssetId and kind, with no
// unowned Catalog entries. Parsed current-schema metadata already guarantees unique output owners.
[[nodiscard]] Core::Status validateSourceImportCatalogOutputs(
    const AssetFormat::SourceImportMetadataView& metadata,
    const CatalogSnapshot& catalog);

// Compares two validated, immutable tool-side import graphs. Matching units become Reimport when
// their importer contract, settings, source membership/content, target platform, or output contract
// changes. The planner never mutates either view and returns UnitId-sorted changes atomically.
[[nodiscard]] Core::Result<SourceImportPlan>
planSourceImports(const AssetFormat::SourceImportMetadataView& baseline,
                  const AssetFormat::SourceImportMetadataView& candidate,
                  SourceImportPlanConfig config);

} // namespace Tina::Asset
