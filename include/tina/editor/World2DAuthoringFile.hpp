#pragma once

#include <tina/core/error/Result.hpp>

#include <string_view>

namespace Tina::Editor {

class World2DAuthoringDocument;

// Synchronously reads a current-schema World2D snapshot and atomically installs
// it as the document baseline. Read, schema, or document-capacity failure leaves
// the current document and its undo/redo history unchanged.
[[nodiscard]] Core::Status
loadWorld2DAuthoringDocument(std::string_view utf8Path,
                             World2DAuthoringDocument& document);

// Synchronously replaces utf8Path with the document's current canonical
// snapshot. The write uses a same-directory temporary file and creates missing
// parent directories. Failure leaves the previously published target intact.
[[nodiscard]] Core::Status
saveWorld2DAuthoringDocument(std::string_view utf8Path,
                             const World2DAuthoringDocument& document);

} // namespace Tina::Editor
