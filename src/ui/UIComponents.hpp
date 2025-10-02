//
// UI 具体组件：Panel（面板）、Label（文本标签）、Button（按钮）
//

#pragma once

#include "UINode.hpp"

namespace Tina::UI {

// === Panel：纯色矩形面板 ===
class UIPanel : public UINode {
public:
    UIPanel(const std::string& name = "Panel")
        : UINode(name), m_color{0.2f, 0.2f, 0.2f, 0.8f}
    {}

    void setColor(float r, float g, float b, float a) {
        m_color = {r, g, b, a};
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
    {}

    void setText(const std::string& text) { m_text = text; }
    void setColor(float r, float g, float b, float a) { m_color = {r, g, b, a}; }

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
        , m_normalColor{0.3f, 0.3f, 0.35f, 0.9f}
        , m_hoverColor{0.4f, 0.4f, 0.5f, 0.9f}
        , m_pressedColor{0.2f, 0.2f, 0.25f, 0.9f}
        , m_textColor{1,1,1,1}
        , m_hovered(false)
        , m_pressed(false)
        , m_selected(false)
    {}

    void setText(const std::string& text) { m_text = text; }
    void setNormalColor(float r, float g, float b, float a) { m_normalColor = {r,g,b,a}; }
    void setHoverColor(float r, float g, float b, float a) { m_hoverColor = {r,g,b,a}; }
    void setPressedColor(float r, float g, float b, float a) { m_pressedColor = {r,g,b,a}; }
    void setTextColor(float r, float g, float b, float a) { m_textColor = {r,g,b,a}; }

    const std::string& getText() const { return m_text; }

    void setHovered(bool h) { m_hovered = h; }
    void setPressed(bool p) { m_pressed = p; }
    void setSelected(bool s) { m_selected = s; }

    bool isHovered() const { return m_hovered; }
    bool isPressed() const { return m_pressed; }
    bool isSelected() const { return m_selected; }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    std::string m_text;
    Tina::Math::Vec4 m_normalColor;
    Tina::Math::Vec4 m_hoverColor;
    Tina::Math::Vec4 m_pressedColor;
    Tina::Math::Vec4 m_textColor;

    bool m_hovered;
    bool m_pressed;
    bool m_selected;
};

} // namespace Tina::UI
