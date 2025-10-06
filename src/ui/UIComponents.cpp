#include "UIComponents.hpp"
#include "UICore.hpp"
#include <algorithm>

namespace Tina::UI {

// === UIPanel 实现 ===

void UIPanel::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    auto size = getSize();
    renderer.drawRect(viewId, pos.x, pos.y, size.x, size.y, m_color);
}

// === UILabel 实现 ===

void UILabel::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    // 文本默认从左上角绘制，可根据需要调整偏移
    renderer.drawText(viewId, pos.x + 4, pos.y + 4, m_color, m_text);
}

// === UIButton 实现 ===

void UIButton::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    auto size = getSize();

    // 根据状态选择背景色
    Tina::Core::Color bgColor = m_normalColor;
    if (m_pressed) {
        bgColor = m_pressedColor;
    } else if (m_hovered) {
        bgColor = m_hoverColor;
    }

    // 绘制背景
    renderer.drawRect(viewId, pos.x, pos.y, size.x, size.y, bgColor);

    // 若选中，绘制边框高亮（四边细矩形）
    if (m_selected) {
        const float t = 2.0f; // 2px 边框
        auto hl = Tina::UI::UIColors::SelectionHL;
        // 上
        renderer.drawRect(viewId, pos.x, pos.y, size.x, t, hl);
        // 下
        renderer.drawRect(viewId, pos.x, pos.y + size.y - t, size.x, t, hl);
        // 左
        renderer.drawRect(viewId, pos.x, pos.y, t, size.y, hl);
        // 右
        renderer.drawRect(viewId, pos.x + size.x - t, pos.y, t, size.y, hl);
    }

    // 绘制文本（居中对齐）
    renderer.drawTextEx(viewId, pos.x, pos.y, size.x, size.y,
                        m_textColor,
                        m_text,
                        UIRenderer::AlignH::Center,
                        UIRenderer::AlignV::Center,
                        0.0f, 0.0f);

    // 角标：用于显示数字小角标（可配置位置）
    if (!m_badgeText.empty()) {
        float tw=0.0f, th=0.0f;
        renderer.measureText(m_badgeText, tw, th);
        const float pad = 3.0f;
        float bw = std::max(tw * 0.85f + pad*2.0f, 16.0f);
        float bh = std::clamp(th * 0.60f + pad*1.0f, 14.0f, 22.0f);
        float bx = pos.x + size.x - bw - 4.0f;
        float by = pos.y + 4.0f;
        switch (m_badgeCorner) {
            case BadgeCorner::TopLeft:
                bx = pos.x + 4.0f; by = pos.y + 4.0f; break;
            case BadgeCorner::TopRight:
                bx = pos.x + size.x - bw - 4.0f; by = pos.y + 4.0f; break;
            case BadgeCorner::BottomLeft:
                bx = pos.x + 4.0f; by = pos.y + size.y - bh - 4.0f; break;
            case BadgeCorner::BottomRight:
                bx = pos.x + size.x - bw - 4.0f; by = pos.y + size.y - bh - 4.0f; break;
        }
        // 背景与描边
        renderer.drawRect(viewId, bx, by, bw, bh, m_badgeBgColor);
        auto bhc = Tina::UI::UIColors::BadgeHighlight;
        renderer.drawRect(viewId, bx, by, bw, 1.0f, bhc);
        renderer.drawRect(viewId, bx, by+bh-1.0f, bw, 1.0f, bhc);
        renderer.drawRect(viewId, bx, by, 1.0f, bh, bhc);
        renderer.drawRect(viewId, bx+bw-1.0f, by, 1.0f, bh, bhc);
        // 文本
        renderer.drawTextEx(viewId, bx, by, bw, bh,
                            m_badgeTextColor,
                            m_badgeText,
                            UIRenderer::AlignH::Center,
                            UIRenderer::AlignV::Center,
                            0.0f, 0.0f);
    }
}

} // namespace Tina::UI


