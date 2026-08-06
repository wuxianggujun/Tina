#pragma once

#include <tina/core/error/Result.hpp>

#include <string_view>

namespace Tina::Editor {

class World3DAuthoringDocument;

// Reads a current Prefab payload schema and atomically installs it as the
// document baseline. Any read, schema, or capacity failure preserves the
// current document and its undo/redo history.
[[nodiscard]] Core::Status
loadWorld3DAuthoringDocument(std::string_view utf8Path,
                             World3DAuthoringDocument& document);

// Atomically replaces utf8Path with the current canonical Prefab payload.
[[nodiscard]] Core::Status
saveWorld3DAuthoringDocument(std::string_view utf8Path,
                             const World3DAuthoringDocument& document);

} // namespace Tina::Editor
