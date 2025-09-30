#include "UIComponents.hpp"
#include "UICore.hpp"

namespace Tina::UI {

// === UIPanel 实现 ===

void UIPanel::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    auto size = getSize();
    renderer.drawRect(viewId, pos.x, pos.y, size.x, size.y,
                      m_color.x, m_color.y, m_color.z, m_color.w);
}

// === UILabel 实现 ===

void UILabel::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    // 文本默认从左上角绘制，可根据需要调整偏移
    renderer.drawText(viewId, pos.x + 4, pos.y + 4,
                      m_color.x, m_color.y, m_color.z, m_color.w,
                      m_text);
}

// === UIButton 实现 ===

void UIButton::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    auto size = getSize();

    // 根据状态选择背景色
    Tina::Math::Vec4 bgColor = m_normalColor;
    if (m_pressed) {
        bgColor = m_pressedColor;
    } else if (m_hovered) {
        bgColor = m_hoverColor;
    }

    // 绘制背景
    renderer.drawRect(viewId, pos.x, pos.y, size.x, size.y,
                      bgColor.x, bgColor.y, bgColor.z, bgColor.w);

    // 绘制文本（居中）
    // 简单实现：文本从中心偏移绘制（实际应根据文本宽度计算）
    float textX = pos.x + size.x * 0.5f - 20.0f; // 粗略居中
    float textY = pos.y + size.y * 0.5f - 8.0f;
    renderer.drawText(viewId, textX, textY,
                      m_textColor.x, m_textColor.y, m_textColor.z, m_textColor.w,
                      m_text);
}

} // namespace Tina::UI