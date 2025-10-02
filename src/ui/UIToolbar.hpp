//
// 简单工具栏布局（顶部一排格子）
// - 使用现有 UINode/UIPanel/UIButton 渲染与事件系统
// - 仅布局与点击回调（功能占位）
//

#pragma once

#include "UICore.hpp"
#include "UIComponents.hpp"
#include "UIEventSystem.hpp"
#include "../core/Container.hpp"

namespace Tina::UI {

class UIToolbar {
public:
    bool initialize(int screenW, int screenH, UIRenderer& renderer, TextRenderer* text);
    void shutdown();

    void onResize(int screenW, int screenH);
    void update(float dt);
    void render(uint16_t viewId);

    // 事件系统对接
    UIEventSystem& events() { return m_events; }
    UINode* root() const { return m_root; }

    int barHeight() const { return m_barH; }
    bool hitTest(float x, float y) const;

private:
    void buildLayout();

private:
    // 渲染
    UIRenderer* m_renderer = nullptr;
    TextRenderer* m_text = nullptr;

    // 根与部件
    UINode* m_root = nullptr;        // 屏幕根（大小=屏幕）
    UIPanel* m_bar = nullptr;        // 顶部栏背景
    Tina::Container::Vector<UIButton*> m_slots; // 格子按钮

    // 事件系统
    UIEventSystem m_events;

    // 布局参数
    int m_screenW = 0;
    int m_screenH = 0;
    int m_barH = 48;      // 工具栏高度
    int m_padding = 8;    // 内边距
    int m_gap = 6;        // 格子间距
    int m_slotSize = 40;  // 格子尺寸（正方形）
    int m_slotCount = 8;  // 默认 8 个格子
};

} // namespace Tina::UI
