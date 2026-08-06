#include <tina/editor/World3DAuthoringFile.hpp>

#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/editor/World3DAuthoringDocument.hpp>

#include <memory_resource>
#include <utility>

namespace Tina::Editor {

Core::Status loadWorld3DAuthoringDocument(std::string_view utf8Path,
                                          World3DAuthoringDocument& document)
{
    const Core::u64 maximumFileBytes =
        AssetFormat::PrefabWire::HeaderBytes +
        static_cast<Core::u64>(document.config().nodeCapacity) *
            AssetFormat::PrefabWire::NodeBytes;
    auto bytes = Core::readFile(utf8Path, Core::ReadFileConfig{
                                              .maxBytes = maximumFileBytes,
                                              .memoryResource = std::pmr::new_delete_resource(),
                                          });
    if (!bytes) {
        return Core::failure(
            std::move(bytes.error()).withContext("loadWorld3DAuthoringDocument", "read"));
    }
    auto status = document.loadPayload(*bytes);
    if (!status) {
        return Core::failure(
            std::move(status.error()).withContext("loadWorld3DAuthoringDocument", "publish"));
    }
    return Tina::Core::success();
}

Core::Status saveWorld3DAuthoringDocument(std::string_view utf8Path,
                                          const World3DAuthoringDocument& document)
{
    auto status = Core::writeFile(utf8Path, document.payloadBytes(),
                                  Core::WriteFileConfig{
                                      .atomicReplace = true,
                                      .createParents = true,
                                  });
    if (!status) {
        return Core::failure(
            std::move(status.error()).withContext("saveWorld3DAuthoringDocument", "replace"));
    }
    return Tina::Core::success();
}

} // namespace Tina::Editor
