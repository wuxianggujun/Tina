#include "EventSystem.hpp"
#include "../ui/UINode.hpp"
#include "../core/Log.hpp"
// Container.hpp 已经包含了所有需要的算法封装

namespace Tina::Engine {

// ==================== UI 根节点设置 ====================

void EventSystem::setUIRoot(Memory::SharedPtr<UI::UINode> root) {
    m_uiContext.root = root;  // 自动转换为 WeakPtr
    // 清空子节点引用（它们的生命周期由root保证）
    m_uiContext.hoveredNode = nullptr;
    m_uiContext.pressedNode = nullptr;
    m_uiContext.focusedNode = nullptr;
}

void EventSystem::setUIRoot(UI::UINode* root) {
    if (root) {
        try {
            m_uiContext.root = root->getWeakPtr();
        } catch (...) {
            // 如果节点不是由 shared_ptr 管理，回退到空
            m_uiContext.root.reset();
        }
    } else {
        m_uiContext.root.reset();
    }
    m_uiContext.hoveredNode = nullptr;
    m_uiContext.pressedNode = nullptr;
    m_uiContext.focusedNode = nullptr;
}

// ==================== UI 输入更新 ====================

// 更新UI输入（每帧调用）
void EventSystem::updateUIInput(float mouseX, float mouseY, bool mouseDown) {
    m_uiContext.mouseX = mouseX;
    m_uiContext.mouseY = mouseY;
    m_uiContext.mouseDownPrev = m_uiContext.mouseDown;
    m_uiContext.mouseDown = mouseDown;
    
    // 调试：每秒打印一次状态（60fps）
    #ifdef TINA_DEBUG_UI_INPUT
    static int debugCounter = 0;
    if (++debugCounter % 60 == 0) {
        auto root = m_uiContext.root.lock();
        TINA_DEBUG("EventSystem - 鼠标: ({}, {}), 按下: {}, 根节点: {}",
                   mouseX, mouseY, mouseDown, root ? "有" : "无");
    }
    #endif
    
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
    
    // ✅ 使用项目封装的 Reverse（Container.hpp）
    Container::Reverse(path.begin(), path.end());
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
    
    // ✅ 按 zIndex 降序排序（使用项目封装的 Sort）
    Container::Sort(sortedChildren.begin(), sortedChildren.end(), 
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
    // ✅ 尝试锁定根节点的 weak_ptr
    auto root = m_uiContext.root.lock();
    if (!root) {
        // 根节点已销毁或未设置，安全返回
        return;
    }
    
    float mx = m_uiContext.mouseX;
    float my = m_uiContext.mouseY;
    bool mouseDown = m_uiContext.mouseDown;
    bool mouseDownPrev = m_uiContext.mouseDownPrev;
    
    // 查找鼠标下的节点
    UI::UINode* nodeUnderMouse = findNodeUnderMouse(root.get(), mx, my);
    
    // 处理 hover 状态变化
    if (nodeUnderMouse != m_uiContext.hoveredNode) {
        // 鼠标离开旧节点
        if (m_uiContext.hoveredNode && m_uiContext.hoveredNode->isHoverable()) {
            m_uiContext.hoveredNode->onMouseLeave();
        }
        
        // 鼠标进入新节点
        if (nodeUnderMouse && nodeUnderMouse->isHoverable()) {
            nodeUnderMouse->onMouseEnter();
        }
        
        m_uiContext.hoveredNode = nodeUnderMouse;
    }
    
    // ✅ 处理鼠标按下
    if (mouseDown && !mouseDownPrev) {
        if (nodeUnderMouse && nodeUnderMouse->isClickable()) {
            m_uiContext.pressedNode = nodeUnderMouse;
            nodeUnderMouse->onMouseDown(mx, my);
        }
    }
    
    // ✅ 处理鼠标释放
    if (!mouseDown && mouseDownPrev) {
        if (m_uiContext.pressedNode) {
            m_uiContext.pressedNode->onMouseUp(mx, my);
            
            // 如果释放时仍在同一节点上，触发点击
            if (m_uiContext.pressedNode == nodeUnderMouse) {
                m_uiContext.pressedNode->onClick();
            }
        }
        m_uiContext.pressedNode = nullptr;
    }
}

} // namespace Tina::Engine
