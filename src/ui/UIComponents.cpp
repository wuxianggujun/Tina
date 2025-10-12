#include "UIComponents.hpp"
#include "UICore.hpp"
#include "UIEventDispatcher.hpp"
#include "../core/Log.hpp"
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
    auto size = getSize();

    // 将 UILabel 内部的对齐枚举映射为 UIRenderer 的对齐枚举
    UIRenderer::AlignH hAlign;
    switch (m_alignH) {
        case TextAlignH::Center: hAlign = UIRenderer::AlignH::Center; break;
        case TextAlignH::Right:  hAlign = UIRenderer::AlignH::Right;  break;
        case TextAlignH::Left:
        default:                 hAlign = UIRenderer::AlignH::Left;   break;
    }
    UIRenderer::AlignV vAlign;
    switch (m_alignV) {
        case TextAlignV::Center:   vAlign = UIRenderer::AlignV::Center;   break;
        case TextAlignV::Bottom:   vAlign = UIRenderer::AlignV::Bottom;   break;
        case TextAlignV::Baseline: vAlign = UIRenderer::AlignV::Baseline; break;
        case TextAlignV::Top:
        default:                   vAlign = UIRenderer::AlignV::Top;      break;
    }

    // 使用统一文本接口，在节点矩形内绘制；默认提供 4px 内边距
    UIRenderer::TextOptions opts{};
    opts.r = m_color.r(); opts.g = m_color.g(); opts.b = m_color.b(); opts.a = m_color.a();
    opts.hAlign = hAlign; opts.vAlign = vAlign; opts.padX = 4.0f; opts.padY = 4.0f;
    if (m_fontPx > 0) opts.fontPx = m_fontPx;
    renderer.drawTextBox(viewId, pos.x, pos.y, size.x, size.y, m_text, opts);
}

// === UIButton 实现 ===

void UIButton::onClick() {
    // 优先通过事件系统触发（支持冒泡、捕获、preventDefault）
    auto* dispatcher = getEventDispatcher();
    if (dispatcher) {
        // 创建点击事件并分发
        UIClickEvent event(UIEvent::Type::Click, this, 0, 0);
        event.setBubbles(true);  // 支持冒泡
        dispatcher->dispatchEvent(event);

        // 如果事件被阻止，不执行回调
        if (event.isDefaultPrevented()) {
            return;
        }
    }

    // 后备方案：如果没有事件系统或事件未阻止，执行回调
    if (m_clickCallback) {
        m_clickCallback();
    }
}

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

    // 若被选中，绘制边框高亮（细边）
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

    const float pad = 6.0f;
    bool hasIcon = bgfx::isValid(m_iconTex);
    if (hasIcon && m_iconLayout == IconLayout::IconTopTextBottom) {
        // 图标在上，文本在下
        float iconSide = std::min(std::max(8.0f, size.x - pad*2.0f), std::max(8.0f, size.y*0.6f - pad*1.0f));
        float ix = pos.x + (size.x - iconSide) * 0.5f;
        float iy = pos.y + pad;
        renderer.drawImage(viewId, ix, iy, iconSide, iconSide, m_iconTex, 1,1,1,0.95f);

        // 文本区域：底部剩余空间
        float tx = pos.x + pad;
        float ty = iy + iconSide + 2.0f;
        float twRect = size.x - pad*2.0f;
        float thRect = std::max(0.0f, pos.y + size.y - ty - pad);
        UIRenderer::TextOptions to{};
        to.r = m_textColor.r(); to.g = m_textColor.g(); to.b = m_textColor.b(); to.a = m_textColor.a();
        to.hAlign = UIRenderer::AlignH::Center; to.vAlign = UIRenderer::AlignV::Center;
        if (m_fontPx > 0) to.fontPx = m_fontPx;
        renderer.drawTextBox(viewId, tx, ty, twRect, thRect, m_text, to);
    } else if (hasIcon && m_iconLayout == IconLayout::IconLeftTextRight) {
        // 图标在左，文本在右
        float iconSide = std::min(size.y - pad*2.0f, size.x * 0.45f);
        iconSide = std::max(iconSide, 8.0f);
        float ix = pos.x + pad;
        float iy = pos.y + (size.y - iconSide) * 0.5f;
        renderer.drawImage(viewId, ix, iy, iconSide, iconSide, m_iconTex, 1,1,1,0.95f);

        float tx = ix + iconSide + pad;
        float ty = pos.y + pad;
        float twRect = std::max(0.0f, pos.x + size.x - pad - tx);
        float thRect = size.y - pad*2.0f;
        UIRenderer::TextOptions to{};
        to.r = m_textColor.r(); to.g = m_textColor.g(); to.b = m_textColor.b(); to.a = m_textColor.a();
        to.hAlign = UIRenderer::AlignH::Left; to.vAlign = UIRenderer::AlignV::Center;
        if (m_fontPx > 0) to.fontPx = m_fontPx;
        renderer.drawTextBox(viewId, tx, ty, twRect, thRect, m_text, to);
    } else {
        // 默认：无图标（或中心覆盖图标）+ 文本居中
        if (hasIcon) {
            float iw = std::max(8.0f, size.x - pad*2.0f);
            float ih = std::max(8.0f, size.y - pad*2.0f);
            float side = std::min(iw, ih);
            float ix = pos.x + (size.x - side) * 0.5f;
            float iy = pos.y + (size.y - side) * 0.5f;
            renderer.drawImage(viewId, ix, iy, side, side, m_iconTex, 1,1,1,0.95f);
        }
        UIRenderer::TextOptions to{};
        to.r = m_textColor.r(); to.g = m_textColor.g(); to.b = m_textColor.b(); to.a = m_textColor.a();
        to.hAlign = UIRenderer::AlignH::Center; to.vAlign = UIRenderer::AlignV::Center;
        to.fontPx = m_fontPx > 0 ? m_fontPx : 0;
        renderer.drawTextBox(viewId, pos.x, pos.y, size.x, size.y, m_text, to);
    }

    // 角标：右上等位置显示的小角标（如数字）
    if (!m_badgeText.empty()) {
        // 角标：固定高度（由字号决定）+ 自适应宽度，确保数字“大小一致”
        float tw=0.0f, th=0.0f;
        int px = (m_badgeFontPx > 0) ? m_badgeFontPx : 16;
        renderer.measureText(m_badgeText, tw, th, px);
        const float badgePad = 3.0f;
        float bh = th + badgePad*2.0f;           // 高度只取决于字号
        float bw = std::max(tw + badgePad*2.0f,  // 宽度根据内容
                            bh);                  // 至少为正圆
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
        // 角标背景+描边
        renderer.drawRect(viewId, bx, by, bw, bh, m_badgeBgColor);
        auto bhc = Tina::UI::UIColors::BadgeHighlight;
        renderer.drawRect(viewId, bx, by, bw, 1.0f, bhc);
        renderer.drawRect(viewId, bx, by+bh-1.0f, bw, 1.0f, bhc);
        renderer.drawRect(viewId, bx, by, 1.0f, bh, bhc);
        renderer.drawRect(viewId, bx+bw-1.0f, by, 1.0f, bh, bhc);
        // 角标文本
        UIRenderer::TextOptions bo{};
        bo.r = m_badgeTextColor.r(); bo.g = m_badgeTextColor.g(); bo.b = m_badgeTextColor.b(); bo.a = m_badgeTextColor.a();
        bo.fontPx = px; bo.hAlign = UIRenderer::AlignH::Center; bo.vAlign = UIRenderer::AlignV::Center;
        renderer.drawTextBox(viewId, bx, by, bw, bh, m_badgeText, bo);
    }
}

} // namespace Tina::UI
