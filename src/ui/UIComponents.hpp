//
// UI 具体组件：Panel（面板）、Label（文本标签）、Button（按钮）
//

#pragma once

#include "UINode.hpp"
#include "../core/Color.hpp"
#include "../core/Signal.hpp"
#include "UIColors.hpp"
#include <bgfx/bgfx.h>

namespace Tina::UI {

enum class BadgeCorner { TopLeft, TopRight, BottomLeft, BottomRight };

// === Panel：纯色矩形面板 ===
class UIPanel : public UINode {
public:
    UIPanel(const std::string& name = "Panel")
        : UINode(name)
        , m_color(Tina::UI::UIColors::PanelBg)
    {}

    void setColor(float r, float g, float b, float a) {
        m_color = Tina::Core::Color(r, g, b, a);
    }
    void setColor(const Tina::Core::Color& c) {
        m_color = c;
    }

    Tina::Core::Color getColor() const { return m_color; }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    Tina::Core::Color m_color;
};

// === Label：文本标签 ===
class UILabel : public UINode {
public:
    UILabel(const std::string& name = "Label")
        : UINode(name)
        , m_text("Label")
        , m_color(Tina::UI::UIColors::LabelText)
    {}

    void setText(const std::string& text) { m_text = text; }
    void setColor(float r, float g, float b, float a) { m_color = Tina::Core::Color(r, g, b, a); }
    void setColor(const Tina::Core::Color& c) { m_color = c; }

    // 文本对齐设置（仅影响渲染，不影响节点布局）
    enum class TextAlignH { Left, Center, Right };
    enum class TextAlignV { Top, Center, Bottom, Baseline };
    void setAlignH(TextAlignH h) { m_alignH = h; }
    void setAlignV(TextAlignV v) { m_alignV = v; }
    void setAlignment(TextAlignH h, TextAlignV v) { m_alignH = h; m_alignV = v; }

    // 文本字号（像素）。0 表示使用当前全局字号
    void setFontPx(int px) { m_fontPx = std::max(0, px); }
    int fontPx() const { return m_fontPx; }

    TextAlignH alignH() const { return m_alignH; }
    TextAlignV alignV() const { return m_alignV; }

    const std::string& getText() const { return m_text; }
    Tina::Core::Color getColor() const { return m_color; }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    std::string m_text;
    Tina::Core::Color m_color;
    TextAlignH m_alignH = TextAlignH::Left;
    TextAlignV m_alignV = TextAlignV::Top;
    int m_fontPx = 0;
};

// === Button：可点击按钮（背景 + 文本） ===
class UIButton : public UINode {
public:
    UIButton(const std::string& name = "Button")
        : UINode(name)
        , m_text("Button")
        , m_normalColor(Tina::UI::UIColors::ButtonNormal)
        , m_hoverColor(Tina::UI::UIColors::ButtonHover)
        , m_pressedColor(Tina::UI::UIColors::ButtonPressed)
        , m_textColor(Tina::UI::UIColors::ButtonText)
        , m_badgeBgColor(Tina::UI::UIColors::BadgeBg)
        , m_badgeTextColor(Tina::UI::UIColors::BadgeText)
        , m_badgeCorner(BadgeCorner::TopRight)
        , m_hovered(false)
        , m_pressed(false)
        , m_selected(false)
    {}

    void setText(const std::string& text) { m_text = text; }
    void setNormalColor(float r, float g, float b, float a) { m_normalColor = Tina::Core::Color(r, g, b, a); }
    void setHoverColor(float r, float g, float b, float a) { m_hoverColor = Tina::Core::Color(r, g, b, a); }
    void setPressedColor(float r, float g, float b, float a) { m_pressedColor = Tina::Core::Color(r, g, b, a); }
    void setTextColor(float r, float g, float b, float a) { m_textColor = Tina::Core::Color(r, g, b, a); }
    void setNormalColor(const Tina::Core::Color& c) { m_normalColor = c; }
    void setHoverColor(const Tina::Core::Color& c) { m_hoverColor = c; }
    void setPressedColor(const Tina::Core::Color& c) { m_pressedColor = c; }
    void setTextColor(const Tina::Core::Color& c) { m_textColor = c; }

    const std::string& getText() const { return m_text; }
    // 角标（数字小角标）API
    void setBadgeText(const std::string& text) { m_badgeText = text; }
    const std::string& badgeText() const { return m_badgeText; }
    void setBadgeColors(float br, float bg, float bb, float ba,
                        float tr, float tg, float tb, float ta) {
        m_badgeBgColor = Tina::Core::Color(br, bg, bb, ba);
        m_badgeTextColor = Tina::Core::Color(tr, tg, tb, ta);
    }
    void setBadgeColors(const Tina::Core::Color& bg, const Tina::Core::Color& text) {
        m_badgeBgColor = bg;
        m_badgeTextColor = text;
    }
    void setBadgeCorner(BadgeCorner c) { m_badgeCorner = c; }

    void setHovered(bool h) { m_hovered = h; }
    void setPressed(bool p) { m_pressed = p; }
    void setSelected(bool s) { m_selected = s; }

    void setIconTexture(bgfx::TextureHandle tex) { m_iconTex = tex; }
    bgfx::TextureHandle iconTexture() const { return m_iconTex; }

    enum class IconLayout { OverlapCenter = 0, IconTopTextBottom, IconLeftTextRight };
    void setIconLayout(IconLayout l) { m_iconLayout = l; }
    IconLayout iconLayout() const { return m_iconLayout; }

    bool isHovered() const { return m_hovered; }
    bool isPressed() const { return m_pressed; }
    bool isSelected() const { return m_selected; }

    // 文本字号（像素），0 表示使用全局默认字号
    void setFontPx(int px) { m_fontPx = std::max(0, px); }
    int fontPx() const { return m_fontPx; }
    // 角标字号（像素），0 表示使用全局默认字号
    void setBadgeFontPx(int px) { m_badgeFontPx = std::max(0, px); }
    int badgeFontPx() const { return m_badgeFontPx; }

    // === Signal 事件（推荐使用）===
    Tina::Core::Signal<> onClick;      // 点击事件（鼠标按下并松开）
    Tina::Core::Signal<> onHoverEnter; // 鼠标进入
    Tina::Core::Signal<> onHoverLeave; // 鼠标离开

    void onMouseEnter() override {
        onHoverEnter.emit();  // 触发 Signal
    }
    void onMouseLeave() override {
        onHoverLeave.emit();  // 触发 Signal
    }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    std::string m_text;
    Tina::Core::Color m_normalColor;
    Tina::Core::Color m_hoverColor;
    Tina::Core::Color m_pressedColor;
    Tina::Core::Color m_textColor;
    std::string m_badgeText;               // 角标文本（如"1"）
    Tina::Core::Color m_badgeBgColor;      // 角标背景色
    Tina::Core::Color m_badgeTextColor;    // 角标文字颜色

    BadgeCorner m_badgeCorner;
    bool m_hovered;
    bool m_pressed;
    bool m_selected;

    // 可选图标纹理（由外部提供生命周期）
    bgfx::TextureHandle m_iconTex = BGFX_INVALID_HANDLE;
    IconLayout m_iconLayout = IconLayout::IconTopTextBottom;

    int m_fontPx = 0;
    int m_badgeFontPx = 0;
};

} // namespace Tina::UI
