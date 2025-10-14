#include "UITextEdit.hpp"
#include "UICore.hpp"
#include "../engine/EngineEvents.hpp"
#include "../engine/InputSystem.hpp"
#include <SDL3/SDL.h>
#include <algorithm>

// 解决 Windows.h 宏冲突
#ifdef Home
#undef Home
#endif
#ifdef End
#undef End
#endif

namespace Tina::UI {

// === 文本设置 ===
void UITextEdit::setText(const std::string& text) {
    // 应用最大长度限制
    if (m_maxLength > 0 && text.length() > m_maxLength) {
        m_text = text.substr(0, m_maxLength);
    } else {
        m_text = text;
    }

    // 调整光标位置
    if (m_cursorPos > m_text.length()) {
        m_cursorPos = m_text.length();
    }

    // 清除选择
    clearSelection();
}

// === 焦点管理 ===
void UITextEdit::setFocus(bool focus) {
    if (m_focused == focus) return;

    m_focused = focus;

    if (m_focused) {
        // 获得焦点：启动文本输入模式，订阅事件
        if (auto* input = Tina::Engine::GetInput()) {
            input->startTextInput();
        }
        setupEventHandlers();
        m_cursorBlinkTime = 0.0f;  // 重置光标闪烁
    } else {
        // 失去焦点：停止文本输入模式，取消订阅
        if (auto* input = Tina::Engine::GetInput()) {
            input->stopTextInput();
        }
        cleanupEventHandlers();
        clearSelection();
    }
}

// === 光标操作 ===
void UITextEdit::setCursorPos(size_t pos) {
    m_cursorPos = std::min(pos, m_text.length());
    m_cursorBlinkTime = 0.0f;  // 重置闪烁
    clearSelection();
}

void UITextEdit::moveCursor(int delta) {
    if (delta < 0) {
        size_t absDelta = static_cast<size_t>(-delta);
        m_cursorPos = (m_cursorPos > absDelta) ? (m_cursorPos - absDelta) : 0;
    } else {
        m_cursorPos = std::min(m_cursorPos + static_cast<size_t>(delta), m_text.length());
    }
    m_cursorBlinkTime = 0.0f;
}

// === 选择操作 ===
void UITextEdit::selectAll() {
    if (m_text.empty()) return;
    m_selectionStart = 0;
    m_selectionEnd = static_cast<int>(m_text.length());
    m_cursorPos = m_text.length();
}

void UITextEdit::clearSelection() {
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

std::string UITextEdit::getSelectedText() const {
    if (!hasSelection()) return "";

    int start = std::min(m_selectionStart, m_selectionEnd);
    int end = std::max(m_selectionStart, m_selectionEnd);

    return m_text.substr(start, end - start);
}

void UITextEdit::deleteSelection() {
    if (!hasSelection()) return;

    int start = std::min(m_selectionStart, m_selectionEnd);
    int end = std::max(m_selectionStart, m_selectionEnd);

    m_text.erase(start, end - start);
    m_cursorPos = start;
    clearSelection();
}

// === 剪贴板操作 ===
void UITextEdit::copy() {
    if (!hasSelection()) return;
    std::string selected = getSelectedText();
    if (!selected.empty()) {
        SDL_SetClipboardText(selected.c_str());
    }
}

void UITextEdit::paste() {
    if (!SDL_HasClipboardText()) return;

    char* clipText = SDL_GetClipboardText();
    if (clipText) {
        insertText(clipText);
        SDL_free(clipText);
    }
}

void UITextEdit::cut() {
    if (!hasSelection()) return;
    copy();
    deleteSelection();
}

// === 鼠标事件 ===
void UITextEdit::onMouseDown(float x, float y) {
    UINode::onMouseDown(x, y);

    // 记录拖拽起始位置
    m_dragging = true;
    size_t pos = getPosFromX(x);
    m_cursorPos = pos;
    m_selectionStart = static_cast<int>(pos);
    m_selectionEnd = static_cast<int>(pos);
    m_cursorBlinkTime = 0.0f;
}

void UITextEdit::onMouseUp(float x, float y) {
    UINode::onMouseUp(x, y);

    // 结束拖拽
    m_dragging = false;

    // 如果选择区域起始和结束相同，清除选择
    if (m_selectionStart == m_selectionEnd) {
        clearSelection();
    }
}

void UITextEdit::onClick() {
    UINode::onClick();

    // 点击获得焦点
    setFocus(true);
}

// === 渲染 ===
void UITextEdit::onRender(uint16_t viewId, UIRenderer& renderer) {
    auto worldPos = getWorldPosition();
    auto size = getSize();

    // 1. 绘制背景
    renderer.drawRect(viewId, worldPos.x, worldPos.y, size.x, size.y, m_bgColor);

    // 2. 绘制选择区域高亮
    if (hasSelection()) {
        int start = std::min(m_selectionStart, m_selectionEnd);
        int end = std::max(m_selectionStart, m_selectionEnd);

        float startX = getXFromPos(start);
        float endX = getXFromPos(end);
        float padding = 4.0f;

        renderer.drawRect(viewId,
            worldPos.x + padding + startX,
            worldPos.y + padding,
            endX - startX,
            size.y - padding * 2,
            m_selectionColor);
    }

    // 3. 绘制文本或占位符
    UIRenderer::TextOptions opts;
    opts.r = m_textColor.r();
    opts.g = m_textColor.g();
    opts.b = m_textColor.b();
    opts.a = m_textColor.a();
    opts.fontPx = m_fontPx;

    float padding = 4.0f;

    if (m_text.empty() && !m_focused) {
        // 显示占位符
        opts.r = m_placeholderColor.r();
        opts.g = m_placeholderColor.g();
        opts.b = m_placeholderColor.b();
        opts.a = m_placeholderColor.a();
        renderer.drawText(viewId, worldPos.x + padding, worldPos.y + padding, m_placeholder, opts);
    } else {
        // 显示实际文本
        renderer.drawText(viewId, worldPos.x + padding, worldPos.y + padding, m_text, opts);
    }

    // 4. 绘制光标（仅在聚焦且闪烁可见时）
    if (m_focused && static_cast<int>(m_cursorBlinkTime * 2.0f) % 2 == 0) {
        float cursorX = getXFromPos(m_cursorPos);
        float cursorWidth = 2.0f;

        renderer.drawRect(viewId,
            worldPos.x + padding + cursorX,
            worldPos.y + padding,
            cursorWidth,
            size.y - padding * 2,
            m_cursorColor);
    }
}

// === 更新 ===
void UITextEdit::onUpdate(float dt) {
    UINode::onUpdate(dt);

    // 更新光标闪烁
    if (m_focused) {
        m_cursorBlinkTime += dt;
        if (m_cursorBlinkTime > 1.0f) {
            m_cursorBlinkTime = 0.0f;
        }
    }

    // 处理拖拽选择
    if (m_dragging) {
        auto* input = Tina::Engine::GetInput();
        if (input) {
            auto mousePos = input->getMousePosition();
            auto worldPos = getWorldPosition();
            float localX = mousePos.x - worldPos.x;

            size_t pos = getPosFromX(localX);
            m_selectionEnd = static_cast<int>(pos);
            m_cursorPos = pos;
        }
    }
}

// === 事件处理器设置 ===
void UITextEdit::setupEventHandlers() {
    if (!eventSystem()) return;

    // 订阅键盘按键事件
    m_keyPressedToken = eventSystem()->subscribe<Engine::Events::KeyPressedEvent>(
        [this](const Engine::Events::KeyPressedEvent& e) {
            if (m_focused) {
                handleKeyPressed(e);
            }
        }
    );

    // 订阅文本输入事件
    m_textInputToken = eventSystem()->subscribe<Engine::Events::TextInputEvent>(
        [this](const Engine::Events::TextInputEvent& e) {
            if (m_focused) {
                handleTextInput(e);
            }
        }
    );
}

void UITextEdit::cleanupEventHandlers() {
    m_keyPressedToken.reset();
    m_textInputToken.reset();
}

// === 键盘输入处理 ===
void UITextEdit::handleKeyPressed(const Engine::Events::KeyPressedEvent& e) {
    // 注意：KeyCode 现在来自 Engine 命名空间（定义在 InputSystem.hpp）
    // Engine::Events::KeyCode 只是一个别名
    using KeyCode = Engine::Events::KeyCode;

    // 处理特殊按键
    switch (e.key) {
        case KeyCode::Backspace:
            if (hasSelection()) {
                deleteSelection();
            } else {
                deleteChar(false);
            }
            break;

        case KeyCode::Delete:
            if (hasSelection()) {
                deleteSelection();
            } else {
                deleteChar(true);
            }
            break;

        case KeyCode::Left:
            if (e.shift) {
                // Shift+左：扩展选择
                if (m_selectionStart < 0) {
                    m_selectionStart = static_cast<int>(m_cursorPos);
                }
                if (m_cursorPos > 0) {
                    m_cursorPos--;
                    m_selectionEnd = static_cast<int>(m_cursorPos);
                }
            } else {
                // 左：移动光标
                if (hasSelection()) {
                    m_cursorPos = std::min(m_selectionStart, m_selectionEnd);
                    clearSelection();
                } else if (m_cursorPos > 0) {
                    m_cursorPos--;
                }
            }
            m_cursorBlinkTime = 0.0f;
            break;

        case KeyCode::Right:
            if (e.shift) {
                // Shift+右：扩展选择
                if (m_selectionStart < 0) {
                    m_selectionStart = static_cast<int>(m_cursorPos);
                }
                if (m_cursorPos < m_text.length()) {
                    m_cursorPos++;
                    m_selectionEnd = static_cast<int>(m_cursorPos);
                }
            } else {
                // 右：移动光标
                if (hasSelection()) {
                    m_cursorPos = std::max(m_selectionStart, m_selectionEnd);
                    clearSelection();
                } else if (m_cursorPos < m_text.length()) {
                    m_cursorPos++;
                }
            }
            m_cursorBlinkTime = 0.0f;
            break;

        case KeyCode::Home:
            m_cursorPos = 0;
            m_cursorBlinkTime = 0.0f;
            if (!e.shift) clearSelection();
            break;

        case KeyCode::End:
            m_cursorPos = m_text.length();
            m_cursorBlinkTime = 0.0f;
            if (!e.shift) clearSelection();
            break;

        case KeyCode::A:
            if (e.ctrl) {
                selectAll();
            }
            break;

        case KeyCode::C:
            if (e.ctrl) {
                copy();
            }
            break;

        case KeyCode::V:
            if (e.ctrl) {
                paste();
            }
            break;

        case KeyCode::X:
            if (e.ctrl) {
                cut();
            }
            break;

        case KeyCode::Enter:
            if (m_multiline) {
                insertText("\n");
            } else {
                // 单行模式：触发提交事件（未来实现）
                setFocus(false);
            }
            break;

        case KeyCode::Escape:
            setFocus(false);
            break;

        default:
            break;
    }
}

// === 文本输入处理 ===
void UITextEdit::handleTextInput(const Engine::Events::TextInputEvent& e) {
    if (e.textLength > 0) {
        insertText(std::string(e.text, e.textLength));
    }
}

// === 插入文本 ===
void UITextEdit::insertText(const std::string& text) {
    if (text.empty()) return;

    // 删除选中内容
    if (hasSelection()) {
        deleteSelection();
    }

    // 检查最大长度限制
    if (m_maxLength > 0) {
        size_t available = m_maxLength - m_text.length();
        if (available == 0) return;

        std::string toInsert = text.substr(0, std::min(text.length(), available));
        m_text.insert(m_cursorPos, toInsert);
        m_cursorPos += toInsert.length();
    } else {
        m_text.insert(m_cursorPos, text);
        m_cursorPos += text.length();
    }

    m_cursorBlinkTime = 0.0f;
}

// === 删除字符 ===
void UITextEdit::deleteChar(bool forward) {
    if (m_text.empty()) return;

    if (forward) {
        // Delete：删除光标后的字符
        if (m_cursorPos < m_text.length()) {
            m_text.erase(m_cursorPos, 1);
        }
    } else {
        // Backspace：删除光标前的字符
        if (m_cursorPos > 0) {
            m_text.erase(m_cursorPos - 1, 1);
            m_cursorPos--;
        }
    }

    m_cursorBlinkTime = 0.0f;
}

// === 坐标与位置转换 ===
size_t UITextEdit::getPosFromX(float x) {
    // 简化实现：按字符平均宽度估算
    // TODO: 使用 TextRenderer::measureText 进行精确计算
    float padding = 4.0f;
    x -= padding;

    if (x <= 0) return 0;
    if (m_text.empty()) return 0;

    // 粗略估算：假设每个字符8像素宽
    float charWidth = 8.0f;
    size_t pos = static_cast<size_t>(x / charWidth);

    return std::min(pos, m_text.length());
}

float UITextEdit::getXFromPos(size_t pos) {
    // 简化实现：按字符平均宽度估算
    // TODO: 使用 TextRenderer::measureText 进行精确计算
    if (pos == 0 || m_text.empty()) return 0.0f;

    // 粗略估算：假设每个字符8像素宽
    float charWidth = 8.0f;
    return pos * charWidth;
}

} // namespace Tina::UI
