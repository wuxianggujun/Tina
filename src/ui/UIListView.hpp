//
// UIListView - 简单纵向列表控件（支持滚轮滚动与点击选择）
//

#pragma once

#include "UINode.hpp"
#include "UICore.hpp"
#include "UIColors.hpp"
#include "../core/Container.hpp"
#include "../core/Color.hpp"
#include "../core/Log.hpp"  // 添加 Log 头文件
#include "../engine/Application.hpp"
#include "../engine/InputSystem.hpp"  // 需要完整类型以调用方法
#include <bgfx/bgfx.h>  // （保留渲染依赖）
#include <functional>
#include <string>
#include <algorithm>

namespace Tina::UI {

class UIListView : public UINode {
public:
    using Items = Tina::Container::Vector<std::string>;

    UIListView(const std::string& name = "UIListView") : UINode(name) {
        setInteractable(true);
        setClickable(true);
        setHoverable(true);
    }

    // 数据接口
    void setItems(const Items& items) {
        m_items = items;
        clampScroll();
    }
    void addItem(const std::string& text) {
        m_items.push_back(text);
        clampScroll();
    }
    void clear() {
        m_items.clear();
        m_selected = -1;
        m_scroll = 0.0f;
    }
    const Items& items() const { return m_items; }

    // 选择
    int selectedIndex() const { return m_selected; }
    void setSelectedIndex(int idx) {
        if (idx < 0 || idx >= (int)m_items.size()) return;
        m_selected = idx;
        if (m_onSelectionChanged) m_onSelectionChanged(m_selected);
        ensureItemVisible(m_selected);
    }

    // 外观参数
    void setItemHeight(float h) {
        float oldHeight = m_itemHeight;
        m_itemHeight = (h > 8.0f ? h : 8.0f);
        if (oldHeight != m_itemHeight) {
            TINA_DEBUG("UIListView::setItemHeight - {} -> {}, scroll before: {}", oldHeight, m_itemHeight, m_scroll);
            clampScroll();  // 高度改变后重新限制滚动范围
            TINA_DEBUG("UIListView::setItemHeight - scroll after: {}", m_scroll);
        }
    }
    float itemHeight() const { return m_itemHeight; }
    void setFontPx(int px) { m_fontPx = px; }
    int fontPx() const { return m_fontPx; }

    // 回调
    void setOnSelectionChanged(std::function<void(int)> cb) { m_onSelectionChanged = std::move(cb); }
    void setOnItemActivated(std::function<void(int)> cb) { m_onItemActivated = std::move(cb); }

    // 滚动控制（供场景使用）
    void scrollBy(float deltaPx) { m_scroll += deltaPx; clampScroll(); }
    void scrollTo(float offsetPx) { m_scroll = offsetPx; clampScroll(); }

    // 事件处理
    void onMouseEnter() override { m_hovered = true; }
    void onMouseLeave() override { m_hovered = false; }
    void onMouseDown(float x, float y) override {
        m_lastMouseX = x; m_lastMouseY = y;
    }
    void onMouseUp(float x, float y) override {
        m_lastMouseX = x; m_lastMouseY = y;
    }
    void onClick() override {
        // 将最后一次鼠标坐标转换为相对本控件坐标
        auto world = getWorldPosition();
        float localY = m_lastMouseY - world.y;
        int idx = (int)((localY + m_scroll) / m_itemHeight);
        if (idx >= 0 && idx < (int)m_items.size()) {
            bool sameAsLast = (idx == m_lastClickedIndex) && (m_doubleClickTimer > 0.0f);
            setSelectedIndex(idx);
            if (sameAsLast) {
                if (m_onItemActivated) m_onItemActivated(m_selected);
            }
            m_lastClickedIndex = idx;
            m_doubleClickTimer = DOUBLE_CLICK_THRESHOLD;
        }
    }

protected:
    void onUpdate(float dt) override {
        // 双击计时器衰减
        if (m_doubleClickTimer > 0.0f) {
            m_doubleClickTimer -= dt;
            if (m_doubleClickTimer < 0.0f) m_doubleClickTimer = 0.0f;
        }

        // 响应鼠标滚轮（不依赖 hover 状态，直接检查鼠标位置）
        if (auto* app = Tina::Engine::Application::instance()) {
            float wheel = app->input().getMouseWheelDelta();
            if (wheel != 0.0f) {
                // 检查鼠标是否在列表区域内
                auto world = getWorldPosition();
                auto size = getSize();
                float mx = app->input().getMouseX();
                float my = app->input().getMouseY();
                bool inside = (mx >= world.x && mx <= world.x + size.x &&
                              my >= world.y && my <= world.y + size.y);

                TINA_INFO("🎯 UIListView::onUpdate - 滚轮={}, 鼠标=({},{}), 列表=({},{},{}x{}), inside={}, scroll={}",
                         wheel, mx, my, world.x, world.y, size.x, size.y, inside, m_scroll);

                if (inside) {
                    float contentH = m_items.size() * m_itemHeight;
                    // 仅当内容高度超过视口高度时才滚动
                    if (contentH > size.y && m_itemHeight > 0.0f) {
                        float oldScroll = m_scroll;
                        // 约定：正值向上滚动（内容向下移动）
                        scrollBy(-wheel * m_itemHeight * 2.0f);
                        if (m_scroll != oldScroll) {
                            TINA_INFO("✅ UIListView 滚动: {} -> {}", oldScroll, m_scroll);
                        }
                    }
                }
            }
        }
    }

    void onRender(uint16_t viewId, UIRenderer& renderer) override {
        auto world = getWorldPosition();
        auto size = getSize();

        // 调试：跟踪世界坐标与滚动量
        TINA_DEBUG("UIListView::onRender - world=({},{}), size=({},{}) scroll={}",
                   world.x, world.y, size.x, size.y, m_scroll);

        // 背景（限定在自身边界内）
        auto clipRectDraw = [&](float x, float y, float w, float h, const Tina::Core::Color& c){
            float x0 = std::max(x, world.x);
            float y0 = std::max(y, world.y);
            float x1 = std::min(x + w, world.x + size.x);
            float y1 = std::min(y + h, world.y + size.y);
            if (x1 <= x0 || y1 <= y0) return;
            renderer.drawRect(viewId, x0, y0, x1 - x0, y1 - y0, c);
        };
        clipRectDraw(world.x, world.y, size.x, size.y, Tina::UI::UIColors::PanelBg);

        // 设置裁剪区域（防止列表项绘制超出列表边界）
        // 注意：UI 坐标原点在左上角，像素为单位
        uint16_t scissorX = static_cast<uint16_t>(std::max(0.0f, world.x));
        uint16_t scissorY = static_cast<uint16_t>(std::max(0.0f, world.y));
        uint16_t scissorW = static_cast<uint16_t>(std::min(size.x, 65535.0f));
        uint16_t scissorH = static_cast<uint16_t>(std::min(size.y, 65535.0f));
        renderer.setClipRect((int16_t)scissorX, (int16_t)scissorY, scissorW, scissorH);

        // 计算可见范围
        if (m_itemHeight <= 0.0f) {
            renderer.clearClipRect();  // 重置裁剪
            return;
        }

        int first = (int)(m_scroll / m_itemHeight);
        if (first < 0) first = 0;
        int visibleRows = (int)(size.y / m_itemHeight) + 2;  // +2 用于缓冲
        int last = std::min((int)m_items.size(), first + visibleRows);

        // 当前鼠标所在行（用于hover高亮）
        int hoverIndex = -1;
        if (auto* app = Tina::Engine::Application::instance()) {
            float mx = app->input().getMouseX();
            float my = app->input().getMouseY();
            bool inside = (mx >= world.x && mx <= world.x + size.x && my >= world.y && my <= world.y + size.y);
            if (inside) {
                float localY = my - world.y;
                hoverIndex = (int)((localY + m_scroll) / m_itemHeight);
            }
        }

        // 绘制列表项
        for (int i = first; i < last; ++i) {
            float y = world.y + (i * m_itemHeight - m_scroll);

            // 行背景
            const bool isSel = (i == m_selected);
            const bool isHover = (i == hoverIndex);
            Tina::Core::Color rowBg = isSel ? Tina::UI::UIColors::ButtonHover :
                                     (isHover ? Tina::UI::UIColors::ButtonPressed :
                                               Tina::UI::UIColors::PanelBg);
            clipRectDraw(world.x + 1, y, size.x - 2, m_itemHeight - 1, rowBg);

            // 文本
            UIRenderer::TextOptions to{};
            to.r = 1; to.g = 1; to.b = 1; to.a = 1;
            to.fontPx = (m_fontPx > 0 ? m_fontPx : 20);
            renderer.drawText(viewId, world.x + 10, y + 6, m_items[i], to);
        }

        // 滚动条（仅在内容超出时显示）
        float contentH = m_items.size() * m_itemHeight;
        if (contentH > size.y) {
            float barW = 6.0f;
            float trackX = world.x + size.x - barW - 2.0f;
            float trackY = world.y + 2.0f;
            float trackH = size.y - 4.0f;
            clipRectDraw(trackX, trackY, barW, trackH, Tina::Core::Color(0,0,0,0.3f));

            float ratio = size.y / contentH;
            float thumbH = std::max(20.0f, trackH * ratio);
            float maxScroll = contentH - size.y;
            float t = (maxScroll > 0.0f) ? (m_scroll / maxScroll) : 0.0f;
            float thumbY = trackY + (trackH - thumbH) * t;
            clipRectDraw(trackX, thumbY, barW, thumbH, Tina::Core::Color(1,1,1,0.6f));
        }

        // 重置裁剪区域（让后续渲染不受影响）
        renderer.clearClipRect();
    }

private:
    void clampScroll() {
        float contentH = m_items.size() * m_itemHeight;
        float viewH = getSize().y;
        float maxScroll = std::max(0.0f, contentH - viewH);
        if (m_scroll < 0.0f) m_scroll = 0.0f;
        if (m_scroll > maxScroll) m_scroll = maxScroll;
    }

    void ensureItemVisible(int idx) {
        float viewH = getSize().y;
        float itemTop = idx * m_itemHeight;
        float itemBottom = itemTop + m_itemHeight;
        if (itemTop < m_scroll) {
            m_scroll = itemTop;
        } else if (itemBottom > m_scroll + viewH) {
            m_scroll = itemBottom - viewH;
        }
        clampScroll();
    }

private:
    Items m_items;
    float m_itemHeight = 32.0f;
    int m_fontPx = 20;
    float m_scroll = 0.0f;  // 以像素计
    int m_selected = -1;
    bool m_hovered = false;

    // 点击/双击
    float m_lastMouseX = 0.0f, m_lastMouseY = 0.0f;
    int m_lastClickedIndex = -1;
    float m_doubleClickTimer = 0.0f;
    static constexpr float DOUBLE_CLICK_THRESHOLD = 0.30f; // 秒

    std::function<void(int)> m_onSelectionChanged;
    std::function<void(int)> m_onItemActivated;
};

} // namespace Tina::UI
