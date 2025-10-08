#include "UIToolbar.hpp"
#include "UIColors.hpp"
#include "../core/Log.hpp"
#include <algorithm>

namespace Tina::UI {

using namespace Tina::UI::UIColors;

bool UIToolbar::initialize(int screenW, int screenH, UIRenderer& renderer, TextRenderer* text)
{
    m_screenW = screenW; m_screenH = screenH;
    m_renderer = &renderer; m_text = text;

    // 根节点
    m_root = new UINode("UIRoot");
    m_root->setSize((float)m_screenW, (float)m_screenH);

    // 工具栏背景
    m_bar = new UIPanel("Toolbar");
    m_bar->setAnchor(Anchor::TopLeft);
    m_bar->setPosition(0, 0);
    m_bar->setColor(ToolbarBg);  // 半透明深色背景
    m_root->addChild(m_bar);

    // 水平栈：用于排列按钮
    m_stack = new UIHStack("ToolbarStack");
    m_stack->setAnchor(Anchor::TopLeft);
    m_stack->setPadding((float)m_padding, (float)m_padding);
    m_stack->setSpacing((float)m_gap);
    m_stack->setCrossAlign(CrossAlign::Center);
    m_bar->addChild(m_stack);

    // 初次布局
    buildLayout();

    // 事件系统根
    m_events.setRoot(m_root);
    return true;
}

void UIToolbar::shutdown()
{
    if (m_root) { delete m_root; m_root = nullptr; }
    m_bar = nullptr; m_slots.clear();
}

void UIToolbar::onResize(int screenW, int screenH)
{
    m_screenW = screenW; m_screenH = screenH;
    if (m_root) m_root->setSize((float)m_screenW, (float)m_screenH);
    buildLayout();
}

void UIToolbar::update(float dt)
{
    if (m_root) m_root->update(dt);
}

void UIToolbar::render(uint16_t viewId)
{
    if (m_root && m_renderer) {
        m_root->render(viewId, *m_renderer);
        // 绘制 Tooltip（悬停提示）
        if (m_tipVisible && !m_tipText.empty()) {
            float tw=0.0f, th=0.0f;
            m_renderer->measureText(m_tipText, tw, th);
            const float pad = 6.0f;
            float w = tw + pad*2.0f;
            float h = th + pad*2.0f;
            // 默认在鼠标右下方显示，避免出屏幕
            float x = m_mouseX + 12.0f;
            float y = m_mouseY + 12.0f;
            float maxX = (float)m_screenW - w - 2.0f;
            float maxY = (float)m_screenH - h - 2.0f;
            if (x > maxX) x = std::max(2.0f, m_mouseX - w - 12.0f);
            if (y > maxY) y = std::max(2.0f, m_mouseY - h - 12.0f);
            // 背景与边框
            m_renderer->drawRect(viewId, x, y, w, h, 0.08f, 0.08f, 0.10f, 0.95f);
            m_renderer->drawRect(viewId, x, y, w, 1.0f, 0.9f, 0.85f, 0.35f, 1.0f);
            m_renderer->drawRect(viewId, x, y+h-1.0f, w, 1.0f, 0.9f, 0.85f, 0.35f, 1.0f);
            m_renderer->drawRect(viewId, x, y, 1.0f, h, 0.9f, 0.85f, 0.35f, 1.0f);
            m_renderer->drawRect(viewId, x+w-1.0f, y, 1.0f, h, 0.9f, 0.85f, 0.35f, 1.0f);
            // 文本
            m_renderer->drawTextEx(viewId, x, y, w, h, 1,1,1,1, m_tipText,
                                   UIRenderer::AlignH::Center, UIRenderer::AlignV::Center, pad, pad);
        }
    }
}

void UIToolbar::setSlotIcon(int index, bgfx::TextureHandle tex)
{
    if (index < 0 || index >= (int)m_slots.size()) return;
    if (auto* btn = m_slots[index]) {
        btn->setIconTexture(tex);
    }
}

void UIToolbar::buildLayout()
{
    if (!m_bar) return;

    // 清理旧按钮
    for (auto* btn : m_slots) {
        if (m_stack) m_stack->removeChild(btn);
        delete btn;
    }
    m_slots.clear();

    // 可容纳按钮数（不超过 m_slotCount），并据此“以内容宽度”为准，居中整个工具栏
    int avail = std::max(0, m_screenW - m_padding * 2);
    int per = m_slotSize + m_gap;
    int maxSlots = per > 0 ? std::max(1, (avail + m_gap) / per) : m_slotCount;
    int count = std::min(m_slotCount, maxSlots);

    int contentW = count > 0 ? (count * m_slotSize + (count - 1) * m_gap) : 0;
    int barW = contentW + m_padding * 2;
    if (barW > m_screenW) barW = m_screenW; // 兜底
    float barX = (float)((m_screenW - barW) / 2);
    // 根据按钮尺寸与内边距，动态放大工具栏高度（避免角标与文本拥挤）
    m_barH = std::max(m_barH, m_slotSize + m_padding * 2);
    m_bar->setSize((float)barW, (float)m_barH);
    m_bar->setPosition(barX, 0.0f);
    if (m_stack) m_stack->setSize((float)barW, (float)m_barH);

    // 创建按钮并布局
    for (int i = 0; i < count; ++i) {
        auto* btn = new UIButton("ToolSlot");
        btn->setSize((float)m_slotSize, (float)m_slotSize);
        btn->setAnchor(Anchor::TopLeft);
        // 默认布局：左图标 + 右文本（避免重叠）
        btn->setIconLayout(UIButton::IconLayout::IconLeftTextRight);

        // 中央主标识：前三个为“水/挖/爆”，其余占位
        std::string center;
        if (i == 0) center = "水";
        else if (i == 1) center = "清";
        else if (i == 2) center = "爆";
        else center = "";
        btn->setText(center);
        // 角标：显示数字编号（放在右下角，避免遮挡中央文字）
        btn->setBadgeText(std::to_string(i + 1));
        btn->setBadgeCorner(BadgeCorner::BottomRight);

        // 点击：切换选中（使用 Signal）
        btn->onClick.connect([this, i]() { this->select(i); });

        // 悬停：显示/隐藏 tooltip（使用 Signal）
        btn->onHoverEnter.connect([this, i]() {
            m_tipVisible = true;
            const char* name = (i==0?"注水": (i==1?"清除": (i==2?"爆炸":"工具")));
            m_tipText = std::string("工具 ") + std::to_string(i+1) + "：" + name;
        });
        btn->onHoverLeave.connect([this]() {
            m_tipVisible = false;
            m_tipText.clear();
        });

        if (m_stack) m_stack->addChild(btn);
        m_slots.push_back(btn);
    }

    // 应用当前选中高亮；若当前无选择则默认选中第一个
    if (m_selected < 0 && !m_slots.empty()) m_selected = 0;
    select(m_selected >= 0 && m_selected < (int)m_slots.size() ? m_selected : -1);
}

bool UIToolbar::hitTest(float x, float y) const
{
    if (!m_bar) return false;
    // 使用 UINode 自带的 containsPoint（基于世界坐标）
    return const_cast<UIPanel*>(m_bar)->containsPoint(x, y);
}

void UIToolbar::select(int index)
{
    m_selected = (index >= 0 && index < (int)m_slots.size()) ? index : -1;
    for (int i = 0; i < (int)m_slots.size(); ++i) {
        auto* btn = m_slots[i];
        if (!btn) continue;
        btn->setSelected(i == m_selected);
    }
}

int UIToolbar::indexAt(float x, float y) const
{
    // 从上层到下层检查，优先命中后添加的（与绘制顺序一致）
    for (int i = (int)m_slots.size() - 1; i >= 0; --i) {
        const auto* btn = m_slots[i];
        if (btn && const_cast<UIButton*>(btn)->containsPoint(x, y)) {
            return i;
        }
    }
    return -1;
}

bool UIToolbar::clickAt(float x, float y)
{
    int idx = indexAt(x, y);
    if (idx >= 0) {
        select(idx);
        return true;
    }
    return false;
}

} // namespace Tina::UI
