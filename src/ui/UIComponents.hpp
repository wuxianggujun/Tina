//
// UI 具体组件：Panel（面板）、Label（文本标签）、Button（按钮）
//

#pragma once

#include "UINode.hpp"
#include "../core/Color.hpp"
#include "UIColors.hpp"

namespace Tina::UI {

enum class BadgeCorner { TopLeft, TopRight, BottomLeft, BottomRight };

// === Panel：纯色矩形面板 ===
class UIPanel : public UINode {
public:
    UIPanel(const std::string& name = "Panel")
        : UINode(name)
    {
        auto a = Tina::UI::UIColors::PanelBg.toArray();
        m_color = { a[0], a[1], a[2], a[3] };
    }

    void setColor(float r, float g, float b, float a) {
        m_color = {r, g, b, a};
    }
    void setColor(const Tina::Core::Color& c) {
        auto a = c.toArray();
        m_color = { a[0], a[1], a[2], a[3] };
    }

    Tina::Math::Vec4 getColor() const { return m_color; }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    Tina::Math::Vec4 m_color; // RGBA
};

// === Label：文本标签 ===
class UILabel : public UINode {
public:
    UILabel(const std::string& name = "Label")
        : UINode(name), m_text("Label"), m_color{1,1,1,1}
    {
        auto a = Tina::UI::UIColors::LabelText.toArray();
        m_color = { a[0], a[1], a[2], a[3] };
    }

    void setText(const std::string& text) { m_text = text; }
    void setColor(float r, float g, float b, float a) { m_color = {r, g, b, a}; }
    void setColor(const Tina::Core::Color& c) { auto a=c.toArray(); m_color={a[0],a[1],a[2],a[3]}; }

    const std::string& getText() const { return m_text; }
    Tina::Math::Vec4 getColor() const { return m_color; }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    std::string m_text;
    Tina::Math::Vec4 m_color;
};

// === Button：可点击按钮（背景 + 文本） ===
class UIButton : public UINode {
public:
    UIButton(const std::string& name = "Button")
        : UINode(name)
        , m_text("Button")
        , m_normalColor{0,0,0,1}
        , m_hoverColor{0,0,0,1}
        , m_pressedColor{0,0,0,1}
        , m_textColor{1,1,1,1}
        , m_badgeBgColor{0,0,0,1}
        , m_badgeTextColor{1,1,1,1}
        , m_badgeCorner(BadgeCorner::TopRight)
        , m_hovered(false)
        , m_pressed(false)
        , m_selected(false)
    {
        auto n = Tina::UI::UIColors::ButtonNormal.toArray(); m_normalColor = {n[0],n[1],n[2],n[3]};
        auto h = Tina::UI::UIColors::ButtonHover.toArray();  m_hoverColor   = {h[0],h[1],h[2],h[3]};
        auto p = Tina::UI::UIColors::ButtonPressed.toArray();m_pressedColor = {p[0],p[1],p[2],p[3]};
        auto t = Tina::UI::UIColors::ButtonText.toArray();   m_textColor    = {t[0],t[1],t[2],t[3]};
        auto bb= Tina::UI::UIColors::BadgeBg.toArray();      m_badgeBgColor = {bb[0],bb[1],bb[2],bb[3]};
        auto bt= Tina::UI::UIColors::BadgeText.toArray();    m_badgeTextColor={bt[0],bt[1],bt[2],bt[3]};
    }

    void setText(const std::string& text) { m_text = text; }
    void setNormalColor(float r, float g, float b, float a) { m_normalColor = {r,g,b,a}; }
    void setHoverColor(float r, float g, float b, float a) { m_hoverColor = {r,g,b,a}; }
    void setPressedColor(float r, float g, float b, float a) { m_pressedColor = {r,g,b,a}; }
    void setTextColor(float r, float g, float b, float a) { m_textColor = {r,g,b,a}; }
    void setNormalColor(const Tina::Core::Color& c) { auto a=c.toArray(); m_normalColor={a[0],a[1],a[2],a[3]}; }
    void setHoverColor(const Tina::Core::Color& c)  { auto a=c.toArray(); m_hoverColor  ={a[0],a[1],a[2],a[3]}; }
    void setPressedColor(const Tina::Core::Color& c){ auto a=c.toArray(); m_pressedColor={a[0],a[1],a[2],a[3]}; }
    void setTextColor(const Tina::Core::Color& c)   { auto a=c.toArray(); m_textColor   ={a[0],a[1],a[2],a[3]}; }

    const std::string& getText() const { return m_text; }
    // 角标（数字小角标）API
    void setBadgeText(const std::string& text) { m_badgeText = text; }
    const std::string& badgeText() const { return m_badgeText; }
    void setBadgeColors(float br, float bg, float bb, float ba,
                        float tr, float tg, float tb, float ta) {
        m_badgeBgColor = {br,bg,bb,ba};
        m_badgeTextColor = {tr,tg,tb,ta};
    }
    void setBadgeColors(const Tina::Core::Color& bg, const Tina::Core::Color& text) {
        auto b=bg.toArray(); auto t=text.toArray();
        m_badgeBgColor = {b[0],b[1],b[2],b[3]};
        m_badgeTextColor = {t[0],t[1],t[2],t[3]};
    }
    void setBadgeCorner(BadgeCorner c) { m_badgeCorner = c; }

    void setHovered(bool h) { m_hovered = h; }
    void setPressed(bool p) { m_pressed = p; }
    void setSelected(bool s) { m_selected = s; }

    bool isHovered() const { return m_hovered; }
    bool isPressed() const { return m_pressed; }
    bool isSelected() const { return m_selected; }
    
    // 可选：悬停回调，便于外部展示 tooltip
    std::function<void()> onHoverIn;
    std::function<void()> onHoverOut;

    void onMouseEnter() override { if (onHoverIn) onHoverIn(); }
    void onMouseLeave() override { if (onHoverOut) onHoverOut(); }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    std::string m_text;
    Tina::Math::Vec4 m_normalColor;
    Tina::Math::Vec4 m_hoverColor;
    Tina::Math::Vec4 m_pressedColor;
    Tina::Math::Vec4 m_textColor;
    std::string m_badgeText;               // 角标文本（如“1”）
    Tina::Math::Vec4 m_badgeBgColor;       // 角标背景色
    Tina::Math::Vec4 m_badgeTextColor;     // 角标文字颜色

    BadgeCorner m_badgeCorner;
    bool m_hovered;
    bool m_pressed;
    bool m_selected;
};

} // namespace Tina::UI
