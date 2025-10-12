//
// UI 事件系统（仅高级分发）
// - 统一使用 UIEventDispatcher：捕获/目标/冒泡
// - 删除简易路径与开关，避免重复逻辑与分支
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

    // 输入采集
    void setRoot(UINode* root);
    void updateMouse(float mouseX, float mouseY, bool mouseDown);
    void processEvents();

    // 高级分发入口/便捷透传
    UIEventDispatcher* getDispatcher() { return m_dispatcher.get(); }
    void addEventListener(UINode* node, UIEvent::Type type,
                         const EventHandler& handler,
                         bool useCapture = false,
                         int priority = 0) {
        if (m_dispatcher) m_dispatcher->addEventListener(node, type, handler, useCapture, priority);
    }
    void removeEventListener(UINode* node, UIEvent::Type type, bool useCapture = false) {
        if (m_dispatcher) m_dispatcher->removeEventListener(node, type, useCapture);
    }
    void dispatchEvent(UIEvent& event) {
        if (m_dispatcher) m_dispatcher->dispatchEvent(event);
    }

private:
    void setDispatcherRecursive(UINode* node, UIEventDispatcher* dispatcher);

    UINode* m_root = nullptr;
    UINode* m_hoveredNode = nullptr;
    UINode* m_pressedNode = nullptr;
    float m_mouseX = 0;
    float m_mouseY = 0;
    bool m_mouseDown = false;
    bool m_mouseDownPrev = false;
    std::unique_ptr<UIEventDispatcher> m_dispatcher;
};

} // namespace Tina::UI

