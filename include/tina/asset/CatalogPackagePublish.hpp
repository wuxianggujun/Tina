#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/io/PackageFile.hpp>

#include <span>
#include <string_view>

namespace Tina::Asset {

struct CatalogPackagePublishConfig final {
    Core::PackageWriteConfig write{};
};

struct CatalogPackageObjectBlob final {
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Core::AssetId assetId{};
    std::span<const std::byte> bytes{};
};

// Atomically replaces one .pck containing manifest.tmnft and every supplied object. Input
// buffers are borrowed for this call. Old readers keep their previous immutable mapping.
[[nodiscard]] Core::Status publishCatalogPackage(std::string_view catalogRootUtf8,
                                                 std::string_view packageRelativePath,
                                                 std::span<const std::byte> manifestBytes,
                                                 std::span<const CatalogPackageObjectBlob> objects,
                                                 CatalogPackagePublishConfig config = {});

} // namespace Tina::Asset
