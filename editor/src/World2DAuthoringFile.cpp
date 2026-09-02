#include <tina/editor/World2DAuthoringFile.hpp>

#include <tina/asset_format/World2DSnapshot.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>

#include <memory_resource>
#include <utility>

namespace Tina::Editor {

Core::Status loadWorld2DAuthoringDocument(std::string_view utf8Path,
                                          World2DAuthoringDocument& document)
{
    const World2DAuthoringDocumentConfig& documentConfig = document.config();
    const Core::u64 maximumFileBytes =
        AssetFormat::World2DSnapshotWire::HeaderBytes +
        static_cast<Core::u64>(documentConfig.entityCapacity) *
            AssetFormat::World2DSnapshotWire::EntityBytes +
        documentConfig.gameplayByteCapacity;
    auto bytes = Core::readFile(utf8Path, Core::ReadFileConfig{
                                              .maxBytes = maximumFileBytes,
                                              .memoryResource = std::pmr::new_delete_resource(),
                                          });
    if (!bytes)
    {
        return Core::failure(
            std::move(bytes.error()).withContext("loadWorld2DAuthoringDocument", "read"));
    }
    auto status = document.loadSnapshot(*bytes);
    if (!status)
    {
        return Core::failure(
            std::move(status.error()).withContext("loadWorld2DAuthoringDocument", "publish"));
    }
    return Core::success();
}

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
