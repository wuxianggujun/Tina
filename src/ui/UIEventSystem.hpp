//
// UI 事件系统：管理鼠标交互
// - 支持 hover、click 事件
// - 自动分发事件到 UI 树
// - 内部使用 UIEventDispatcher 提供高级功能
//

#pragma once

#include "UINode.hpp"
#include "UIEventDispatcher.hpp"
#include "../core/Container.hpp"
#include <memory>

namespace Tina::UI {

class UIEventSystem {
public:
    UIEventSystem();
    ~UIEventSystem();

    // ==================== 基础接口（保持兼容） ====================

    // 设置根节点：切换 UI 树时重置悬停/按下状态，避免悬挂指针
    void setRoot(UINode* root);

    // 更新鼠标状态（每帧调用）
    void updateMouse(float mouseX, float mouseY, bool mouseDown);

    // 处理事件（在鼠标更新后调用）
    void processEvents();

    // ==================== 高级功能（新增） ====================

    // 启用高级事件功能（冒泡、捕获等）
    void enableAdvancedEvents(bool enable = true) {
        m_useAdvancedEvents = enable;
    }

    // 是否启用了高级事件
    bool isAdvancedEventsEnabled() const {
        return m_useAdvancedEvents;
    }

    // 获取事件分发器（用于高级功能）
    UIEventDispatcher* getDispatcher() {
        return m_dispatcher.get();
    }

    // 添加事件监听器（高级功能）
    void addEventListener(UINode* node, UIEvent::Type type,
                         const EventHandler& handler,
                         bool useCapture = false,
                         int priority = 0) {
        if (m_dispatcher) {
            m_dispatcher->addEventListener(node, type, handler, useCapture, priority);
        }
    }

    // 移除事件监听器
    void removeEventListener(UINode* node, UIEvent::Type type, bool useCapture = false) {
        if (m_dispatcher) {
            m_dispatcher->removeEventListener(node, type, useCapture);
        }
    }

    // 手动分发事件
    void dispatchEvent(UIEvent& event) {
        if (m_dispatcher) {
            m_dispatcher->dispatchEvent(event);
        }
    }

private:
    // 简单模式的辅助函数（向后兼容）
    UINode* findNodeUnderMouse(UINode* node, float mx, float my);
    void collectAllNodes(UINode* node, Tina::Container::Vector<UINode*>& outList);
    void processEventsSimple();     // 原有的简单处理
    void processEventsAdvanced();   // 使用分发器的高级处理

private:
    UINode* m_root = nullptr;
    UINode* m_hoveredNode = nullptr;
    UINode* m_pressedNode = nullptr;

    float m_mouseX = 0;
    float m_mouseY = 0;
    bool m_mouseDown = false;
    bool m_mouseDownPrev = false;

    // 高级事件分发器（可选使用）
    std::unique_ptr<UIEventDispatcher> m_dispatcher;
    bool m_useAdvancedEvents = false;  // 默认使用简单模式，保持兼容性
};

} // namespace Tina::UI
