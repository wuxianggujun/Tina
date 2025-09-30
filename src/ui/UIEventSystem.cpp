#include "UIEventSystem.hpp"
#include "UIComponents.hpp"

namespace Tina::UI {

UIEventSystem::UIEventSystem()
{
}

UIEventSystem::~UIEventSystem()
{
}

void UIEventSystem::updateMouse(float mouseX, float mouseY, bool mouseDown)
{
    m_mouseX = mouseX;
    m_mouseY = mouseY;
    m_mouseDownPrev = m_mouseDown;
    m_mouseDown = mouseDown;
}

void UIEventSystem::processEvents()
{
    if (!m_root) return;

    // 查找鼠标下的节点（深度优先，后绘制的节点优先）
    UINode* hitNode = findNodeUnderMouse(m_root, m_mouseX, m_mouseY);

    // === Hover 事件 ===
    if (hitNode != m_hoveredNode) {
        // Leave
        if (m_hoveredNode) {
            m_hoveredNode->onMouseLeave();
            // 如果是 Button，清除 hover 状态
            if (auto* btn = dynamic_cast<UIButton*>(m_hoveredNode)) {
                btn->setHovered(false);
            }
        }

        m_hoveredNode = hitNode;

        // Enter
        if (m_hoveredNode) {
            m_hoveredNode->onMouseEnter();
            if (auto* btn = dynamic_cast<UIButton*>(m_hoveredNode)) {
                btn->setHovered(true);
            }
        }
    }

    // === Click 事件 ===
    // 按下时记录
    if (m_mouseDown && !m_mouseDownPrev) {
        m_pressedNode = hitNode;
        if (auto* btn = dynamic_cast<UIButton*>(m_pressedNode)) {
            btn->setPressed(true);
        }
    }

    // 释放时触发 click
    if (!m_mouseDown && m_mouseDownPrev) {
        if (m_pressedNode && m_pressedNode == hitNode) {
            // 只有在同一节点按下并释放才算点击
            m_pressedNode->onClick();
            if (m_pressedNode->onClickCallback) {
                m_pressedNode->onClickCallback();
            }
        }

        // 清除按下状态
        if (auto* btn = dynamic_cast<UIButton*>(m_pressedNode)) {
            btn->setPressed(false);
        }
        m_pressedNode = nullptr;
    }
}

UINode* UIEventSystem::findNodeUnderMouse(UINode* node, float mx, float my)
{
    if (!node || !node->isVisible() || !node->isEnabled()) {
        return nullptr;
    }

    // 先递归检查子节点（后绘制的在上层）
    const auto& children = node->getChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        UINode* hit = findNodeUnderMouse(*it, mx, my);
        if (hit) return hit;
    }

    // 再检查当前节点
    if (node->containsPoint(mx, my)) {
        return node;
    }

    return nullptr;
}

void UIEventSystem::collectAllNodes(UINode* node, Tina::Container::Vector<UINode*>& outList)
{
    if (!node) return;
    outList.push_back(node);
    for (auto* child : node->getChildren()) {
        collectAllNodes(child, outList);
    }
}

} // namespace Tina::UI