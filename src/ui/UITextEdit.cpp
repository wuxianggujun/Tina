#include "UITextEdit.hpp"
#include "UICore.hpp"
#include "../engine/EngineEvents.hpp"
#include "../engine/InputSystem.hpp"
#include "../engine/Application.hpp"
#include "../core/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <utf8.h>

// 解决 Windows.h 宏冲突
#ifdef Home
#undef Home
#endif
#ifdef End
#undef End
#endif

namespace Tina::UI {

// === UTF-8 辅助函数：计算字符数 ===
static size_t getUTF8CharCount(const std::string& str) {
    try {
        return utf8::distance(str.begin(), str.end());
    } catch (const utf8::exception&) {
        // UTF-8序列损坏，降级为字节数
        return str.length();
    }
}

// === 文本设置 ===
void UITextEdit::setText(const std::string& text) {
    // ✅ UTF-8验证：拒绝无效的UTF-8序列
    if (!utf8::is_valid(text.begin(), text.end())) {
        TINA_ERROR("UITextEdit::setText - Invalid UTF-8 sequence rejected, text not set");
        return;
    }

    // ✅ 按字符数限制（参考Android）
    if (m_maxLength > 0) {
        size_t charCount = getUTF8CharCount(text);
        if (charCount > m_maxLength) {
            // 截断到maxLength个字符
            auto it = text.begin();
            size_t count = 0;
            while (it != text.end() && count < m_maxLength) {
                auto next_it = it;
                try {
                    utf8::next(next_it, text.end());
                    it = next_it;
                    count++;
                } catch (const utf8::exception&) {
                    break;
                }
            }
            m_text = text.substr(0, std::distance(text.begin(), it));
            TINA_WARN("UITextEdit::setText - Text truncated to {} chars (limit={})", 
                      getUTF8CharCount(m_text), m_maxLength);
        } else {
            m_text = text;
        }
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

// === 测量（根据文本/字号估计自然尺寸） ===
Tina::Math::Vec2 UITextEdit::measureContent(float availableWidth, float /*availableHeight*/)
{
    auto* app = Tina::Engine::Application::instance();
    float tw = 0.0f, th = 0.0f;
    if (app) {
        auto& tr = app->textRenderer();
        int prev = tr.currentFontPx();
        bool needRestore = false;
        if (m_fontPx.has_value() && m_fontPx.value() > 0 && m_fontPx.value() != prev) {
            if (tr.setFontPx(m_fontPx.value())) needRestore = true;
        }
        const std::string& src = m_text.empty() ? m_placeholder : m_text;
        tr.measureText(src, tw, th);
        if (needRestore) tr.setFontPx(prev);
    } else {
        // 无法测量时使用保守估计
        tw = std::max(100.0f, (float)m_text.size() * 8.0f);
        th = 20.0f;
    }
    // 内边距（与渲染一致）
    const float pad = 4.0f;
    float minW = tw + pad * 2.0f;
    float minH = th + pad * 2.0f;
    // 单行模式：高度至少 28
    if (!m_multiline) minH = std::max(minH, 28.0f);

    // 在可用宽度约束下返回期望值（不硬性裁剪）
    (void)availableWidth;
    return {minW, minH};
}

// === 焦点管理 ===
void UITextEdit::setFocus(bool focus) {
    if (m_focused == focus) return;

    TINA_INFO("UITextEdit::setFocus - name='{}', focus={}", getName(), focus);

    m_focused = focus;

    if (m_focused) {
        // 获得焦点：启动文本输入模式，订阅事件
        if (auto* input = Tina::Engine::GetInput()) {
            input->startTextInput();
            TINA_INFO("UITextEdit::setFocus - 文本输入模式已启动");
        } else {
            TINA_WARN("UITextEdit::setFocus - 无法获取InputSystem");
        }
        setupEventHandlers();
        m_cursorBlinkTime = 0.0f;  // 重置光标闪烁
    } else {
        // 失去焦点：停止文本输入模式，取消订阅
        if (auto* input = Tina::Engine::GetInput()) {
            input->stopTextInput();
            TINA_INFO("UITextEdit::setFocus - 文本输入模式已停止");
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
    ensureCursorVisible();  // ✅ 自动滚动
}

void UITextEdit::moveCursor(int delta) {
    if (delta == 0) return;
    
    // ✅ 按UTF-8字符移动，而不是按字节移动
    if (delta < 0) {
        // 向左移动：找到前一个UTF-8字符
        int steps = -delta;
        while (steps > 0 && m_cursorPos > 0) {
            auto it = m_text.begin() + m_cursorPos;
            try {
                utf8::prior(it, m_text.begin());
                m_cursorPos = std::distance(m_text.begin(), it);
            } catch (const utf8::exception&) {
                // UTF-8错误，降级为字节移动
                m_cursorPos = (m_cursorPos > 0) ? (m_cursorPos - 1) : 0;
            }
            steps--;
        }
    } else {
        // 向右移动：找到下一个UTF-8字符
        int steps = delta;
        while (steps > 0 && m_cursorPos < m_text.length()) {
            auto it = m_text.begin() + m_cursorPos;
            try {
                utf8::next(it, m_text.end());
                m_cursorPos = std::distance(m_text.begin(), it);
            } catch (const utf8::exception&) {
                // UTF-8错误，降级为字节移动
                m_cursorPos = std::min(m_cursorPos + 1, m_text.length());
            }
            steps--;
        }
    }
    
    m_cursorBlinkTime = 0.0f;
    ensureCursorVisible();  // ✅ 自动滚动
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
    // ✅ 考虑滚动偏移
    size_t pos = getPosFromX(x + m_scrollOffsetX);
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

    TINA_INFO("UITextEdit::onClick - name='{}'", getName());
    
    // 点击获得焦点
    setFocus(true);
}

// === 渲染 ===
void UITextEdit::onRender(uint16_t viewId, UIRenderer& renderer) {
    // 缓存renderer指针用于文本测量
    m_renderer = &renderer;
    
    auto worldPos = getWorldPosition();
    auto size = getSize();

    // 1. 绘制背景
    renderer.drawRect(viewId, worldPos.x, worldPos.y, size.x, size.y, m_bgColor);

    // 2. 绘制选择高亮（✅ 应用水平滚动）
    if (hasSelection()) {
        int selStart = std::min(m_selectionStart, m_selectionEnd);
        int selEnd = std::max(m_selectionStart, m_selectionEnd);

        float startX = getXFromPos(selStart);
        float endX = getXFromPos(selEnd);
        float padding = 4.0f;

        renderer.drawRect(viewId,
            worldPos.x + padding + startX - m_scrollOffsetX,
            worldPos.y + padding,
            endX - startX,
            size.y - padding * 2,
            m_selectionColor);
    }

    // 3. 绘制文本或占位符（✅ 应用水平滚动）
    UIRenderer::TextOptions opts;
    opts.r = m_textColor.r();
    opts.g = m_textColor.g();
    opts.b = m_textColor.b();
    opts.a = m_textColor.a();
    opts.fontPx = m_fontPx;

    float padding = 4.0f;

    if (m_text.empty() && !m_focused) {
        // 显示占位符（无滚动）
        opts.r = m_placeholderColor.r();
        opts.g = m_placeholderColor.g();
        opts.b = m_placeholderColor.b();
        opts.a = m_placeholderColor.a();
        renderer.drawText(viewId, worldPos.x + padding, worldPos.y + padding, m_placeholder, opts);
    } else {
        // 显示实际文本（✅ 应用滚动偏移）
        renderer.drawText(viewId, worldPos.x + padding - m_scrollOffsetX, worldPos.y + padding, m_text, opts);
    }

    // 4. 绘制光标（仅在聚焦且闪烁可见时，✅ 应用滚动偏移）
    if (m_focused && static_cast<int>(m_cursorBlinkTime * 2.0f) % 2 == 0) {
        float cursorX = getXFromPos(m_cursorPos);
        float cursorWidth = 2.0f;

        renderer.drawRect(viewId,
            worldPos.x + padding + cursorX - m_scrollOffsetX,
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

            // ✅ 考虑滚动偏移
            size_t pos = getPosFromX(localX + m_scrollOffsetX);
            m_selectionEnd = static_cast<int>(pos);
            m_cursorPos = pos;
        }
    }
}

// === 事件处理器设置 ===
void UITextEdit::setupEventHandlers() {
    if (!eventSystem()) {
        TINA_ERROR("UITextEdit::setupEventHandlers - eventSystem() is null!");
        return;
    }

    TINA_INFO("UITextEdit::setupEventHandlers - name='{}'", getName());

    // 订阅键盘按键事件
    m_keyPressedToken = eventSystem()->subscribe<Engine::Events::KeyPressedEvent>(
        [this](const Engine::Events::KeyPressedEvent& e) {
            TINA_INFO("UITextEdit - KeyPressed event received, key={}, focused={}", 
                      static_cast<int>(e.key), m_focused);
            if (m_focused) {
                handleKeyPressed(e);
            }
        }
    );

    // 订阅文本输入事件
    m_textInputToken = eventSystem()->subscribe<Engine::Events::TextInputEvent>(
        [this](const Engine::Events::TextInputEvent& e) {
            TINA_INFO("UITextEdit - TextInput event received, focused={}", m_focused);
            if (m_focused) {
                handleTextInput(e);
            }
        }
    );
    
    TINA_INFO("UITextEdit::setupEventHandlers - 事件订阅完成");
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
            TINA_INFO("UITextEdit: Backspace pressed, hasSelection={}", hasSelection());
            if (hasSelection()) {
                deleteSelection();
            } else {
                deleteChar(false);
            }
            break;

        case KeyCode::Delete:
            TINA_INFO("UITextEdit: Delete pressed, cursorPos={}, textLength={}", m_cursorPos, m_text.length());
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
                    moveCursor(-1);  // ✅ 按UTF-8字符移动
                    m_selectionEnd = static_cast<int>(m_cursorPos);
                }
            } else {
                // 左：移动光标
                if (hasSelection()) {
                    m_cursorPos = std::min(m_selectionStart, m_selectionEnd);
                    clearSelection();
                    ensureCursorVisible();
                } else {
                    moveCursor(-1);  // ✅ 按UTF-8字符移动
                }
            }
            break;

        case KeyCode::Right:
            if (e.shift) {
                // Shift+右：扩展选择
                if (m_selectionStart < 0) {
                    m_selectionStart = static_cast<int>(m_cursorPos);
                }
                if (m_cursorPos < m_text.length()) {
                    moveCursor(1);  // ✅ 按UTF-8字符移动
                    m_selectionEnd = static_cast<int>(m_cursorPos);
                }
            } else {
                // 右：移动光标
                if (hasSelection()) {
                    m_cursorPos = std::max(m_selectionStart, m_selectionEnd);
                    clearSelection();
                    ensureCursorVisible();
                } else {
                    moveCursor(1);  // ✅ 按UTF-8字符移动
                }
            }
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

        case KeyCode::Tab:
            // Tab键：不处理（避免插入Tab字符导致乱码）
            // TODO: 可以用于焦点切换
            TINA_INFO("UITextEdit: Tab key ignored");
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

    // ✅ UTF-8验证：拒绝无效的UTF-8序列
    if (!utf8::is_valid(text.begin(), text.end())) {
        TINA_WARN("UITextEdit::insertText - Invalid UTF-8 sequence rejected");
        return;
    }

    // 删除选中内容
    if (hasSelection()) {
        deleteSelection();
    }

    // ✅ 按字符数限制（参考Android实现）
    if (m_maxLength > 0) {
        size_t currentCharCount = getUTF8CharCount(m_text);
        size_t inputCharCount = getUTF8CharCount(text);
        
        // 检查是否超出字符数限制
        if (currentCharCount >= m_maxLength) {
            TINA_WARN("UITextEdit::insertText - Max character count reached ({}/{}), text rejected", 
                      currentCharCount, m_maxLength);
            return;
        }

        // 计算可插入的字符数
        size_t availableChars = m_maxLength - currentCharCount;
        
        if (inputCharCount <= availableChars) {
            // 完整插入
            m_text.insert(m_cursorPos, text);
            m_cursorPos += text.length();
            TINA_INFO("UITextEdit::insertText - Inserted '{}', charCount={}/{} (bytes={})", 
                      text, currentCharCount + inputCharCount, m_maxLength, m_text.length());
        } else {
            // 需要截断：只插入部分字符
            std::string toInsert;
            auto it = text.begin();
            size_t charCount = 0;
            
            while (it != text.end() && charCount < availableChars) {
                auto next_it = it;
                try {
                    utf8::next(next_it, text.end());
                    toInsert.append(it, next_it);
                    it = next_it;
                    charCount++;
                } catch (const utf8::exception&) {
                    break;
                }
            }
            
            if (!toInsert.empty()) {
                m_text.insert(m_cursorPos, toInsert);
                m_cursorPos += toInsert.length();
                TINA_INFO("UITextEdit::insertText - Inserted '{}' (truncated from {} to {} chars), charCount={}/{}", 
                          toInsert, inputCharCount, charCount, currentCharCount + charCount, m_maxLength);
            }
        }
    } else {
        // 无限制
        m_text.insert(m_cursorPos, text);
        m_cursorPos += text.length();
        size_t charCount = getUTF8CharCount(m_text);
        TINA_INFO("UITextEdit::insertText - Inserted '{}', charCount={} (no limit, bytes={})", 
                  text, charCount, m_text.length());
    }

    m_cursorBlinkTime = 0.0f;
    ensureCursorVisible();  // ✅ 自动滚动
}

// === 删除字符 ===
void UITextEdit::deleteChar(bool forward) {
    if (m_text.empty()) return;

    TINA_INFO("UITextEdit::deleteChar - forward={}, cursorPos={}, textLength={}, text='{}'", 
              forward, m_cursorPos, m_text.length(), m_text);

    if (forward) {
        // Delete：删除光标后的UTF-8字符
        if (m_cursorPos < m_text.length()) {
            // 使用utfcpp找到下一个UTF-8字符的位置
            auto it = m_text.begin() + m_cursorPos;
            auto next_it = it;
            try {
                utf8::next(next_it, m_text.end());  // 移动到下一个UTF-8字符
                size_t charLen = std::distance(it, next_it);
                m_text.erase(m_cursorPos, charLen);
                TINA_INFO("UITextEdit::deleteChar - After delete forward (charLen={}), text='{}'", charLen, m_text);
            } catch (const utf8::exception& e) {
                // UTF-8序列损坏：降级为删除1字节（尝试修复乱码）
                TINA_WARN("UITextEdit::deleteChar - UTF-8 error: {}, fallback to 1-byte delete", e.what());
                m_text.erase(m_cursorPos, 1);
                TINA_INFO("UITextEdit::deleteChar - After fallback delete, text='{}'", m_text);
            }
        } else {
            TINA_WARN("UITextEdit::deleteChar - Cannot delete forward, cursor at end");
        }
    } else {
        // Backspace：删除光标前的UTF-8字符
        if (m_cursorPos > 0) {
            // 使用utfcpp找到前一个UTF-8字符的位置
            auto it = m_text.begin() + m_cursorPos;
            auto prev_it = it;
            try {
                utf8::prior(prev_it, m_text.begin());  // 移动到前一个UTF-8字符
                size_t prevPos = std::distance(m_text.begin(), prev_it);
                size_t charLen = m_cursorPos - prevPos;
                m_text.erase(prevPos, charLen);
                m_cursorPos = prevPos;
                TINA_INFO("UITextEdit::deleteChar - After backspace (charLen={}), text='{}'", charLen, m_text);
            } catch (const utf8::exception& e) {
                // UTF-8序列损坏：降级为删除1字节（尝试修复乱码）
                TINA_WARN("UITextEdit::deleteChar - UTF-8 error: {}, fallback to 1-byte delete", e.what());
                m_text.erase(m_cursorPos - 1, 1);
                m_cursorPos--;
                TINA_INFO("UITextEdit::deleteChar - After fallback delete, text='{}'", m_text);
            }
        } else {
            TINA_WARN("UITextEdit::deleteChar - Cannot backspace, cursor at start");
        }
    }

    m_cursorBlinkTime = 0.0f;
    ensureCursorVisible();  // ✅ 自动滚动
}

// === 确保光标可见（自动滚动）===
void UITextEdit::ensureCursorVisible() {
    if (!m_renderer) return;
    
    float padding = 4.0f;
    float availableWidth = getSize().x - padding * 2;
    float cursorX = getXFromPos(m_cursorPos);
    
    // 光标相对于可视区域的位置
    float visibleCursorX = cursorX - m_scrollOffsetX;
    
    // 右边距：光标需要距离右边界至少20px
    float rightMargin = 20.0f;
    
    // 如果光标超出右边界
    if (visibleCursorX > availableWidth - rightMargin) {
        // 向左滚动，让光标距离右边界rightMargin
        m_scrollOffsetX = cursorX - (availableWidth - rightMargin);
    }
    
    // 如果光标超出左边界
    if (visibleCursorX < 0) {
        // 向右滚动，让光标回到左边
        m_scrollOffsetX = cursorX;
    }
    
    // 限制滚动范围：不能滚动到负数
    if (m_scrollOffsetX < 0) {
        m_scrollOffsetX = 0;
    }
}

// === 坐标与位置转换 ===
size_t UITextEdit::getPosFromX(float x) {
    float padding = 4.0f;
    x -= padding;

    if (x <= 0) return 0;
    if (m_text.empty()) return 0;

    // ✅ 使用真实的文本测量
    if (!m_renderer) {
        // 降级方案：使用固定字符宽度估算
        float charWidth = 8.0f;
        size_t pos = static_cast<size_t>(x / charWidth);
        return std::min(pos, m_text.length());
    }

    // 逐字符测量，找到最接近的位置
    float currentX = 0.0f;
    for (size_t i = 0; i < m_text.length(); ++i) {
        std::string substr = m_text.substr(0, i + 1);
        float w = 0.0f, h = 0.0f;
        if (m_renderer->measureText(substr, w, h, m_fontPx)) {
            if (w > x) {
                // 找到了超过x的位置，判断是i还是i+1更接近
                float prevW = currentX;
                float midPoint = (prevW + w) * 0.5f;
                return (x < midPoint) ? i : (i + 1);
            }
            currentX = w;
        }
    }

    return m_text.length();
}

float UITextEdit::getXFromPos(size_t pos) {
    if (pos == 0 || m_text.empty()) return 0.0f;

    // ✅ 使用真实的文本测量
    if (!m_renderer) {
        // 降级方案：使用固定字符宽度估算
        float charWidth = 8.0f;
        return pos * charWidth;
    }

    // 测量从开头到pos位置的文本宽度
    std::string substr = m_text.substr(0, std::min(pos, m_text.length()));
    float w = 0.0f, h = 0.0f;
    if (m_renderer->measureText(substr, w, h, m_fontPx)) {
        return w;
    }

    // 降级方案
    float charWidth = 8.0f;
    return pos * charWidth;
}

} // namespace Tina::UI
