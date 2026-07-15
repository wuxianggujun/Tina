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

// ============================================================================
// UTF-8 ↔ UTF-32 转换辅助函数（使用utfcpp库）
// 
// TODO: 未来支持复杂Emoji（字形簇）
// - 当前实现将每个Unicode code point视为一个字符
// - 例如：👨‍👩‍👧‍👦 (家庭Emoji) 会被当成4个字符处理
// - 如需正确支持，考虑引入libgrapheme库进行字形簇分割
// - 参考：https://github.com/libjpeg-turbo/libgrapheme
// ============================================================================

// UTF-8 → UTF-32字符数组
static Container::Vector<char32_t> utf8ToChars(const Container::String& utf8Str) {
    Container::Vector<char32_t> result;
    if (utf8Str.empty()) return result;
    
    try {
        utf8::utf8to32(utf8Str.begin(), utf8Str.end(), eastl::back_inserter(result));
    } catch (const utf8::exception& e) {
        TINA_ERROR("UTF-8 decode error: {}", e.what());
        result.clear();
    }
    return result;
}

// UTF-32字符数组 → UTF-8
static Container::String charsToUTF8(const Container::Vector<char32_t>& chars) {
    Container::String result;
    if (chars.empty()) return result;
    
    try {
        utf8::utf32to8(chars.begin(), chars.end(), eastl::back_inserter(result));
    } catch (const utf8::exception& e) {
        TINA_ERROR("UTF-32 encode error: {}", e.what());
        result.clear();
    }
    return result;
}

// ============================================================================
// 公共API
// ============================================================================

// === 设置文本（从UTF-8） ===
void UITextEdit::setText(const std::string& text) {
    // ✅ 转换为UTF-32字符数组
    m_chars = utf8ToChars(Container::String(text.c_str()));
    
    // 应用字符数限制
    if (m_maxLength > 0 && m_chars.size() > m_maxLength) {
        m_chars.resize(m_maxLength);
        TINA_WARN("UITextEdit::setText - Text truncated to {} chars", m_maxLength);
    }
    
    // 更新UTF-8缓存
    m_utf8Cache = text;
    m_utf8Dirty = false;
    
    // 调整光标位置
    if (m_cursorPos > m_chars.size()) {
        m_cursorPos = m_chars.size();
    }
    
    // 清除选择
    clearSelection();
}

// === 获取文本（转为UTF-8） ===
std::string UITextEdit::getText() const {
    if (m_utf8Dirty) {
        // 延迟转换：只在需要时才转换
        Container::String eastlStr = charsToUTF8(m_chars);
        m_utf8Cache = std::string(eastlStr.c_str());
        m_utf8Dirty = false;
    }
    return m_utf8Cache;
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
        std::string src = m_chars.empty() ? std::string(m_placeholder.c_str()) : getText();
        tr.measureText(src, tw, th);
        if (needRestore) tr.setFontPx(prev);
    } else {
        // 无法测量时使用保守估计
        tw = std::max(100.0f, (float)m_chars.size() * 8.0f);
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
    m_cursorPos = std::min(pos, m_chars.size());
    m_cursorBlinkTime = 0.0f;  // 重置闪烁
    clearSelection();
    ensureCursorVisible();  // ✅ 自动滚动
}

void UITextEdit::moveCursor(int delta) {
    if (delta == 0) return;
    
    // ✅ 超简单的加减法！不再需要utf8::prior/next！
    int newPos = static_cast<int>(m_cursorPos) + delta;
    m_cursorPos = static_cast<size_t>(Container::Max(0, Container::Min(newPos, static_cast<int>(m_chars.size()))));
    
    m_cursorBlinkTime = 0.0f;
    ensureCursorVisible();
}

// === 选择操作 ===
void UITextEdit::selectAll() {
    if (m_chars.empty()) return;
    m_selectionStart = 0;
    m_selectionEnd = static_cast<int>(m_chars.size());
    m_cursorPos = m_chars.size();
}

void UITextEdit::clearSelection() {
    m_selectionStart = -1;
    m_selectionEnd = -1;
}

std::string UITextEdit::getSelectedText() const {
    if (!hasSelection()) return "";

    int start = std::min(m_selectionStart, m_selectionEnd);
    int end = std::max(m_selectionStart, m_selectionEnd);

    // 返回选中的UTF-8文本
    Container::Vector<char32_t> selectedChars(m_chars.begin() + start, m_chars.begin() + end);
    Container::String result = charsToUTF8(selectedChars);
    return result.c_str();
}

void UITextEdit::deleteSelection() {
    if (!hasSelection()) return;

    int start = std::min(m_selectionStart, m_selectionEnd);
    int end = std::max(m_selectionStart, m_selectionEnd);

    m_chars.erase(m_chars.begin() + start, m_chars.begin() + end);
    m_utf8Dirty = true;
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

// === 布局回调（大小变化时由UI系统自动调用）===
void UITextEdit::onLayout() {
    UINode::onLayout();  // 调用父类
    
    // ✅ 大小改变了，重新计算滚动偏移确保光标可见
    if (m_renderer) {
        ensureCursorVisible();
    }
}

// === 渲染 ===
void UITextEdit::onRender(uint16_t viewId, UIRenderer& renderer) {
    // 缓存renderer指针用于文本测量
    m_renderer = &renderer;
    
    auto worldPos = getWorldPosition();
    auto size = getSize();

    // 1. 绘制背景
    renderer.drawRect(viewId, worldPos.x, worldPos.y, size.x, size.y, m_bgColor);
    
    // ✅ 2. 设置裁剪矩形（防止文本超出输入框）
    float padding = 4.0f;
    renderer.pushClip(
        worldPos.x + padding, 
        worldPos.y + padding,
        size.x - padding * 2, 
        size.y - padding * 2
    );

    // 2. 绘制选择高亮（✅ 应用水平滚动）
    if (hasSelection()) {
        int selStart = std::min(m_selectionStart, m_selectionEnd);
        int selEnd = std::max(m_selectionStart, m_selectionEnd);

        float startX = getXFromPos(selStart);
        float endX = getXFromPos(selEnd);

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

    if (m_chars.empty() && !m_focused) {
        // 显示占位符（无滚动）
        opts.r = m_placeholderColor.r();
        opts.g = m_placeholderColor.g();
        opts.b = m_placeholderColor.b();
        opts.a = m_placeholderColor.a();
        std::string placeholderStr(m_placeholder.c_str());
        renderer.drawText(viewId, worldPos.x + padding, worldPos.y + padding, placeholderStr, opts);
    } else {
        // 显示实际文本（✅ 应用滚动偏移）
        renderer.drawText(viewId, worldPos.x + padding - m_scrollOffsetX, worldPos.y + padding, getText(), opts);
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
    
    // ✅ 移除裁剪矩形
    renderer.popClip();
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
            TINA_INFO("UITextEdit: Delete pressed, cursorPos={}, textLength={}", m_cursorPos, m_chars.size());
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
                if (m_cursorPos < m_chars.size()) {
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
            m_cursorPos = m_chars.size();
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

// === 插入文本（从UTF-8） ===
void UITextEdit::insertText(const std::string& text) {
    if (text.empty()) return;
    
    // 删除选中内容
    if (hasSelection()) {
        deleteSelection();
    }
    
    // ✅ 转换为UTF-32字符
    Container::Vector<char32_t> inputChars = utf8ToChars(Container::String(text.c_str()));
    if (inputChars.empty()) {
        TINA_WARN("UITextEdit::insertText - Invalid UTF-8, rejected");
        return;
    }
    
    // 检查字符数限制
    if (m_maxLength > 0) {
        size_t currentCount = m_chars.size();
        if (currentCount >= m_maxLength) {
            TINA_WARN("UITextEdit::insertText - Max char limit reached ({}/{})", 
                      currentCount, m_maxLength);
            return;
        }
        
        // 计算可插入的字符数
        size_t available = m_maxLength - currentCount;
        if (inputChars.size() > available) {
            inputChars.resize(available);  // ✅ 简单截断！
            TINA_INFO("UITextEdit::insertText - Truncated to {} chars", available);
        }
    }
    
    // ✅ 插入字符（超简单！）
    m_chars.insert(m_chars.begin() + m_cursorPos, inputChars.begin(), inputChars.end());
    m_cursorPos += inputChars.size();
    
    // 标记缓存失效
    m_utf8Dirty = true;
    m_cursorBlinkTime = 0.0f;
    ensureCursorVisible();
    
    TINA_INFO("UITextEdit::insertText - Inserted {} chars, total={}/{}", 
              inputChars.size(), m_chars.size(), m_maxLength > 0 ? m_maxLength : 999);
}

// === 删除字符（超简单！不再需要UTF-8判断） ===
void UITextEdit::deleteChar(bool forward) {
    if (m_chars.empty()) return;
    
    if (forward) {
        // Delete：删除光标后的字符
        if (m_cursorPos < m_chars.size()) {
            m_chars.erase(m_chars.begin() + m_cursorPos);  // ✅ 一行搞定！
            TINA_INFO("UITextEdit::deleteChar - Deleted forward at pos {}", m_cursorPos);
        }
    } else {
        // Backspace：删除光标前的字符
        if (m_cursorPos > 0) {
            m_chars.erase(m_chars.begin() + m_cursorPos - 1);  // ✅ 一行搞定！
            m_cursorPos--;
            TINA_INFO("UITextEdit::deleteChar - Backspace at pos {}", m_cursorPos);
        }
    }
    
    // 标记缓存失效
    m_utf8Dirty = true;
    m_cursorBlinkTime = 0.0f;
    ensureCursorVisible();
}

// === 确保光标可见（自动滚动）===
void UITextEdit::ensureCursorVisible() {
    if (!m_renderer) return;
    
    float padding = 4.0f;
    float availableWidth = getSize().x - padding * 2;
    float cursorX = getXFromPos(m_cursorPos);
    
    // ✅ 计算整个文本的宽度
    float textWidth = getXFromPos(m_chars.size());
    
    // ✅ 优化：如果文本完全能放下，重置滚动为0（窗口变大时）
    if (textWidth <= availableWidth) {
        m_scrollOffsetX = 0;
        return;
    }
    
    // 光标相对于可视区域的位置
    float visibleCursorX = cursorX - m_scrollOffsetX;
    
    // 右边距：光标需要距离右边界至少20px
    float rightMargin = 20.0f;
    
    // 如果光标超出右边界
    if (visibleCursorX > availableWidth - rightMargin) {
        // 向左滚动，让光标距离右边界rightMargin
        m_scrollOffsetX = cursorX - (availableWidth - rightMargin);
    }
    
    // 左边距：光标需要距离左边界至少20px
    float leftMargin = 20.0f;
    
    // 如果光标超出左边界（带左边距）
    if (visibleCursorX < leftMargin) {
        // 向右滚动，让光标距离左边界leftMargin
        m_scrollOffsetX = Container::Max(0.0f, cursorX - leftMargin);
    }
    
    // 限制滚动范围：不能滚动到负数
    if (m_scrollOffsetX < 0) {
        m_scrollOffsetX = 0;
    }
    
    // ✅ 限制滚动范围：不要滚动超过必要的距离（避免右侧出现空白）
    float maxScroll = Container::Max(0.0f, textWidth - availableWidth + 20.0f);
    if (m_scrollOffsetX > maxScroll) {
        m_scrollOffsetX = maxScroll;
    }
}

// === 坐标与位置转换 ===
size_t UITextEdit::getPosFromX(float x) {
    float padding = 4.0f;
    x -= padding;

    if (x <= 0) return 0;
    if (m_chars.empty()) return 0;

    // ✅ 使用真实的文本测量
    if (!m_renderer) {
        // 降级方案：使用固定字符宽度估算
        float charWidth = 8.0f;
        size_t pos = static_cast<size_t>(x / charWidth);
        return std::min(pos, m_chars.size());
    }

    // ✅ 逐字符测量（使用UTF-32索引）
    float currentX = 0.0f;
    for (size_t i = 0; i < m_chars.size(); ++i) {
        // 将前i+1个UTF-32字符转为UTF-8测量
        Container::Vector<char32_t> subChars(m_chars.begin(), m_chars.begin() + i + 1);
        Container::String substr = charsToUTF8(subChars);
        
        float w = 0.0f, h = 0.0f;
        if (m_renderer->measureText(substr.c_str(), w, h, m_fontPx)) {
            if (w > x) {
                // 找到了超过x的位置，判断是i还是i+1更接近
                float prevW = currentX;
                float midPoint = (prevW + w) * 0.5f;
                return (x < midPoint) ? i : (i + 1);
            }
            currentX = w;
        }
    }

    return m_chars.size();
}

float UITextEdit::getXFromPos(size_t pos) {
    if (pos == 0 || m_chars.empty()) return 0.0f;

    // ✅ 使用真实的文本测量
    if (!m_renderer) {
        // 降级方案：使用固定字符宽度估算
        float charWidth = 8.0f;
        return pos * charWidth;
    }

    // ✅ 测量从开头到pos位置（UTF-32字符索引）
    size_t charCount = std::min(pos, m_chars.size());
    Container::Vector<char32_t> subChars(m_chars.begin(), m_chars.begin() + charCount);
    Container::String substr = charsToUTF8(subChars);
    
    float w = 0.0f, h = 0.0f;
    if (m_renderer->measureText(substr.c_str(), w, h, m_fontPx)) {
        return w;
    }

    // 降级方案
    float charWidth = 8.0f;
    return pos * charWidth;
}

} // namespace Tina::UI
