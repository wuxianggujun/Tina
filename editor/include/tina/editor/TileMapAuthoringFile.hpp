#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <string_view>

namespace Tina::Editor {

class TileMapAuthoringDocument;

struct TileMapAuthoringSaveResult final {
    Core::usize artifactCount = 0;
    Core::u64 byteCount = 0;
};

// Cooks the current TileMap root/chunk schema family under utf8Root. Each
// canonical Cooked artifact uses its AssetFormat relative path and atomic
// sibling replacement. Chunk artifacts are published before the root commit
// marker. No editor-only persistence format is used.
[[nodiscard]] Core::Result<TileMapAuthoringSaveResult>
saveTileMapAuthoringDocument(
    std::string_view utf8Root,
    const TileMapAuthoringDocument& document,
    AssetFormat::TargetPlatform platform = AssetFormat::TargetPlatform::WindowsX64);

} // namespace Tina::Editor
