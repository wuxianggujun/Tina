#pragma once

#include "EditorFileDialog.hpp"

namespace Tina::EditorApp::Detail {

[[nodiscard]] Core::Result<EditorFileDialogResult>
openExistingFileLinux(const OpenExistingFileDialogRequest& request);

[[nodiscard]] Core::Result<EditorFileDialogResult>
saveFileLinux(const SaveFileDialogRequest& request);

[[nodiscard]] Core::Result<EditorFileDialogResult>
pickFolderLinux(const PickFolderDialogRequest& request);

} // namespace Tina::EditorApp::Detail
