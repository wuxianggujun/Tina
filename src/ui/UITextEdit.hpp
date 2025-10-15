// UITextEdit：单行/多行文本编辑框
#pragma once

#include "UINode.hpp"
#include "../core/Color.hpp"
#include "UIColors.hpp"
#include "../engine/EventSystem.hpp"
#include "../engine/SubscriptionToken.hpp"
#include <string>

// 前向声明引擎事件
namespace Tina::Engine::Events {
    struct KeyPressedEvent;
    struct TextInputEvent;
}

namespace Tina::UI {

// === UITextEdit：文本输入框 ===
class UITextEdit : public UINode {
public:
    UITextEdit(const std::string& name = "TextEdit")
        : UINode(name)
        , m_placeholder("请输入文本...")
        , m_bgColor(Tina::UI::UIColors::PanelBg)
        , m_textColor(Tina::UI::UIColors::LabelText)
        , m_placeholderColor(0.5f, 0.5f, 0.5f, 0.7f)
        , m_cursorColor(Tina::UI::UIColors::LabelText)
        , m_selectionColor(0.3f, 0.5f, 0.8f, 0.4f)
        , m_focused(false)
        , m_cursorPos(0)
        , m_cursorBlinkTime(0.0f)
        , m_selectionStart(-1)
        , m_selectionEnd(-1)
        , m_dragging(false)
        , m_multiline(false)
        , m_maxLength(0)
        , m_fontPx(0)
    {
        setFocusable(true);
        setClickable(true);
    }

    // === 文本内容 ===
    void setText(const std::string& text);
    std::string getText() const;  // 返回值（需要转换）
    void clear() { setText(""); }

    // === 占位符文本 ===
    void setPlaceholder(const std::string& text) { m_placeholder = Container::String(text.c_str()); }
    const char* getPlaceholder() const { return m_placeholder.c_str(); }

    // === 颜色设置 ===
    void setBgColor(const Tina::Core::Color& c) { m_bgColor = c; }
    void setTextColor(const Tina::Core::Color& c) { m_textColor = c; }
    void setPlaceholderColor(const Tina::Core::Color& c) { m_placeholderColor = c; }
    void setCursorColor(const Tina::Core::Color& c) { m_cursorColor = c; }
    void setSelectionColor(const Tina::Core::Color& c) { m_selectionColor = c; }

    // === 多行模式 ===
    void setMultiline(bool enable) { m_multiline = enable; }
    bool isMultiline() const { return m_multiline; }

    // === 最大长度限制（按UTF-8字符数，类似Android的InputFilter.LengthFilter）===
    void setMaxLength(size_t length) { m_maxLength = length; }  // length = 字符数，0表示无限制
    size_t getMaxLength() const { return m_maxLength; }

    // === 字体大小 ===
    void setFontPx(Container::Optional<int> px) { m_fontPx = px; }
    Container::Optional<int> fontPx() const { return m_fontPx; }

    // === 焦点状态 ===
    bool isFocused() const { return m_focused; }
    void setFocus(bool focus);

    // === 光标操作 ===
    void setCursorPos(size_t pos);
    size_t getCursorPos() const { return m_cursorPos; }
    void moveCursor(int delta);

    // === 选择操作 ===
    void selectAll();
    void clearSelection();
    bool hasSelection() const { return m_selectionStart >= 0 && m_selectionEnd >= 0 && m_selectionStart != m_selectionEnd; }
    std::string getSelectedText() const;
    void deleteSelection();

    // === 剪贴板操作 ===
    void copy();
    void paste();
    void cut();

    // === 事件回调 ===
    void onMouseDown(float x, float y) override;
    void onMouseUp(float x, float y) override;
    void onClick() override;

protected:
    void onRender(uint16_t viewId, UIRenderer& renderer) override;
    void onUpdate(float dt) override;
    void onLayout() override;  // ✅ 布局变化时重新计算滚动
    Tina::Math::Vec2 measureContent(float availableWidth, float availableHeight) override;

private:
    // ✅ UTF-32字符数组（内部表示）- 参考Godot引擎的实现
    Container::Vector<char32_t> m_chars;
    Container::String m_placeholder;  // Placeholder仍用UTF-8（不常修改）
    
    // UTF-8缓存（用于渲染和getText()）- 使用std::string便于返回
    mutable std::string m_utf8Cache;
    mutable bool m_utf8Dirty = true;

    // 颜色配置
    Tina::Core::Color m_bgColor;
    Tina::Core::Color m_textColor;
    Tina::Core::Color m_placeholderColor;
    Tina::Core::Color m_cursorColor;
    Tina::Core::Color m_selectionColor;

    // 焦点状态
    bool m_focused;

    // 光标
    size_t m_cursorPos;           // 光标位置（UTF-32字符索引，不是字节索引）
    float m_cursorBlinkTime;      // 光标闪烁计时器

    // 选择区域
    int m_selectionStart;         // 选择起始位置（-1表示无选择）
    int m_selectionEnd;           // 选择结束位置
    bool m_dragging;              // 是否正在拖拽选择

    // 配置
    bool m_multiline;             // 是否支持多行
    size_t m_maxLength;           // 最大字符数（0表示无限制）
    Container::Optional<int> m_fontPx;  // 字体大小（nullopt 表示使用默认）

    // 事件订阅
    Engine::SubscriptionToken m_keyPressedToken;
    Engine::SubscriptionToken m_textInputToken;

    // UIRenderer缓存（用于文本测量）
    UIRenderer* m_renderer;
    
    // 文本滚动偏移量（用于水平滚动）
    float m_scrollOffsetX = 0.0f;

    // 内部方法
    void setupEventHandlers();
    void cleanupEventHandlers();
    void handleKeyPressed(const Engine::Events::KeyPressedEvent& e);
    void handleTextInput(const Engine::Events::TextInputEvent& e);
    void insertText(const std::string& text);
    void deleteChar(bool forward);
    size_t getPosFromX(float x);  // 根据X坐标获取光标位置
    float getXFromPos(size_t pos); // 根据光标位置获取X坐标
    void ensureCursorVisible();    // 确保光标可见（自动滚动）
};

} // namespace Tina::UI
