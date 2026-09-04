#pragma once

#include <tina/asset/SourceImportCapture.hpp>
#include <tina/asset_format/SourceImportMetadataFormat.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <vector>

namespace Tina::Asset {

struct SourceImportCandidateComposeDesc final {
    // Required when retainedUnitIds is non-empty or both intended input spans are empty. The view
    // remains borrowed for this call only.
    const AssetFormat::SourceImportMetadataView* baseline = nullptr;
    std::span<const AssetFormat::SourceImportUnitId> retainedUnitIds{};
    std::span<const SourceImportCandidate> recookedCandidates{};
};

struct SourceImportCandidateComposeResult final {
    SourceImportCandidate candidate{};
    // AssetIds owned by retained baseline units, sorted for incremental package assembly.
    std::vector<Core::AssetId> retainedAssetIds{};
};

// Produces one complete candidate from clean baseline units plus recooked units. Source locators are
// deduplicated only when their fingerprint/read extent agree. UnitIds and output AssetIds remain
// globally unique; failures return no partial candidate. A valid baseline with no retained or
// recooked units produces the canonical empty candidate used when every unit is removed.
[[nodiscard]] Core::Result<SourceImportCandidateComposeResult>
composeSourceImportCandidate(const SourceImportCandidateComposeDesc& desc);

} // namespace Tina::Asset
