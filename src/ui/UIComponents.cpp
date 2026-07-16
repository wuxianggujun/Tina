#include "UIComponents.hpp"
#include "UICore.hpp"
#include "UIContext.hpp"
#include "../engine/UIEvents.hpp"
#include "../engine/EventSystem.hpp"
#include "../engine/Application.hpp"
#include "../core/Log.hpp"
#include <algorithm>

namespace Tina::UI {
namespace {

const UIStyle* styleFor(const UINode& node, UIStyleRole role)
{
    const UIContext* context = node.uiContext();
    return context ? &context->theme().style(role) : nullptr;
}

Container::Optional<int> themedFontPx(const UINode& node,
                                      Container::Optional<int> explicitFont,
                                      const UIStyle* style)
{
    if (explicitFont.has_value() || !style) return explicitFont;
    const UIContext* context = node.uiContext();
    return context ? Container::Optional<int>(context->viewport().fontPx(style->fontSize))
                   : Container::Optional<int>(style->fontSize);
}

} // namespace

// === UIPanel 实现 ===

Tina::Core::Color UIPanel::getColor() const
{
    if (!m_colorOverride) {
        if (const UIStyle* style = styleFor(*this, UIStyleRole::Panel)) {
            return style->backgroundColor;
        }
    }
    return m_color;
}

void UIPanel::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    auto size = getSize();
    renderer.drawRect(viewId, pos.x, pos.y, size.x, size.y, getColor());
}

// === UILabel 实现 ===

Tina::Core::Color UILabel::getColor() const
{
    if (!m_colorOverride) {
        if (const UIStyle* style = styleFor(*this, UIStyleRole::Label)) {
            return style->textColor;
        }
    }
    return m_color;
}

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
    const UIStyle* style = styleFor(*this, UIStyleRole::Label);
    const Tina::Core::Color color = getColor();
    opts.r = color.r(); opts.g = color.g(); opts.b = color.b(); opts.a = color.a();
    opts.hAlign = hAlign; opts.vAlign = vAlign; opts.padX = 4.0f; opts.padY = 4.0f;
    opts.fontPx = themedFontPx(*this, m_fontPx, style);
    renderer.drawTextBox(viewId, pos.x, pos.y, size.x, size.y, m_text, opts);
}

Tina::Math::Vec2 UILabel::measureContent(float availableWidth, float /*availableHeight*/)
{
    (void)availableWidth;
    auto* app = Tina::Engine::Application::instance();
    if (!app) return getSize();

    float tw = 0.0f, th = 0.0f;
    auto& tr = app->textRenderer();
    int prev = tr.currentFontPx();
    bool needRestore = false;
    const auto resolvedFont = themedFontPx(*this, m_fontPx, styleFor(*this, UIStyleRole::Label));
    if (resolvedFont.has_value() && resolvedFont.value() > 0 && resolvedFont.value() != prev) {
        if (tr.setFontPx(resolvedFont.value())) needRestore = true;
    }
    tr.measureText(m_text, tw, th);
    if (needRestore) tr.setFontPx(prev);

    const float padX = 4.0f;
    const float padY = 4.0f;
    return {tw + padX * 2.0f, th + padY * 2.0f};
}

// === UIButton 实现 ===

void UIButton::onClick() {
    // 🛡️ 工程化改进1：禁用态早返回
    if (!isInteractable() || !isEnabled()) {
        TINA_DEBUG("按钮 '{}' 被禁用，忽略点击", getName());
        return;
    }
    
    // 🛡️ 工程化改进2：重入保护
    static bool isProcessing = false;
    if (isProcessing) {
        TINA_WARN("按钮 '{}' 重入保护：忽略递归点击", getName());
        return;
    }
    isProcessing = true;
    
    TINA_INFO("UIButton::onClick - 按钮 '{}' 被点击！", getName());
    
    // 🎯 正确的架构：事件优先，回调作为默认行为
    // 执行顺序契约：捕获 → 目标 → 冒泡 → （若未 defaultPrevented）本地回调
    
    // 步骤1：触发事件（总是触发，系统统一可见）
    if (eventSystem()) {
        Engine::ButtonClickEvent clickEvent(m_buttonId, getName().c_str());
        clickEvent.target = this;
        clickEvent.mouseX = eventSystem()->uiContext().mouseX;
        clickEvent.mouseY = eventSystem()->uiContext().mouseY;
        clickEvent.button = 0;  // 左键
        
        // 触发事件（支持捕获/目标/冒泡，可被拦截）
        TINA_DEBUG("按钮 '{}' - 触发事件（事件总线）", getName());
        eventSystem()->triggerUIEvent(clickEvent, this);
        
        // 步骤2：若未取消默认行为，执行本地回调
        if (!clickEvent.defaultPrevented && m_pendingClick) {
            TINA_DEBUG("按钮 '{}' - 执行默认行为（本地回调）", getName());
            m_pendingClick();
        } else if (clickEvent.defaultPrevented) {
            TINA_INFO("按钮 '{}' - 默认行为被取消", getName());
        }
    } else {
        // 降级：没有事件系统，直接执行本地回调
        TINA_WARN("按钮 '{}' - 没有事件系统，直接执行回调", getName());
        if (m_pendingClick) {
            m_pendingClick();
        }
    }
    
    isProcessing = false;
}

void UIButton::onRender(uint16_t viewId, UIRenderer& renderer)
{
    auto pos = getWorldPosition();
    auto size = getSize();

    // 根据状态选择背景色
    const UIStyle* style = styleFor(*this, UIStyleRole::Button);
    const Tina::Core::Color normalColor = !m_normalColorOverride && style
        ? style->backgroundColor : m_normalColor;
    const Tina::Core::Color hoverColor = !m_hoverColorOverride && style
        ? style->hoverColor : m_hoverColor;
    const Tina::Core::Color pressedColor = !m_pressedColorOverride && style
        ? style->activeColor : m_pressedColor;
    const Tina::Core::Color textColor = !m_textColorOverride && style
        ? (isEnabled() ? style->textColor : style->textDisabledColor)
        : m_textColor;
    const auto resolvedFont = themedFontPx(*this, m_fontPx, style);

    Tina::Core::Color bgColor = isEnabled() || !style ? normalColor : style->disabledColor;
    if (m_pressed && isEnabled()) {
        bgColor = pressedColor;
    } else if (m_hovered && isEnabled()) {
        bgColor = hoverColor;
    }

    // 绘制背景
    renderer.drawRect(viewId, pos.x, pos.y, size.x, size.y, bgColor);

    // 若被选中，绘制边框高亮（细边）
    if (m_selected || hasFocus()) {
        const float t = 2.0f; // 2px 边框
        const auto hl = style ? style->borderColor : Tina::UI::UIColors::SelectionHL;
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
        to.r = textColor.r(); to.g = textColor.g(); to.b = textColor.b(); to.a = textColor.a();
        to.hAlign = UIRenderer::AlignH::Center; to.vAlign = UIRenderer::AlignV::Center;
        to.fontPx = resolvedFont;
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
        to.r = textColor.r(); to.g = textColor.g(); to.b = textColor.b(); to.a = textColor.a();
        to.hAlign = UIRenderer::AlignH::Left; to.vAlign = UIRenderer::AlignV::Center;
        to.fontPx = resolvedFont;
        renderer.drawTextBox(viewId, tx, ty, twRect, thRect, m_text, to);
    } else {
        // 默认：无图标（或中心覆盖图标）+ 文本精确居中
        if (hasIcon) {
            float iw = std::max(8.0f, size.x - pad*2.0f);
            float ih = std::max(8.0f, size.y - pad*2.0f);
            float side = std::min(iw, ih);
            float ix = pos.x + (size.x - side) * 0.5f;
            float iy = pos.y + (size.y - side) * 0.5f;
            renderer.drawImage(viewId, ix, iy, side, side, m_iconTex, 1,1,1,0.95f);
        }

        // 使用 drawTextBox 的居中对齐（内部基于字形度量计算基线，垂直居中更准确）
        UIRenderer::TextOptions to{};
        to.r = textColor.r(); to.g = textColor.g(); to.b = textColor.b(); to.a = textColor.a();
        to.hAlign = UIRenderer::AlignH::Center;
        to.vAlign = UIRenderer::AlignV::Center;
        to.fontPx = resolvedFont;
        to.padX = 0.0f; to.padY = 0.0f;
        renderer.drawTextBox(viewId, pos.x, pos.y, size.x, size.y, m_text, to);
    }

    // 角标：右上等位置显示的小角标（如数字）
    if (!m_badgeText.empty()) {
        // 角标：固定高度（由字号决定）+ 自适应宽度，确保数字“大小一致”
        float tw=0.0f, th=0.0f;
        Container::Optional<int> badgePx = m_badgeFontPx.has_value() ? m_badgeFontPx : Container::Optional<int>(16);
        renderer.measureText(m_badgeText, tw, th, badgePx);
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
        bo.fontPx = badgePx; bo.hAlign = UIRenderer::AlignH::Center; bo.vAlign = UIRenderer::AlignV::Center;
        renderer.drawTextBox(viewId, bx, by, bw, bh, m_badgeText, bo);
    }
}

Tina::Math::Vec2 UIButton::measureContent(float availableWidth, float /*availableHeight*/)
{
    (void)availableWidth;
    auto* app = Tina::Engine::Application::instance();
    float tw = 0.0f, th = 0.0f;
    if (app) {
        auto& tr = app->textRenderer();
        int prev = tr.currentFontPx();
        bool needRestore = false;
        const auto resolvedFont = themedFontPx(*this, m_fontPx, styleFor(*this, UIStyleRole::Button));
        if (resolvedFont.has_value() && resolvedFont.value() > 0 && resolvedFont.value() != prev) {
            if (tr.setFontPx(resolvedFont.value())) needRestore = true;
        }
        tr.measureText(m_text, tw, th);
        if (needRestore) tr.setFontPx(prev);
    } else {
        tw = 60.0f; th = 20.0f;
    }

    const float pad = 8.0f;
    float minH = std::max(th + pad * 2.0f, 28.0f);
    float minW = std::max(tw + pad * 2.0f, 60.0f);

    if (bgfx::isValid(m_iconTex)) {
        float iconW = std::max(16.0f, minH - pad * 2.0f);
        switch (m_iconLayout) {
            case IconLayout::IconLeftTextRight:
                minW += iconW + pad;
                break;
            case IconLayout::IconTopTextBottom:
                minH = std::max(minH, iconW + th + pad * 3.0f);
                minW = std::max(minW, iconW + pad * 2.0f);
                break;
            case IconLayout::OverlapCenter:
            default:
                break;
        }
    }

    return {minW, minH};
}

} // namespace Tina::UI
