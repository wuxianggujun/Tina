#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/error/Result.hpp>

#include <string_view>

namespace Tina::Editor {

class SpriteAnimationAuthoringDocument;

// Cooks the current SpriteAnimationClip schema and atomically replaces utf8Path
// with that canonical Cooked asset. No editor-only persistence format is used.
[[nodiscard]] Core::Status saveSpriteAnimationAuthoringDocument(
    std::string_view utf8Path,
    const SpriteAnimationAuthoringDocument& document,
    AssetFormat::TargetPlatform platform = AssetFormat::TargetPlatform::WindowsX64);

} // namespace Tina::Editor
