#include <tina/editor/Fx2DAuthoringFile.hpp>

#include <tina/asset_format/Fx2DPayload.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/editor/Fx2DAuthoringDocument.hpp>

namespace Tina::Editor {

Core::Status saveFx2DAuthoringDocument(
    std::string_view utf8Path,
    const Fx2DAuthoringDocument& document,
    Core::AssetId assetId,
    AssetFormat::TargetPlatform platform)
{
    auto cooked = AssetFormat::writeCookedFx2DAsset(
        assetId, document.value(), platform);
    if (!cooked) {
        return Core::failure(std::move(cooked.error()).withContext(
            "saveFx2DAuthoringDocument", "writeCookedFx2DAsset"));
    }
    auto status = Core::writeFile(
        utf8Path, *cooked,
        Core::WriteFileConfig{.atomicReplace = true, .createParents = true});
    if (!status) {
        return Core::failure(std::move(status.error()).withContext(
            "saveFx2DAuthoringDocument", "writeCookedArtifact"));
    }
    return Core::success();
}

} // namespace Tina::Editor
