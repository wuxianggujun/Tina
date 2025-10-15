//
// UI 样式系统
// - 统一管理UI组件的视觉样式
// - 支持主题切换
// - 减少重复代码
//

#pragma once

#include "../core/Color.hpp"
#include <string>
#include <unordered_map>

namespace Tina::UI {

// ============================================================================
// UIStyle - UI样式定义
// ============================================================================

struct UIStyle {
    // 颜色
    Core::Color backgroundColor{0.2f, 0.2f, 0.2f, 1.0f};
    Core::Color foregroundColor{1.0f, 1.0f, 1.0f, 1.0f};
    Core::Color borderColor{0.3f, 0.3f, 0.3f, 1.0f};
    Core::Color hoverColor{0.3f, 0.3f, 0.4f, 1.0f};
    Core::Color activeColor{0.4f, 0.4f, 0.5f, 1.0f};
    Core::Color disabledColor{0.15f, 0.15f, 0.15f, 1.0f};
    
    // 文字
    int fontSize = 16;
    Core::Color textColor{1.0f, 1.0f, 1.0f, 1.0f};
    Core::Color textDisabledColor{0.5f, 0.5f, 0.5f, 1.0f};
    
    // 尺寸
    float borderWidth = 1.0f;
    float cornerRadius = 0.0f;
    float padding = 10.0f;
    
    // 间距
    float spacing = 10.0f;
};

// ============================================================================
// UITheme - 主题管理器
// ============================================================================

class UITheme {
public:
    // 获取默认主题
    static UITheme& getDefault();
    
    // 预定义样式
    const UIStyle& getButtonStyle() const { return m_buttonStyle; }
    const UIStyle& getLabelStyle() const { return m_labelStyle; }
    const UIStyle& getTextEditStyle() const { return m_textEditStyle; }
    const UIStyle& getPanelStyle() const { return m_panelStyle; }
    const UIStyle& getDialogStyle() const { return m_dialogStyle; }
    
    // 自定义样式
    void setStyle(const std::string& name, const UIStyle& style);
    const UIStyle* getStyle(const std::string& name) const;
    
    // 主题切换
    void loadDarkTheme();
    void loadLightTheme();
    void loadCustomTheme(const std::string& themeName);

private:
    UITheme();
    
    void initDarkTheme();
    void initLightTheme();
    
    UIStyle m_buttonStyle;
    UIStyle m_labelStyle;
    UIStyle m_textEditStyle;
    UIStyle m_panelStyle;
    UIStyle m_dialogStyle;
    
    std::unordered_map<std::string, UIStyle> m_customStyles;
};

// ============================================================================
// 预定义主题
// ============================================================================

namespace Themes {

// 深色主题（默认）
inline UIStyle createDarkButtonStyle() {
    UIStyle style;
    style.backgroundColor = Core::Color(0.25f, 0.25f, 0.3f, 1.0f);
    style.foregroundColor = Core::Color(1.0f, 1.0f, 1.0f, 1.0f);
    style.hoverColor = Core::Color(0.3f, 0.3f, 0.4f, 1.0f);
    style.activeColor = Core::Color(0.35f, 0.35f, 0.45f, 1.0f);
    style.textColor = Core::Color(1.0f, 1.0f, 1.0f, 1.0f);
    style.fontSize = 16;
    style.cornerRadius = 4.0f;
    style.padding = 10.0f;
    return style;
}

inline UIStyle createDarkLabelStyle() {
    UIStyle style;
    style.backgroundColor = Core::Color(0.0f, 0.0f, 0.0f, 0.0f);  // 透明
    style.textColor = Core::Color(1.0f, 1.0f, 1.0f, 1.0f);
    style.fontSize = 16;
    return style;
}

inline UIStyle createDarkTextEditStyle() {
    UIStyle style;
    style.backgroundColor = Core::Color(0.15f, 0.15f, 0.2f, 1.0f);
    style.borderColor = Core::Color(0.3f, 0.3f, 0.4f, 1.0f);
    style.textColor = Core::Color(1.0f, 1.0f, 1.0f, 1.0f);
    style.fontSize = 16;
    style.borderWidth = 1.0f;
    style.cornerRadius = 4.0f;
    style.padding = 8.0f;
    return style;
}

inline UIStyle createDarkPanelStyle() {
    UIStyle style;
    style.backgroundColor = Core::Color(0.2f, 0.2f, 0.25f, 1.0f);
    style.borderColor = Core::Color(0.3f, 0.3f, 0.35f, 1.0f);
    style.borderWidth = 1.0f;
    style.cornerRadius = 8.0f;
    return style;
}

inline UIStyle createDarkDialogStyle() {
    UIStyle style;
    style.backgroundColor = Core::Color(0.15f, 0.15f, 0.2f, 1.0f);
    style.borderColor = Core::Color(0.4f, 0.4f, 0.5f, 1.0f);
    style.borderWidth = 2.0f;
    style.cornerRadius = 8.0f;
    style.padding = 20.0f;
    return style;
}

// 浅色主题
inline UIStyle createLightButtonStyle() {
    UIStyle style;
    style.backgroundColor = Core::Color(0.9f, 0.9f, 0.95f, 1.0f);
    style.foregroundColor = Core::Color(0.1f, 0.1f, 0.1f, 1.0f);
    style.hoverColor = Core::Color(0.85f, 0.85f, 0.9f, 1.0f);
    style.activeColor = Core::Color(0.8f, 0.8f, 0.85f, 1.0f);
    style.textColor = Core::Color(0.1f, 0.1f, 0.1f, 1.0f);
    style.fontSize = 16;
    style.cornerRadius = 4.0f;
    style.padding = 10.0f;
    return style;
}

inline UIStyle createLightLabelStyle() {
    UIStyle style;
    style.backgroundColor = Core::Color(0.0f, 0.0f, 0.0f, 0.0f);  // 透明
    style.textColor = Core::Color(0.1f, 0.1f, 0.1f, 1.0f);
    style.fontSize = 16;
    return style;
}

inline UIStyle createLightTextEditStyle() {
    UIStyle style;
    style.backgroundColor = Core::Color(1.0f, 1.0f, 1.0f, 1.0f);
    style.borderColor = Core::Color(0.7f, 0.7f, 0.75f, 1.0f);
    style.textColor = Core::Color(0.1f, 0.1f, 0.1f, 1.0f);
    style.fontSize = 16;
    style.borderWidth = 1.0f;
    style.cornerRadius = 4.0f;
    style.padding = 8.0f;
    return style;
}

inline UIStyle createLightPanelStyle() {
    UIStyle style;
    style.backgroundColor = Core::Color(0.95f, 0.95f, 0.97f, 1.0f);
    style.borderColor = Core::Color(0.8f, 0.8f, 0.85f, 1.0f);
    style.borderWidth = 1.0f;
    style.cornerRadius = 8.0f;
    return style;
}

inline UIStyle createLightDialogStyle() {
    UIStyle style;
    style.backgroundColor = Core::Color(0.98f, 0.98f, 1.0f, 1.0f);
    style.borderColor = Core::Color(0.7f, 0.7f, 0.8f, 1.0f);
    style.borderWidth = 2.0f;
    style.cornerRadius = 8.0f;
    style.padding = 20.0f;
    return style;
}

} // namespace Themes

} // namespace Tina::UI
