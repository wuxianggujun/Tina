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

    // 绘制文本（左对齐，垂直居中，带左边距）
    // 由于没有文本宽度测量 API，使用左对齐 + padding
    float tw = 0.0f, th = 0.0f;
    renderer.measureText(m_text, tw, th);
    float textX = pos.x + (size.x - tw) * 0.5f;
    float textY = pos.y + (size.y - th) * 0.5f;
    renderer.drawText(viewId, textX, textY,
                      m_textColor.x, m_textColor.y, m_textColor.z, m_textColor.w,
                      m_text);
}

} // namespace Tina::UI
