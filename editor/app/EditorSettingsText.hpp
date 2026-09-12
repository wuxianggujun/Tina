#pragma once

#include <string_view>

namespace Tina::EditorApp::WorkspaceInternal {

// Borrowed line-oriented settings lookup. The caller owns the text; matching a
// full key prevents a suffix or a recent-project path from impersonating a key.
[[nodiscard]] inline std::string_view findEditorSettingValue(
    std::string_view text, std::string_view key) noexcept
{
    if (key.empty()) return {};
    while (!text.empty())
    {
        const auto newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() > key.size() && line.starts_with(key) && line[key.size()] == '=')
        {
            return line.substr(key.size() + 1);
        }
        if (newline == std::string_view::npos) break;
        text.remove_prefix(newline + 1);
    }
    return {};
}

} // namespace Tina::EditorApp::WorkspaceInternal
