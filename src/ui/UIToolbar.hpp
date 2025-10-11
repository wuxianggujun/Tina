//
// 简单工具栏布局（顶部一排格子）
// - 使用现有 UINode/UIPanel/UIButton 渲染与事件系统
// - 仅布局与点击回调（功能占位）
//

#pragma once

#include "UICore.hpp"
#include "UIComponents.hpp"
#include "UILayout.hpp"
#include "UIEventSystem.hpp"
#include "../core/Container.hpp"
#include "../core/Memory.hpp"
#include "UINode.hpp"  // 引入UINode基类

namespace Tina::UI {

// 工具栏状态（用于保存/恢复）
struct ToolbarState {
    int selectedIndex = -1;
    int slotCount = 8;
    int slotSize = 64;
    int gap = 8;
    int padding = 12;
    int barHeight = 80;
    bool visible = true;
};

class UIToolbar : public UINode {
public:
    // 迁移至新架构：仅依赖 UIRenderer
    bool initialize(int screenW, int screenH, UIRenderer& renderer);
    void shutdown();

    // 覆盖UINode的onWindowSizeChanged()方法（框架自动调用）
    void onWindowSizeChanged(int screenW, int screenH) override {
        onResize(screenW, screenH);
        // 不需要调用父类的默认实现，因为UIToolbar自己管理子节点
    }

    void onResize(int screenW, int screenH);  // 内部布局方法
    void update(float dt);
    void render(uint16_t viewId);

    // 事件系统对接
    UIEventSystem& events() { return m_events; }
    UINode* root() const { return m_root.get(); }

    int barHeight() const { return m_barH; }
    bool hitTest(float x, float y) const;
    void setMousePos(float x, float y) { m_mouseX = x; m_mouseY = y; }

    // 选择与参数调整
    void select(int index);
    int selectedIndex() const { return m_selected; }
    int slotCount() const { return (int)m_slots.size(); }
    void setSlotCount(int count) { m_slotCount = std::max(1, count); buildLayout(); }
    void setSlotSize(int size) { m_slotSize = std::max(8, size); buildLayout(); }
    void setGap(int gap) { m_gap = std::max(0, gap); buildLayout(); }
    void setPadding(int pad) { m_padding = std::max(0, pad); buildLayout(); }

    // 设置某个格子的图标纹理句柄（由调用方保证句柄生命周期）
    void setSlotIcon(int index, bgfx::TextureHandle tex);

    // 根据屏幕坐标返回格子索引（-1 表示不在任何格子）
    int indexAt(float x, float y) const;

    // 处理一次点击（内部计算命中并切换选中），返回是否命中了任一格子
    bool clickAt(float x, float y);

    // === 状态管理（用于场景切换） ===
    ToolbarState getState() const;
    void setState(const ToolbarState& state);

private:
    void buildLayout();

private:
    // 渲染
    UIRenderer* m_renderer = nullptr;

    // 根与部件
    Memory::UniquePtr<UINode> m_root; // 屏幕根（大小=屏幕）
    UIPanel* m_bar = nullptr;        // 顶部栏背景
    UIHStack* m_stack = nullptr;     // 水平栈布局容器
    Tina::Container::Vector<UIButton*> m_slots; // 格子按钮

    // 事件系统
    UIEventSystem m_events;

    // 布局参数
    int m_screenW = 0;
    int m_screenH = 0;
    int m_barH = 80;      // 工具栏高度
    int m_padding = 12;   // 内边距
    int m_gap = 8;        // 格子间距
    int m_slotSize = 64;  // 格子尺寸（正方形）
    int m_slotCount = 8;  // 默认 8 个格子
    int m_selected = -1;  // 选中格

    // Tooltip
    bool m_tipVisible = false;
    std::string m_tipText;
    float m_mouseX = 0.0f, m_mouseY = 0.0f;
};

} // namespace Tina::UI
