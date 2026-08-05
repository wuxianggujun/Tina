#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace Tina::Asset {

struct CatalogPackagePublishConfig final {
    Core::WriteFileConfig write{};
    // When true (default), also write cooked object files under catalogRoot.
    bool writeObjects = true;
};

struct CatalogPackageObjectBlob final {
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Core::AssetId assetId{};
    std::span<const std::byte> bytes{};
};

// Writes manifest.tmnft (or relative path) and optional object blobs under catalogRoot.
// Object relative paths use makeCookedArtifactPath and the manifest is written last. This is a
// best-effort in-place publish, not a multi-file transaction; use cookAndStageCatalogPackage when
// replacing content for an existing AssetId.
[[nodiscard]] Core::Status publishCatalogPackage(std::string_view catalogRootUtf8,
                                                 std::string_view manifestRelativePath,
                                                 std::span<const std::byte> manifestBytes,
                                                 std::span<const CatalogPackageObjectBlob> objects,
                                                 CatalogPackagePublishConfig config = {});

} // namespace Tina::Asset
