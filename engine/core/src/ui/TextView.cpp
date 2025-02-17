#include "tina/ui/TextView.hpp"
#include "tina/log/Logger.hpp"
#include "tina/ui/UITypes.hpp"
#include "tina/view/UIView.hpp"

namespace Tina {

TextView::TextView(const std::string& name) : UIWidget(name) {
    // 设置默认约束
    LayoutConstraints constraints;
    constraints.minWidth = 100;
    constraints.minHeight = 30;
    setConstraints(constraints);
    
    TINA_LOG_DEBUG("Created TextView '{}'", name);
}

void TextView::setText(const std::string& text) {
    if (m_text != text) {
        m_text = text;
        m_cursorPosition = 0;
        clearSelection();
        updateTextLayout();
        
        TINA_LOG_DEBUG("TextView '{}' text updated: {}", getName(), text);
    }
}

void TextView::setSelection(size_t start, size_t end) {
    start = std::min(start, m_text.length());
    end = std::min(end, m_text.length());
    
    if (start > end) {
        std::swap(start, end);
    }
    
    m_selectionStart = start;
    m_selectionEnd = end;
    m_cursorPosition = end;
    
    TINA_LOG_DEBUG("TextView '{}' selection updated: {}->{}",
        getName(), m_selectionStart, m_selectionEnd);
}

void TextView::clearSelection() {
    m_selectionStart = m_selectionEnd = 0;
}

void TextView::draw() {
    if (!isVisible() || !getView()) return;
    
    // 调用基类绘制
    UIWidget::draw();
    
    // 获取绘制区域
    const auto& bounds = getBounds();
    const auto& constraints = getConstraints();
    
    // 绘制背景
    RectStyle bgStyle;
    bgStyle.fillColor = {0.2f, 0.2f, 0.2f, 1.0f};
    if (auto view = std::dynamic_pointer_cast<UIView>(getView())) {
        view->drawRect(bounds, bgStyle);
    }
    
    // 绘制选中区域
    if (hasSelection()) {
        RectStyle selStyle;
        selStyle.fillColor = {0.3f, 0.5f, 0.7f, 0.5f};
        
        // TODO: 计算选中区域的位置和大小
        glm::vec2 selStart = getPositionForCharIndex(m_selectionStart);
        glm::vec2 selEnd = getPositionForCharIndex(m_selectionEnd);
        
        if (auto view = std::dynamic_pointer_cast<UIView>(getView())) {
            Rect selRect(
                bounds.getPosition() + glm::vec2(selStart.x, selStart.y),
                glm::vec2(selEnd.x - selStart.x, m_fontSize)
            );
            view->drawRect(selRect, selStyle);
        }
    }
    
    // 绘制文本
    float y = bounds.getPosition().y + constraints.padding - m_scrollY;
    for (const auto& line : m_lines) {
        // 如果行在可见区域内
        if (y + line.height >= bounds.getPosition().y && 
            y <= bounds.getPosition().y + bounds.getSize().y) {
            // TODO: 实现文本渲染
            // if (auto view = std::dynamic_pointer_cast<UIView>(getView())) {
            //     view->drawText(
            //         line.text,
            //         {bounds.getPosition().x + constraints.padding - m_scrollX, y},
            //         m_fontSize,
            //         m_textColor
            //     );
            // }
        }
        y += line.height;
    }
    
    // 绘制光标
    if (m_editable && isFocused() && m_cursorVisible) {
        glm::vec2 cursorPos = getPositionForCharIndex(m_cursorPosition);
        
        LineStyle cursorStyle;
        cursorStyle.thickness = 2.0f;
        cursorStyle.color = m_textColor;
        
        if (auto view = std::dynamic_pointer_cast<UIView>(getView())) {
            glm::vec2 start = bounds.getPosition() + glm::vec2(cursorPos.x, cursorPos.y);
            glm::vec2 end = start + glm::vec2(0, m_fontSize);
            view->drawLine(start, end, cursorStyle);
        }
    }
}

bool TextView::onMouseButton(Event::MouseButton button, bool pressed, float x, float y) {
    if (!m_editable || button != Event::MouseButton::Left) return false;
    
    if (pressed) {
        // 获取点击位置对应的字符索引
        const auto& bounds = getBounds();
        size_t charIndex = getCharIndexAtPosition(x - bounds.getPosition().x, y - bounds.getPosition().y);
        
        if (Event::isKeyPressed(Event::KeyCode::LeftShift)) {
            // Shift+点击扩展选区
            setSelection(m_selectionStart, charIndex);
        } else {
            // 普通点击设置光标位置
            m_cursorPosition = charIndex;
            clearSelection();
        }
        
        m_cursorVisible = true;
        m_cursorBlinkTime = 0;
        
        return true;
    }
    
    return false;
}

bool TextView::onKeyboard(Event::KeyCode key, bool pressed) {
    if (!m_editable || !pressed) return false;
    
    bool handled = true;
    
    switch (key) {
        case Event::KeyCode::Left:
            if (m_cursorPosition > 0) {
                if (Event::isKeyPressed(Event::KeyCode::LeftShift)) {
                    // Shift+Left扩展选区
                    if (!hasSelection()) {
                        m_selectionStart = m_cursorPosition;
                    }
                    m_cursorPosition--;
                    m_selectionEnd = m_cursorPosition;
                } else {
                    // 普通Left移动光标
                    m_cursorPosition--;
                    clearSelection();
                }
            }
            break;
            
        case Event::KeyCode::Right:
            if (m_cursorPosition < m_text.length()) {
                if (Event::isKeyPressed(Event::KeyCode::LeftShift)) {
                    // Shift+Right扩展选区
                    if (!hasSelection()) {
                        m_selectionStart = m_cursorPosition;
                    }
                    m_cursorPosition++;
                    m_selectionEnd = m_cursorPosition;
                } else {
                    // 普通Right移动光标
                    m_cursorPosition++;
                    clearSelection();
                }
            }
            break;
            
        case Event::KeyCode::Backspace:
            if (hasSelection()) {
                deleteSelection();
            } else if (m_cursorPosition > 0) {
                m_text.erase(m_cursorPosition - 1, 1);
                m_cursorPosition--;
                updateTextLayout();
            }
            break;
            
        case Event::KeyCode::Delete:
            if (hasSelection()) {
                deleteSelection();
            } else if (m_cursorPosition < m_text.length()) {
                m_text.erase(m_cursorPosition, 1);
                updateTextLayout();
            }
            break;
            
        default:
            handled = false;
            break;
    }
    
    if (handled) {
        m_cursorVisible = true;
        m_cursorBlinkTime = 0;
        ensureCursorVisible();
    }
    
    return handled;
}

bool TextView::onChar(unsigned int codepoint) {
    if (!m_editable) return false;
    
    // 转换codepoint为UTF-8
    std::string utf8;
    if (codepoint < 0x80) {
        utf8.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        utf8.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        utf8.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    
    insertText(utf8);
    return true;
}

bool TextView::onFocus(bool focused) {
    if (UIWidget::onFocus(focused)) {
        m_cursorVisible = focused;
        m_cursorBlinkTime = 0;
        return true;
    }
    return false;
}

void TextView::insertText(const std::string& text) {
    if (hasSelection()) {
        deleteSelection();
    }
    
    m_text.insert(m_cursorPosition, text);
    m_cursorPosition += text.length();
    updateTextLayout();
    ensureCursorVisible();
    
    TINA_LOG_DEBUG("TextView '{}' inserted text at position {}: {}",
        getName(), m_cursorPosition - text.length(), text);
}

void TextView::deleteSelection() {
    if (!hasSelection()) return;
    
    size_t start = std::min(m_selectionStart, m_selectionEnd);
    size_t end = std::max(m_selectionStart, m_selectionEnd);
    
    m_text.erase(start, end - start);
    m_cursorPosition = start;
    clearSelection();
    updateTextLayout();
    
    TINA_LOG_DEBUG("TextView '{}' deleted selection {}->{}",
        getName(), start, end);
}

void TextView::updateTextLayout() {
    m_lines.clear();
    
    if (m_text.empty()) {
        TextLine line;
        line.height = m_fontSize;
        m_lines.push_back(line);
        return;
    }
    
    if (m_multiLine) {
        // TODO: 实现多行文本布局
    } else {
        TextLine line;
        line.text = m_text;
        // TODO: 计算文本宽度
        line.width = m_text.length() * m_fontSize * 0.6f; // 临时估算
        line.height = m_fontSize;
        m_lines.push_back(line);
    }
}

void TextView::updateCursorPosition() {
    m_cursorBlinkTime += 0.016f; // 假设60FPS
    if (m_cursorBlinkTime >= 0.5f) {
        m_cursorBlinkTime = 0;
        m_cursorVisible = !m_cursorVisible;
    }
}

size_t TextView::getCharIndexAtPosition(float x, float y) const {
    // TODO: 实现更精确的字符索引计算
    x = std::max(0.0f, x - getConstraints().padding + m_scrollX);
    float charWidth = m_fontSize * 0.6f; // 临时估算
    size_t index = static_cast<size_t>(x / charWidth);
    return std::min(index, m_text.length());
}

glm::vec2 TextView::getPositionForCharIndex(size_t index) const {
    // TODO: 实现更精确的位置计算
    float charWidth = m_fontSize * 0.6f; // 临时估算
    return {
        getConstraints().padding + index * charWidth - m_scrollX,
        getConstraints().padding
    };
}

void TextView::ensureCursorVisible() {
    const auto& bounds = getBounds();
    glm::vec2 cursorPos = getPositionForCharIndex(m_cursorPosition);
    
    // 水平滚动
    if (cursorPos.x < m_scrollX) {
        m_scrollX = cursorPos.x;
    } else if (cursorPos.x > m_scrollX + bounds.getSize().x - getConstraints().padding * 2) {
        m_scrollX = cursorPos.x - bounds.getSize().x + getConstraints().padding * 2;
    }
    
    // 垂直滚动(多行模式)
    if (m_multiLine) {
        if (cursorPos.y < m_scrollY) {
            m_scrollY = cursorPos.y;
        } else if (cursorPos.y + m_fontSize > m_scrollY + bounds.getSize().y - getConstraints().padding * 2) {
            m_scrollY = cursorPos.y + m_fontSize - bounds.getSize().y + getConstraints().padding * 2;
        }
    }
}

} // namespace Tina 