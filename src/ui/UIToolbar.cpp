#include "UIToolbar.hpp"
#include "../core/Log.hpp"
#include <algorithm>

namespace Tina::UI {

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
    // 半透明深色背景
    m_bar->setColor(0.10f, 0.10f, 0.12f, 0.85f);
    m_root->addChild(m_bar);

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
    }
}

void UIToolbar::buildLayout()
{
    if (!m_bar) return;

    // 清理旧按钮
    for (auto* btn : m_slots) {
        m_bar->removeChild(btn);
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
    m_bar->setSize((float)barW, (float)m_barH);
    m_bar->setPosition(barX, 0.0f);

    // 创建按钮并布局
    for (int i = 0; i < count; ++i) {
        auto* btn = new UIButton("ToolSlot");
        btn->setSize((float)m_slotSize, (float)m_slotSize);
        btn->setAnchor(Anchor::TopLeft);
        float x = (float)m_padding + i * (float)(m_slotSize + m_gap);
        float y = (float)m_padding;
        btn->setPosition(x, y);

        // 显示序号
        btn->setText(std::to_string(i + 1));
        // 点击占位动作
        btn->onClickCallback = [i]() {
            TINA_INFO("Toolbar slot {} clicked (功能占位)", i + 1);
        };

        m_bar->addChild(btn);
        m_slots.push_back(btn);
    }
}

bool UIToolbar::hitTest(float x, float y) const
{
    if (!m_bar) return false;
    // 使用 UINode 自带的 containsPoint（基于世界坐标）
    return const_cast<UIPanel*>(m_bar)->containsPoint(x, y);
}

} // namespace Tina::UI
