#include "UIStyle.hpp"

namespace Tina::UI {

UITheme::UITheme() {
    initDarkTheme();  // 默认使用深色主题
}

UITheme& UITheme::getDefault() {
    static UITheme instance;
    return instance;
}

void UITheme::setStyle(const std::string& name, const UIStyle& style) {
    m_customStyles[name] = style;
}

const UIStyle* UITheme::getStyle(const std::string& name) const {
    auto it = m_customStyles.find(name);
    if (it != m_customStyles.end()) {
        return &it->second;
    }
    return nullptr;
}

void UITheme::loadDarkTheme() {
    initDarkTheme();
}

void UITheme::loadLightTheme() {
    initLightTheme();
}

void UITheme::loadCustomTheme(const std::string& themeName) {
    // 可以从文件加载主题
    // 这里暂时只支持内置主题
    if (themeName == "dark") {
        loadDarkTheme();
    } else if (themeName == "light") {
        loadLightTheme();
    }
}

void UITheme::initDarkTheme() {
    m_buttonStyle = Themes::createDarkButtonStyle();
    m_labelStyle = Themes::createDarkLabelStyle();
    m_textEditStyle = Themes::createDarkTextEditStyle();
    m_panelStyle = Themes::createDarkPanelStyle();
    m_dialogStyle = Themes::createDarkDialogStyle();
}

void UITheme::initLightTheme() {
    m_buttonStyle = Themes::createLightButtonStyle();
    m_labelStyle = Themes::createLightLabelStyle();
    m_textEditStyle = Themes::createLightTextEditStyle();
    m_panelStyle = Themes::createLightPanelStyle();
    m_dialogStyle = Themes::createLightDialogStyle();
}

} // namespace Tina::UI
