#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/error/Result.hpp>

#include <string_view>

namespace Tina::Editor {

class Fx2DAuthoringDocument;

[[nodiscard]] Core::Status saveFx2DAuthoringDocument(
    std::string_view utf8Path,
    const Fx2DAuthoringDocument& document,
    Core::AssetId assetId,
    AssetFormat::TargetPlatform platform = AssetFormat::TargetPlatform::WindowsX64);

} // namespace Tina::Editor
