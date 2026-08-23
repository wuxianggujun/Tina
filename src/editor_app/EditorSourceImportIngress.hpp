#pragma once

#include "EditorSourceImportLimits.hpp"

#include <tina/core/error/Result.hpp>

#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::EditorApp::Detail {

struct EditorSourceImportIngressProgress final {
    void* context = nullptr;
    void (*copyingStarted)(void* context) noexcept = nullptr;

    void notifyCopyingStarted() const noexcept
    {
        if (copyingStarted != nullptr) {
            copyingStarted(context);
        }
    }
};

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
        Core::u32 maxSelectedPaths,
        std::stop_token stopToken,
        EditorSourceImportIngressProgress progress);
};

// Worker-only ingress transaction. Resolves files already below Source directly and copies
// external PNG/JPEG/WAV files into Source/Imported. Cancellation and progress are explicit so the
// UI cannot accidentally invoke the former synchronous convenience path.
[[nodiscard]] Core::Result<EditorSourceImportIngress>
prepareEditorSourceImportIngress(
    std::string_view sourceRootUtf8,
    std::span<const std::string> selectedPathsUtf8,
    Core::u32 maxSelectedPaths,
    std::stop_token stopToken,
    EditorSourceImportIngressProgress progress);

} // namespace Tina::EditorApp::Detail
