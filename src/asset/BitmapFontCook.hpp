#pragma once
#include <tina/asset/CatalogCook.hpp>
#include <functional>

namespace Tina::Asset::Detail {
// Source JSON and PNG reading belongs to the cooker. The callback performs the
// recipe's existing path containment, snapshot capture and dependency tracking.
[[nodiscard]] Core::Result<std::vector<CatalogCookAssetSpec>> cookBitmapFontSource(
    Core::AssetId fontId, std::span<const std::byte> source,
    const std::function<Core::Result<std::vector<std::byte>>(std::string_view)>& readPage);
}
