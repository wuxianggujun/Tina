#include "EventSystem.hpp"
#include "../ui/UINode.hpp"
#include "../core/Log.hpp"

namespace Tina::Engine {

// 更新UI输入（每帧调用）
void EventSystem::updateUIInput(float mouseX, float mouseY, bool mouseDown) {
    m_uiContext.mouseX = mouseX;
    m_uiContext.mouseY = mouseY;
    m_uiContext.mouseDownPrev = m_uiContext.mouseDown;
    m_uiContext.mouseDown = mouseDown;
    
    static int debugCounter = 0;
    if (++debugCounter % 60 == 0) {  // 每60帧打印一次
        TINA_INFO("EventSystem::updateUIInput - 鼠标: ({}, {}), 按下: {}, 根节点: {}",
                   mouseX, mouseY, mouseDown, m_uiContext.root ? "有" : "无");
    }
    
    // 处理鼠标输入
    handleMouseInput();
}

// 构建事件路径（从根到目标）
void EventSystem::buildEventPath(UI::UINode* target, Vector<UI::UINode*>& path) {
    if (!target) return;
    
    // 从目标向上遍历到根
    UI::UINode* current = target;
    while (current) {
        path.push_back(current);
        current = current->getParent();
    }
    
    // 反转路径（变为从根到目标）
    std::reverse(path.begin(), path.end());
}

// 查找鼠标下的节点（递归，深度优先，后序遍历确保上层节点优先）
UI::UINode* EventSystem::findNodeUnderMouse(UI::UINode* node, float x, float y) {
    if (!node || !node->isVisible() || !node->isEnabled()) {
        return nullptr;
    }
    
    // 先检查子节点（后序遍历，子节点优先）
    // 按 zIndex 排序（高 zIndex 优先）
    auto& children = node->getChildren();
    Container::Vector<UI::UINode*> sortedChildren;
    sortedChildren.reserve(children.size());
    
    for (auto& child : children) {
        if (child) {
            sortedChildren.push_back(child.get());
        }
    }
    
    // 按 zIndex 降序排序
    std::sort(sortedChildren.begin(), sortedChildren.end(), 
        [](UI::UINode* a, UI::UINode* b) {
            return a->zIndex() > b->zIndex();
        });
    
    // 递归检查子节点
    for (auto* child : sortedChildren) {
        UI::UINode* found = findNodeUnderMouse(child, x, y);
        if (found) {
            return found;
        }
    }
    
    // 检查当前节点
    if (node->isInteractable()) {
        auto wp = node->getWorldPosition();
        auto sz = node->getSize();
        bool hit = node->containsPoint(x, y);
        
        // 调试：打印所有可交互节点的信息
        static int debugCount = 0;
        if (++debugCount % 300 == 0 && node->getName().find("Btn") != std::string::npos) {
            TINA_INFO("检查节点 '{}': 世界坐标({}, {}), 尺寸({}, {}), 鼠标({}, {}), 命中: {}",
                     node->getName(), wp.x, wp.y, sz.x, sz.y, x, y, hit);
        }
        
        if (hit) {
            return node;
        }
    }
    
    return nullptr;
}

// 处理鼠标输入（内部实现）
void EventSystem::handleMouseInput() {
    if (!m_uiContext.root) {
        TINA_WARN("EventSystem::handleMouseInput - 没有UI根节点！");
        return;
    }
    
    float mx = m_uiContext.mouseX;
    float my = m_uiContext.mouseY;
    bool mouseDown = m_uiContext.mouseDown;
    bool mouseDownPrev = m_uiContext.mouseDownPrev;
    
    // 查找鼠标下的节点
    UI::UINode* nodeUnderMouse = findNodeUnderMouse(m_uiContext.root, mx, my);
    
    // 🔧 修复：移除静态变量，避免悬空指针
    // static UI::UINode* lastNode = nullptr;
    // if (nodeUnderMouse != lastNode) {
    //     if (nodeUnderMouse) {
    //         TINA_INFO("EventSystem - 鼠标进入节点: {}", nodeUnderMouse->getName());
    //     } else {
    //         TINA_INFO("EventSystem - 鼠标离开所有节点");
    //     }
    //     lastNode = nodeUnderMouse;
    // }
    
    // 处理 hover 状态变化
    if (nodeUnderMouse != m_uiContext.hoveredNode) {
        // 鼠标离开旧节点
        if (m_uiContext.hoveredNode && m_uiContext.hoveredNode->isHoverable()) {
            m_uiContext.hoveredNode->onMouseLeave();
            
            // 触发 MouseLeave 事件（不需要捕获/冒泡）
            // MouseLeaveEvent event;
            // event.target = m_uiContext.hoveredNode;
            // dispatch(event);
        }
        
        // 鼠标进入新节点
        if (nodeUnderMouse && nodeUnderMouse->isHoverable()) {
            nodeUnderMouse->onMouseEnter();
            
            // 触发 MouseEnter 事件（不需要捕获/冒泡）
            // MouseEnterEvent event;
            // event.target = nodeUnderMouse;
            // dispatch(event);
        }
        
        m_uiContext.hoveredNode = nodeUnderMouse;
    }
    
    // 处理鼠标按下
    if (mouseDown && !mouseDownPrev) {
        if (nodeUnderMouse && nodeUnderMouse->isClickable()) {
            m_uiContext.pressedNode = nodeUnderMouse;
            nodeUnderMouse->onMouseDown(mx, my);
        }
    }
    
    // 处理鼠标释放
    if (!mouseDown && mouseDownPrev) {
        if (m_uiContext.pressedNode) {
            m_uiContext.pressedNode->onMouseUp(mx, my);
            
            // 如果释放时仍在同一节点上，触发点击
            if (m_uiContext.pressedNode == nodeUnderMouse) {
                m_uiContext.pressedNode->onClick();
            }
            
            m_uiContext.pressedNode = nullptr;
        }
    }
}

} // namespace Tina::Engine
