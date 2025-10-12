#include "UIEventSystem.hpp"
#include "UIComponents.hpp"
#include "../core/Log.hpp"

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
        TINA_DEBUG("UIEventSystem: 鼠标按下，节点={}",
            m_pressedNode ? m_pressedNode->getName() : "null");
        if (auto* btn = dynamic_cast<UIButton*>(m_pressedNode)) {
            btn->setPressed(true);
        }
    }

    // 释放时触发 click
    if (!m_mouseDown && m_mouseDownPrev) {
        TINA_DEBUG("UIEventSystem: 鼠标释放，按下节点={}, 当前节点={}",
            m_pressedNode ? m_pressedNode->getName() : "null",
            hitNode ? hitNode->getName() : "null");

        if (m_pressedNode && m_pressedNode == hitNode) {
            // 只有在同一节点按下并释放才算点击
            TINA_INFO("UIEventSystem: 触发点击事件，节点={}", m_pressedNode->getName());
            m_pressedNode->onClick();  // 调用虚函数（用于子类重写）
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
    // 从后往前遍历，因为后添加的子节点在上层
    for (int i = node->getChildCount() - 1; i >= 0; --i) {
        UINode* child = node->getChild(i);
        if (child) {
            UINode* hit = findNodeUnderMouse(child, mx, my);
            if (hit) return hit;
        }
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
    for (size_t i = 0; i < node->getChildCount(); ++i) {
        UINode* child = node->getChild(i);
        if (child) {
            collectAllNodes(child, outList);
        }
    }
}

} // namespace Tina::UI
