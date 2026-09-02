#include <tina/editor/SpriteAnimationAuthoringFile.hpp>

#include <tina/core/io/WriteFile.hpp>
#include <tina/editor/SpriteAnimationAuthoringDocument.hpp>

#include <utility>

namespace Tina::Editor {

Core::Status saveSpriteAnimationAuthoringDocument(
    std::string_view utf8Path,
    const SpriteAnimationAuthoringDocument& document,
    AssetFormat::TargetPlatform platform)
{
    auto preview = document.cookPreview(platform);
    if (!preview) {
        return Core::failure(std::move(preview.error()).withContext(
            "saveSpriteAnimationAuthoringDocument", "cookPreview"));
    }
    auto status = Core::writeFile(
        utf8Path, preview->cookedBytes,
        Core::WriteFileConfig{.atomicReplace = true, .createParents = true});
    if (!status) {
        return Core::failure(std::move(status.error()).withContext(
            "saveSpriteAnimationAuthoringDocument", "writeCookedArtifact"));
    }
    return Core::success();
}

} // namespace Tina::Editor
