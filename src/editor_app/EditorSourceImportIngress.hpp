#pragma once

#include "EditorSourceImportLimits.hpp"

#include <tina/core/error/Result.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::EditorApp::Detail {

class EditorSourceImportIngress final {
public:
    ~EditorSourceImportIngress() noexcept;

    EditorSourceImportIngress(const EditorSourceImportIngress&) = delete;
    EditorSourceImportIngress& operator=(const EditorSourceImportIngress&) = delete;
    EditorSourceImportIngress(EditorSourceImportIngress&& other) noexcept;
    EditorSourceImportIngress& operator=(EditorSourceImportIngress&&) = delete;

    [[nodiscard]] std::span<const std::string> projectPathsUtf8() const noexcept;
    [[nodiscard]] Core::u32 copiedFileCount() const noexcept;
    [[nodiscard]] Core::u32 reusedFileCount() const noexcept;

    void commit() noexcept;

private:
    EditorSourceImportIngress() = default;

    void rollback() noexcept;

    std::vector<std::string> projectPathsUtf8_{};
    std::vector<std::string> createdFilePathsUtf8_{};
    std::vector<std::string> createdDirectoryPathsUtf8_{};
    Core::u32 copiedFileCount_ = 0;
    Core::u32 reusedFileCount_ = 0;
    bool committed_ = false;

    friend Core::Result<EditorSourceImportIngress>
    prepareEditorSourceImportIngress(
        std::string_view sourceRootUtf8,
        std::span<const std::string> selectedPathsUtf8,
        Core::u32 maxSelectedPaths);
};

// Resolves files already below Source directly. External PNG/JPEG/WAV files are copied into
// Source/Imported using a rollback-on-failure transaction before their project paths are returned.
[[nodiscard]] Core::Result<EditorSourceImportIngress>
prepareEditorSourceImportIngress(
    std::string_view sourceRootUtf8,
    std::span<const std::string> selectedPathsUtf8,
    Core::u32 maxSelectedPaths = EditorSourceImportUnitCapacity);

} // namespace Tina::EditorApp::Detail
