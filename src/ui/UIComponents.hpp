// UI 具体组件：Panel（面板）、Label（文本标签）、Button（按钮）、TextEdit（文本编辑框）
#pragma once

#include "UINode.hpp"
#include "UIAction.hpp"
#include "../core/Color.hpp"
#include "UIColors.hpp"
#include "../engine/UIEvents.hpp"  // 使用统一的UI事件
#include "../engine/EventSystem.hpp"
#include "UITextEdit.hpp"  // 文本编辑框
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

    // === 颜色设置 ===
    UIPanel* setColor(float r, float g, float b, float a) {
        m_color = Tina::Core::Color(r, g, b, a);
        m_colorOverride = true;
        return this;
    }
    UIPanel* setColor(const Tina::Core::Color& c) {
        m_color = c;
        m_colorOverride = true;
        return this;
    }
    void useThemeColor() { m_colorOverride = false; }

    Tina::Core::Color getColor() const;

    // === 重写父类方法以支持链式调用（返回UIPanel*） ===
    
    // 变换方法
    UIPanel* setSize(float w, float h) { UINode::setSize(w, h); return this; }
    UIPanel* setWidth(float w) { UINode::setWidth(w); return this; }
    UIPanel* setHeight(float h) { UINode::setHeight(h); return this; }
    UIPanel* setPosition(float x, float y) { UINode::setPosition(x, y); return this; }
    
    // 响应式尺寸方法
    UIPanel* setSizePercent(float widthPercent, float heightPercent) { 
        UINode::setSizePercent(widthPercent, heightPercent); 
        return this; 
    }
    UIPanel* setMinSize(float minW, float minH) { 
        UINode::setMinSize(minW, minH); 
        return this; 
    }
    UIPanel* setMaxSize(float maxW, float maxH) { 
        UINode::setMaxSize(maxW, maxH); 
        return this; 
    }
    
    // 对齐方法
    UIPanel* setAlign(HAlign h, VAlign v) { UINode::setAlign(h, v); return this; }
    UIPanel* setHAlign(HAlign h) { UINode::setHAlign(h); return this; }
    UIPanel* setVAlign(VAlign v) { UINode::setVAlign(v); return this; }
    UIPanel* center() { UINode::center(); return this; }
    UIPanel* centerH() { UINode::centerH(); return this; }
    UIPanel* centerV() { UINode::centerV(); return this; }
    UIPanel* alignTop() { UINode::alignTop(); return this; }
    UIPanel* alignBottom() { UINode::alignBottom(); return this; }
    UIPanel* alignLeft() { UINode::alignLeft(); return this; }
    UIPanel* alignRight() { UINode::alignRight(); return this; }
    UIPanel* alignTopLeft() { UINode::alignTopLeft(); return this; }
    UIPanel* alignTopRight() { UINode::alignTopRight(); return this; }
    UIPanel* alignBottomLeft() { UINode::alignBottomLeft(); return this; }
    UIPanel* alignBottomRight() { UINode::alignBottomRight(); return this; }
    
    // 状态方法
    UIPanel* setVisible(bool v) { UINode::setVisible(v); return this; }
    UIPanel* setEnabled(bool e) { UINode::setEnabled(e); return this; }
    UIPanel* setInteractable(bool i) { UINode::setInteractable(i); return this; }
    UIPanel* setClickable(bool v) { UINode::setClickable(v); return this; }
    UIPanel* setHoverable(bool v) { UINode::setHoverable(v); return this; }
    UIPanel* setFocusable(bool v) { UINode::setFocusable(v); return this; }
    UIPanel* setZIndex(int z) { UINode::setZIndex(z); return this; }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;

private:
    Tina::Core::Color m_color;
    bool m_colorOverride = false;
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
    void setColor(float r, float g, float b, float a) { m_color = Tina::Core::Color(r, g, b, a); m_colorOverride = true; }
    void setColor(const Tina::Core::Color& c) { m_color = c; m_colorOverride = true; }
    void useThemeColor() { m_colorOverride = false; }

    // 文本对齐设置（仅影响渲染，不影响节点布局）
    enum class TextAlignH { Left, Center, Right };
    enum class TextAlignV { Top, Center, Bottom, Baseline };
    void setAlignH(TextAlignH h) { m_alignH = h; }
    void setAlignV(TextAlignV v) { m_alignV = v; }
    void setAlignment(TextAlignH h, TextAlignV v) { m_alignH = h; m_alignV = v; }

    // 文本字号（像素）。nullopt 表示使用当前全局字号
    void setFontPx(Container::Optional<int> px) { m_fontPx = px; }
    Container::Optional<int> fontPx() const { return m_fontPx; }

    TextAlignH alignH() const { return m_alignH; }
    TextAlignV alignV() const { return m_alignV; }

    const std::string& getText() const { return m_text; }
    Tina::Core::Color getColor() const;

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;
    Tina::Math::Vec2 measureContent(float availableWidth, float availableHeight) override;

private:
    std::string m_text;
    Tina::Core::Color m_color;
    bool m_colorOverride = false;
    TextAlignH m_alignH = TextAlignH::Left;
    TextAlignV m_alignV = TextAlignV::Top;
    Container::Optional<int> m_fontPx;
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
        , m_buttonId(s_nextButtonId++)  // 自动分配唯一ID
    {
        setFocusable(true);
    }

    void setText(const std::string& text) { m_text = text; }
    void setNormalColor(float r, float g, float b, float a) { m_normalColor = Tina::Core::Color(r, g, b, a); m_normalColorOverride = true; }
    void setHoverColor(float r, float g, float b, float a) { m_hoverColor = Tina::Core::Color(r, g, b, a); m_hoverColorOverride = true; }
    void setPressedColor(float r, float g, float b, float a) { m_pressedColor = Tina::Core::Color(r, g, b, a); m_pressedColorOverride = true; }
    void setTextColor(float r, float g, float b, float a) { m_textColor = Tina::Core::Color(r, g, b, a); m_textColorOverride = true; }
    void setNormalColor(const Tina::Core::Color& c) { m_normalColor = c; m_normalColorOverride = true; }
    void setHoverColor(const Tina::Core::Color& c) { m_hoverColor = c; m_hoverColorOverride = true; }
    void setPressedColor(const Tina::Core::Color& c) { m_pressedColor = c; m_pressedColorOverride = true; }
    void setTextColor(const Tina::Core::Color& c) { m_textColor = c; m_textColorOverride = true; }
    void useThemeColors() {
        m_normalColorOverride = false;
        m_hoverColorOverride = false;
        m_pressedColorOverride = false;
        m_textColorOverride = false;
    }

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
    bool isPressed() const { return m_pressed || m_keyboardPressed; }
    bool isSelected() const { return m_selected; }

    // 文本字号（像素），nullopt 表示使用全局默认字号
    void setFontPx(Container::Optional<int> px) { m_fontPx = px; }
    Container::Optional<int> fontPx() const { return m_fontPx; }
    // 角标字号（像素），nullopt 表示使用全局默认字号
    void setBadgeFontPx(Container::Optional<int> px) { m_badgeFontPx = px; }
    Container::Optional<int> badgeFontPx() const { return m_badgeFontPx; }

    // === 鼠标事件处理 ===
    void onMouseEnter() override { setHovered(true); }

    void onMouseLeave() override { setHovered(false); }

    void onMouseDown(float /*x*/, float /*y*/) override {
        setPressed(true);
    }

    void onMouseUp(float /*x*/, float /*y*/) override {
        setPressed(false);
    }

    bool onKeyPressed(Tina::Engine::KeyCode key, bool isRepeat,
                      bool shift, bool ctrl, bool alt) override {
        (void)shift;
        const bool activationKey = key == Tina::Engine::KeyCode::Enter ||
                                   key == Tina::Engine::KeyCode::NumpadEnter ||
                                   key == Tina::Engine::KeyCode::Space;
        if (activationKey && !isRepeat && !ctrl && !alt &&
            isEnabled() && isInteractable()) {
            m_keyboardPressed = true;
        }
        // EventSystem owns the single non-repeat activation default action.
        return false;
    }

    bool onKeyReleased(Tina::Engine::KeyCode key, bool shift,
                       bool ctrl, bool alt) override {
        (void)shift;
        (void)ctrl;
        (void)alt;
        const bool activationKey = key == Tina::Engine::KeyCode::Enter ||
                                   key == Tina::Engine::KeyCode::NumpadEnter ||
                                   key == Tina::Engine::KeyCode::Space;
        if (!activationKey) return false;
        const bool wasKeyboardPressed = m_keyboardPressed;
        m_keyboardPressed = false;
        return wasKeyboardPressed;
    }

    void onFocusLost() override {
        setPressed(false);
        m_keyboardPressed = false;
    }
    bool supportsKeyboardActivation() const override { return true; }

    // 设置按钮ID（用于事件识别）
    void setButtonId(uint32_t id) { m_buttonId = id; }
    uint32_t getButtonId() const { return m_buttonId; }

    // 重写 onClick 虚函数，响应鼠标点击（会触发引擎事件 + UI 冒泡）
    void onClick() override;  // 定义在 cpp 文件中，触发事件

    // 直观接口：设置按钮点击处理器（作为默认行为）
    template<typename F>
    void setOnClick(F&& handler) {
        m_clickAction.setHandler(Tina::Container::Forward<F>(handler));
    }
    void clearOnClick() {
        m_clickAction.clearHandler();
    }

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;
    Tina::Math::Vec2 measureContent(float availableWidth, float availableHeight) override;

private:
    std::string m_text;
    Tina::Core::Color m_normalColor;
    Tina::Core::Color m_hoverColor;
    Tina::Core::Color m_pressedColor;
    Tina::Core::Color m_textColor;
    bool m_normalColorOverride = false;
    bool m_hoverColorOverride = false;
    bool m_pressedColorOverride = false;
    bool m_textColorOverride = false;
    std::string m_badgeText;               // 角标文本（如"1"）
    Tina::Core::Color m_badgeBgColor;      // 角标背景色
    Tina::Core::Color m_badgeTextColor;    // 角标文字颜色

    BadgeCorner m_badgeCorner;
    bool m_hovered;
    bool m_pressed;
    bool m_keyboardPressed = false;
    bool m_selected;

    // 可选图标纹理（由外部提供生命周期）
    bgfx::TextureHandle m_iconTex = BGFX_INVALID_HANDLE;
    IconLayout m_iconLayout = IconLayout::IconTopTextBottom;

    Container::Optional<int> m_fontPx;
    Container::Optional<int> m_badgeFontPx;

    // 按钮ID（用于事件识别）
    uint32_t m_buttonId = 0;
    static inline uint32_t s_nextButtonId = 1;

    UIAction m_clickAction;
};

} // namespace Tina::UI
