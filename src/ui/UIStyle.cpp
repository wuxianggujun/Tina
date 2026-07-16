#include "UIStyle.hpp"

namespace Tina::UI {
namespace {

UIStyle darkButton()
{
    UIStyle style;
    style.backgroundColor = Core::Color(0.25f, 0.25f, 0.30f, 1.0f);
    style.hoverColor = Core::Color(0.32f, 0.34f, 0.46f, 1.0f);
    style.activeColor = Core::Color(0.18f, 0.20f, 0.30f, 1.0f);
    style.disabledColor = Core::Color(0.18f, 0.18f, 0.20f, 0.55f);
    style.borderColor = Core::Color(0.42f, 0.44f, 0.56f, 1.0f);
    style.cornerRadius = 4.0f;
    return style;
}

UIStyle lightButton()
{
    UIStyle style = darkButton();
    style.backgroundColor = Core::Color(0.90f, 0.91f, 0.95f, 1.0f);
    style.hoverColor = Core::Color(0.82f, 0.85f, 0.93f, 1.0f);
    style.activeColor = Core::Color(0.72f, 0.78f, 0.90f, 1.0f);
    style.disabledColor = Core::Color(0.82f, 0.82f, 0.84f, 0.65f);
    style.borderColor = Core::Color(0.60f, 0.62f, 0.70f, 1.0f);
    style.textColor = Core::Color(0.10f, 0.11f, 0.14f, 1.0f);
    style.textDisabledColor = Core::Color(0.42f, 0.42f, 0.46f, 1.0f);
    return style;
}

UIStyle darkLabel()
{
    UIStyle style;
    style.backgroundColor = Core::Color(0.0f, 0.0f, 0.0f, 0.0f);
    style.textColor = Core::Color(0.96f, 0.97f, 1.0f, 1.0f);
    return style;
}

UIStyle lightLabel()
{
    UIStyle style = darkLabel();
    style.textColor = Core::Color(0.10f, 0.11f, 0.14f, 1.0f);
    return style;
}

UIStyle darkPanel()
{
    UIStyle style;
    style.backgroundColor = Core::Color(0.15f, 0.16f, 0.21f, 0.94f);
    style.borderColor = Core::Color(0.32f, 0.34f, 0.43f, 1.0f);
    style.cornerRadius = 6.0f;
    return style;
}

UIStyle lightPanel()
{
    UIStyle style = darkPanel();
    style.backgroundColor = Core::Color(0.96f, 0.97f, 0.99f, 0.96f);
    style.borderColor = Core::Color(0.76f, 0.78f, 0.84f, 1.0f);
    return style;
}

UIStyle darkTextEdit()
{
    UIStyle style = darkPanel();
    style.backgroundColor = Core::Color(0.10f, 0.11f, 0.15f, 1.0f);
    style.borderColor = Core::Color(0.36f, 0.39f, 0.52f, 1.0f);
    style.selectionColor = Core::Color(0.28f, 0.48f, 0.86f, 0.55f);
    style.padding = 6.0f;
    return style;
}

UIStyle lightTextEdit()
{
    UIStyle style = lightPanel();
    style.backgroundColor = Core::Color(1.0f, 1.0f, 1.0f, 1.0f);
    style.borderColor = Core::Color(0.62f, 0.66f, 0.76f, 1.0f);
    style.textColor = Core::Color(0.08f, 0.09f, 0.12f, 1.0f);
    style.caretColor = style.textColor;
    style.selectionColor = Core::Color(0.22f, 0.46f, 0.86f, 0.35f);
    style.padding = 6.0f;
    return style;
}

} // namespace

UITheme::UITheme()
{
    applyDark();
}

UITheme::UITheme(UIThemeKind kind)
{
    if (kind == UIThemeKind::Light) {
        applyLight();
    } else {
        applyDark();
    }
    if (kind == UIThemeKind::Custom) m_kind = UIThemeKind::Custom;
}

UITheme UITheme::dark()
{
    return UITheme(UIThemeKind::Dark);
}

UITheme UITheme::light()
{
    return UITheme(UIThemeKind::Light);
}

const UIStyle& UITheme::style(UIStyleRole role) const
{
    switch (role) {
        case UIStyleRole::Panel: return m_panelStyle;
        case UIStyleRole::Label: return m_labelStyle;
        case UIStyleRole::Button: return m_buttonStyle;
        case UIStyleRole::TextEdit: return m_textEditStyle;
        case UIStyleRole::Dialog: return m_dialogStyle;
    }
    return m_panelStyle;
}

void UITheme::setStyle(UIStyleRole role, const UIStyle& styleValue)
{
    m_kind = UIThemeKind::Custom;
    switch (role) {
        case UIStyleRole::Panel: m_panelStyle = styleValue; break;
        case UIStyleRole::Label: m_labelStyle = styleValue; break;
        case UIStyleRole::Button: m_buttonStyle = styleValue; break;
        case UIStyleRole::TextEdit: m_textEditStyle = styleValue; break;
        case UIStyleRole::Dialog: m_dialogStyle = styleValue; break;
    }
}

void UITheme::setStyle(const std::string& name, const UIStyle& styleValue)
{
    m_kind = UIThemeKind::Custom;
    m_customStyles[name] = styleValue;
}

const UIStyle* UITheme::getStyle(const std::string& name) const
{
    const auto it = m_customStyles.find(name);
    return it == m_customStyles.end() ? nullptr : &it->second;
}

void UITheme::applyDark()
{
    m_kind = UIThemeKind::Dark;
    m_buttonStyle = darkButton();
    m_labelStyle = darkLabel();
    m_textEditStyle = darkTextEdit();
    m_panelStyle = darkPanel();
    m_dialogStyle = darkPanel();
    m_dialogStyle.padding = 20.0f;
    m_dialogStyle.borderWidth = 2.0f;
    m_customStyles.clear();
}

void UITheme::applyLight()
{
    m_kind = UIThemeKind::Light;
    m_buttonStyle = lightButton();
    m_labelStyle = lightLabel();
    m_textEditStyle = lightTextEdit();
    m_panelStyle = lightPanel();
    m_dialogStyle = lightPanel();
    m_dialogStyle.padding = 20.0f;
    m_dialogStyle.borderWidth = 2.0f;
    m_customStyles.clear();
}

} // namespace Tina::UI
