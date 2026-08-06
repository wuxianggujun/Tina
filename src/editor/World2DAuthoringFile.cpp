#include <tina/editor/World2DAuthoringFile.hpp>

#include <tina/core/io/WriteFile.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>

#include <utility>

namespace Tina::Editor {

Core::Status saveWorld2DAuthoringDocument(std::string_view utf8Path,
                                          const World2DAuthoringDocument& document)
{
    auto status = Core::writeFile(utf8Path, document.snapshotBytes(),
                                  Core::WriteFileConfig{
                                      .atomicReplace = true,
                                      .createParents = true,
                                  });
    if (!status)
    {
        return Core::failure(
            std::move(status.error()).withContext("saveWorld2DAuthoringDocument", "replace"));
    }
    return Core::success();
}

} // namespace Tina::Editor
