#pragma once

#include "../core/Color.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Tina::UI {

struct UIStyle {
    Core::Color backgroundColor{0.2f, 0.2f, 0.2f, 1.0f};
    Core::Color foregroundColor{1.0f, 1.0f, 1.0f, 1.0f};
    Core::Color borderColor{0.3f, 0.3f, 0.3f, 1.0f};
    Core::Color hoverColor{0.3f, 0.3f, 0.4f, 1.0f};
    Core::Color activeColor{0.4f, 0.4f, 0.5f, 1.0f};
    Core::Color disabledColor{0.15f, 0.15f, 0.15f, 1.0f};
    Core::Color textColor{1.0f, 1.0f, 1.0f, 1.0f};
    Core::Color textDisabledColor{0.5f, 0.5f, 0.5f, 1.0f};
    Core::Color selectionColor{0.3f, 0.5f, 0.8f, 0.4f};
    Core::Color caretColor{1.0f, 1.0f, 1.0f, 1.0f};
    int fontSize = 16;
    float borderWidth = 1.0f;
    float cornerRadius = 0.0f;
    float padding = 10.0f;
    float spacing = 10.0f;
};

enum class UIStyleRole : uint8_t {
    Panel,
    Label,
    Button,
    TextEdit,
    Dialog
};

enum class UIThemeKind : uint8_t {
    Dark,
    Light,
    Custom
};

// A theme is an ordinary value owned by one window UIContext. It deliberately
// has no global default instance, so separate windows and tests cannot mutate
// each other's visual state.
class UITheme {
public:
    UITheme();
    explicit UITheme(UIThemeKind kind);

    static UITheme dark();
    static UITheme light();

    UIThemeKind kind() const noexcept { return m_kind; }

    const UIStyle& style(UIStyleRole role) const;
    void setStyle(UIStyleRole role, const UIStyle& style);

    const UIStyle& getButtonStyle() const { return m_buttonStyle; }
    const UIStyle& getLabelStyle() const { return m_labelStyle; }
    const UIStyle& getTextEditStyle() const { return m_textEditStyle; }
    const UIStyle& getPanelStyle() const { return m_panelStyle; }
    const UIStyle& getDialogStyle() const { return m_dialogStyle; }

    void setStyle(const std::string& name, const UIStyle& style);
    const UIStyle* getStyle(const std::string& name) const;

private:
    void applyDark();
    void applyLight();

    UIThemeKind m_kind = UIThemeKind::Dark;
    UIStyle m_buttonStyle;
    UIStyle m_labelStyle;
    UIStyle m_textEditStyle;
    UIStyle m_panelStyle;
    UIStyle m_dialogStyle;
    std::unordered_map<std::string, UIStyle> m_customStyles;
};

} // namespace Tina::UI
