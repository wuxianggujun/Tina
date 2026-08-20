#pragma once

#include "EditorIconAtlas.hpp"

#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/ui/UIIconButton.hpp>
#include <tina/ui/UIText.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace Tina::EditorApp::WorkspaceInternal {

inline constexpr Core::usize EditorNumberTextCapacity = 32;

struct EditorNumberText final {
    std::array<char, EditorNumberTextCapacity> bytes{};
    Core::usize size = 0;

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view(bytes.data(), size);
    }
};

[[nodiscard]] Core::Result<EditorNumberText>
formatEditorNumber(float value) noexcept;

struct EditorIconToggleButtonParts final {
    UI::UINodeId root{};
    UI::UINodeId button{};
    UI::UINodeId tooltip{};
};

struct EditorPanelHeaderParts final {
    UI::UINodeId root{};
    UI::UINodeId title{};
    UI::UINodeId actions{};
};

struct EditorSectionHeaderParts final {
    UI::UINodeId root{};
    UI::UINodeId title{};
    UI::UINodeId divider{};
};

struct EditorPropertyRowParts final {
    UI::UINodeId root{};
    UI::UINodeId label{};
    UI::UINodeId value{};
};

struct EditorSearchFieldParts final {
    UI::UINodeId root{};
    UI::UINodeId icon{};
    UI::UINodeId textEdit{};
};

[[nodiscard]] UI::UILayoutStyle editorDocumentTabLayout(
    const UI::UITheme& theme, UI::UIVisibility visibility) noexcept;

[[nodiscard]] UI::UITextStyle makeEditorInfoTextStyle(
    const UI::UITheme& theme, float logicalSize = 0.0F) noexcept;

class EditorToolbarGroup final {
  public:
    [[nodiscard]] static Core::Result<UI::UINodeId>
    Build(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
          const UI::UITheme& theme, UI::UILayoutStyle layout = {});
};

class EditorIconButton final {
  public:
    [[nodiscard]] static Core::Result<UI::UIIconButtonParts>
    Build(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
          const UI::UITheme& theme, EditorIcon icon,
          std::string_view accessibleName, UI::UILayoutStyle layout = {},
          bool enabled = true,
          UI::UIButtonVariant variant = UI::UIButtonVariant::Text);
};

class EditorIconToggleButton final {
  public:
    [[nodiscard]] static Core::Result<EditorIconToggleButtonParts>
    Build(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
          const UI::UITheme& theme, EditorIcon icon,
          std::string_view accessibleName, UI::UILayoutStyle layout = {},
          bool enabled = true);
};

class EditorPanelHeader final {
  public:
    [[nodiscard]] static Core::Result<EditorPanelHeaderParts>
    Build(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
          const UI::UITheme& theme, std::string_view title,
          const UI::UITextStyle& textStyle, UI::UILayoutStyle layout = {});
};

class EditorSectionHeader final {
  public:
    [[nodiscard]] static Core::Result<EditorSectionHeaderParts>
    Build(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
          const UI::UITheme& theme, std::string_view title,
          const UI::UITextStyle& textStyle, UI::UILayoutStyle layout = {});
};

class EditorPropertyRow final {
  public:
    [[nodiscard]] static Core::Result<EditorPropertyRowParts>
    Build(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
          const UI::UITheme& theme, std::string_view label,
          const UI::UITextStyle& labelStyle, UI::UILayoutStyle layout = {});
};

class EditorSearchField final {
  public:
    [[nodiscard]] static Core::Result<EditorSearchFieldParts>
    Build(PrimaryWindowUITreeUpdater& tree, UI::UINodeId parent,
          const UI::UITheme& theme, std::string_view value,
          std::string_view accessibleName, UI::UILayoutStyle layout = {},
          bool enabled = true);
};

} // namespace Tina::EditorApp::WorkspaceInternal
