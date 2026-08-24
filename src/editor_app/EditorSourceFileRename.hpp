#pragma once

#include <tina/core/error/Result.hpp>

#include <string>
#include <string_view>

namespace Tina::EditorApp::Detail {

// Move-only filesystem transaction used while an asynchronous source import
// validates a renamed media file. Destruction rolls the file back unless the
// owner explicitly commits the transaction.
class EditorSourceFileRename final {
public:
    [[nodiscard]] static Core::Result<EditorSourceFileRename>
    Begin(std::string_view previousPathUtf8,
          std::string_view renamedPathUtf8) noexcept;

    ~EditorSourceFileRename() noexcept;

    EditorSourceFileRename(const EditorSourceFileRename&) = delete;
    EditorSourceFileRename& operator=(const EditorSourceFileRename&) = delete;
    EditorSourceFileRename(EditorSourceFileRename&& other) noexcept;
    EditorSourceFileRename& operator=(EditorSourceFileRename&& other) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::string_view previousPathUtf8() const noexcept
    {
        return previousPathUtf8_;
    }
    [[nodiscard]] std::string_view renamedPathUtf8() const noexcept
    {
        return renamedPathUtf8_;
    }

    [[nodiscard]] Core::Status rollback() noexcept;
    void commit() noexcept { active_ = false; }

private:
    EditorSourceFileRename(std::string previousPathUtf8,
                           std::string renamedPathUtf8) noexcept;
    void rollbackNoexcept() noexcept;

    std::string previousPathUtf8_{};
    std::string renamedPathUtf8_{};
    bool active_ = false;
};

} // namespace Tina::EditorApp::Detail
