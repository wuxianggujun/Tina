#pragma once

#include "tina/ui/UIWidget.hpp"
#include <string>
#include <vector>
#include <glm/vec4.hpp>

#include "tina/event/Event.hpp"

namespace Tina {

/**
 * @brief 文本视图组件，用于显示和编辑文本
 */
class TINA_CORE_API TextView : public UIWidget {
public:
    /**
     * @brief 构造函数
     * @param name 组件名称
     */
    explicit TextView(const std::string& name = "TextView");
    
    /**
     * @brief 设置文本内容
     * @param text 要设置的文本
     */
    void setText(const std::string& text);
    
    /**
     * @brief 获取文本内容
     * @return 当前文本内容
     */
    const std::string& getText() const { return m_text; }
    
    /**
     * @brief 设置文本选择范围
     * @param start 选择起始位置
     * @param end 选择结束位置
     */
    void setSelection(size_t start, size_t end);
    
    /**
     * @brief 清除文本选择
     */
    void clearSelection();
    
    /**
     * @brief 检查是否有文本被选中
     * @return 是否有选中的文本
     */
    bool hasSelection() const { return m_selectionStart != m_selectionEnd; }
    
    /**
     * @brief 设置是否可编辑
     * @param editable 是否可编辑
     */
    void setEditable(bool editable) { m_editable = editable; }
    
    /**
     * @brief 检查是否可编辑
     * @return 是否可编辑
     */
    bool isEditable() const { return m_editable; }
    
    /**
     * @brief 设置是否支持多行
     * @param multiLine 是否支持多行
     */
    void setMultiLine(bool multiLine) { m_multiLine = multiLine; updateTextLayout(); }
    
    /**
     * @brief 检查是否支持多行
     * @return 是否支持多行
     */
    bool isMultiLine() const { return m_multiLine; }
    
    /**
     * @brief 设置字体大小
     * @param fontSize 字体大小
     */
    void setFontSize(float fontSize) { m_fontSize = fontSize; updateTextLayout(); }
    
    /**
     * @brief 获取字体大小
     * @return 字体大小
     */
    float getFontSize() const { return m_fontSize; }
    
    /**
     * @brief 设置文本颜色
     * @param color 文本颜色
     */
    void setTextColor(const glm::vec4& color) { m_textColor = color; }
    
    /**
     * @brief 获取文本颜色
     * @return 文本颜色
     */
    const glm::vec4& getTextColor() const { return m_textColor; }
    
    // UIWidget接口实现
    void draw() override;
    bool onMouseButton(Event::MouseButton button, bool pressed, float x, float y) override;
    bool onKeyboard(Event::KeyCode key, bool pressed) override;
    bool onChar(unsigned int codepoint) override;
    bool onFocus(bool focused) override;

private:
    /**
     * @brief 文本行信息
     */
    struct TextLine {
        std::string text;  ///< 行文本
        float width = 0;   ///< 行宽度
        float height = 0;  ///< 行高度
    };
    
    /**
     * @brief 插入文本
     * @param text 要插入的文本
     */
    void insertText(const std::string& text);
    
    /**
     * @brief 删除选中的文本
     */
    void deleteSelection();
    
    /**
     * @brief 更新文本布局
     */
    void updateTextLayout();
    
    /**
     * @brief 更新光标位置
     */
    void updateCursorPosition();
    
    /**
     * @brief 获取指定位置对应的字符索引
     * @param x x坐标
     * @param y y坐标
     * @return 字符索引
     */
    size_t getCharIndexAtPosition(float x, float y) const;
    
    /**
     * @brief 获取指定字符索引对应的位置
     * @param index 字符索引
     * @return 位置坐标
     */
    glm::vec2 getPositionForCharIndex(size_t index) const;
    
    /**
     * @brief 确保光标可见
     */
    void ensureCursorVisible();
    
    std::string m_text;                   ///< 文本内容
    std::vector<TextLine> m_lines;        ///< 文本行信息
    size_t m_cursorPosition = 0;          ///< 光标位置
    size_t m_selectionStart = 0;          ///< 选择起始位置
    size_t m_selectionEnd = 0;            ///< 选择结束位置
    bool m_editable = true;               ///< 是否可编辑
    bool m_multiLine = false;             ///< 是否支持多行
    bool m_cursorVisible = true;          ///< 光标是否可见
    float m_cursorBlinkTime = 0;          ///< 光标闪烁计时器
    float m_fontSize = 16.0f;             ///< 字体大小
    glm::vec4 m_textColor{1.0f};         ///< 文本颜色
    float m_scrollX = 0;                  ///< 水平滚动偏移
    float m_scrollY = 0;                  ///< 垂直滚动偏移
};

} // namespace Tina 
