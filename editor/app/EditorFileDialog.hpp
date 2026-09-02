#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Tina::EditorApp::Detail {

struct EditorFileDialogFilter final {
    std::string_view labelUtf8{};
    std::string_view patternUtf8{};
};

enum class EditorFileDialogOutcome : u8 {
    Selected,
    Cancelled,
};

struct EditorFileDialogResult final {
    EditorFileDialogOutcome outcome = EditorFileDialogOutcome::Cancelled;
    std::string selectedPathUtf8{};

    [[nodiscard]] bool selected() const noexcept
    {
        return outcome == EditorFileDialogOutcome::Selected;
    }
};

inline constexpr u32 EditorFileDialogSelectionCapacity = 4096;

struct EditorFileDialogResults final {
    EditorFileDialogOutcome outcome = EditorFileDialogOutcome::Cancelled;
    std::vector<std::string> selectedPathsUtf8{};

    [[nodiscard]] bool selected() const noexcept
    {
        return outcome == EditorFileDialogOutcome::Selected;
    }
};

struct OpenExistingFileDialogRequest final {
    std::string_view titleUtf8{};
    std::string_view initialDirectoryUtf8{};
    std::span<const EditorFileDialogFilter> filters{};
};

struct OpenExistingFilesDialogRequest final {
    std::string_view titleUtf8{};
    std::string_view initialDirectoryUtf8{};
    std::span<const EditorFileDialogFilter> filters{};
    u32 maxSelectedPaths = EditorFileDialogSelectionCapacity;
};

struct SaveFileDialogRequest final {
    std::string_view titleUtf8{};
    std::string_view initialDirectoryUtf8{};
    std::string_view suggestedFileNameUtf8{};
    std::string_view defaultExtensionUtf8{};
    std::span<const EditorFileDialogFilter> filters{};
};

struct PickFolderDialogRequest final {
    std::string_view titleUtf8{};
    std::string_view initialDirectoryUtf8{};
};

class EditorFileDialog final {
public:
    explicit EditorFileDialog(uintptr nativeOwnerWindow = 0) noexcept;

    EditorFileDialog(const EditorFileDialog&) = delete;
    EditorFileDialog& operator=(const EditorFileDialog&) = delete;
    EditorFileDialog(EditorFileDialog&&) = delete;
    EditorFileDialog& operator=(EditorFileDialog&&) = delete;

    [[nodiscard]] Core::Result<EditorFileDialogResult>
    openExistingFile(const OpenExistingFileDialogRequest& request) const;

    [[nodiscard]] Core::Result<EditorFileDialogResults>
    openExistingFiles(const OpenExistingFilesDialogRequest& request) const;

    [[nodiscard]] Core::Result<EditorFileDialogResult>
    saveFile(const SaveFileDialogRequest& request) const;

    [[nodiscard]] Core::Result<EditorFileDialogResult>
    pickFolder(const PickFolderDialogRequest& request) const;

private:
    [[nodiscard]] Core::Status ensureOwnerThread() const;

    std::thread::id ownerThread_{};
    uintptr nativeOwnerWindow_ = 0;
};

} // namespace Tina::EditorApp::Detail
