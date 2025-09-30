//
// UI 事件系统：管理鼠标交互
// - 支持 hover、click 事件
// - 自动分发事件到 UI 树
//

#pragma once

#include "UINode.hpp"
#include "../core/Container.hpp"

namespace Tina::UI {

class UIEventSystem {
public:
    UIEventSystem();
    ~UIEventSystem();

    // 设置根节点
    void setRoot(UINode* root) { m_root = root; }

    // 更新鼠标状态（每帧调用）
    void updateMouse(float mouseX, float mouseY, bool mouseDown);

    // 处理事件（在鼠标更新后调用）
    void processEvents();

private:
    UINode* findNodeUnderMouse(UINode* node, float mx, float my);
    void collectAllNodes(UINode* node, Tina::Container::Vector<UINode*>& outList);

private:
    UINode* m_root = nullptr;
    UINode* m_hoveredNode = nullptr;
    UINode* m_pressedNode = nullptr;

    float m_mouseX = 0;
    float m_mouseY = 0;
    bool m_mouseDown = false;
    bool m_mouseDownPrev = false;
};

} // namespace Tina::UI