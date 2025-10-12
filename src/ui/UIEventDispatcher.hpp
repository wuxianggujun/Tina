//
// UIEventDispatcher.hpp - 完整的UI事件分发系统
// 支持事件捕获、冒泡、阻止传播等特性
//

#pragma once

#include "UINode.hpp"
#include "../core/Container.hpp"
#include "../core/Log.hpp"
#include <functional>
#include <unordered_map>

namespace Tina::UI {

// ==================== UI事件基类 ====================

class UIEvent {
public:
    enum class Phase {
        Capture,    // 捕获阶段（从根到目标）
        Target,     // 目标阶段（在目标节点）
        Bubble      // 冒泡阶段（从目标到根）
    };

    enum class Type {
        MouseDown,
        MouseUp,
        Click,
        MouseEnter,
        MouseLeave,
        MouseMove,
        KeyDown,
        KeyUp,
        Focus,
        Blur,
        Custom
    };

    UIEvent(Type type, UINode* target = nullptr)
        : m_type(type)
        , m_target(target)
        , m_currentTarget(nullptr)
        , m_phase(Phase::Target)
        , m_propagationStopped(false)
        , m_immediatePropagationStopped(false)
        , m_defaultPrevented(false)
        , m_bubbles(true)
        , m_cancelable(true) {}

    // 事件类型
    Type getType() const { return m_type; }

    // 目标节点（事件的最终目标）
    UINode* getTarget() const { return m_target; }

    // 当前处理节点（随着传递而变化）
    UINode* getCurrentTarget() const { return m_currentTarget; }
    void setCurrentTarget(UINode* node) { m_currentTarget = node; }

    // 事件阶段
    Phase getPhase() const { return m_phase; }
    void setPhase(Phase phase) { m_phase = phase; }

    // 停止传播（阻止后续节点接收事件）
    void stopPropagation() { m_propagationStopped = true; }
    bool isPropagationStopped() const { return m_propagationStopped; }

    // 立即停止传播（阻止当前节点的其他处理器）
    void stopImmediatePropagation() {
        m_propagationStopped = true;
        m_immediatePropagationStopped = true;
    }
    bool isImmediatePropagationStopped() const { return m_immediatePropagationStopped; }

    // 阻止默认行为
    void preventDefault() {
        if (m_cancelable) {
            m_defaultPrevented = true;
        }
    }
    bool isDefaultPrevented() const { return m_defaultPrevented; }

    // 是否支持冒泡
    bool bubbles() const { return m_bubbles; }
    void setBubbles(bool bubbles) { m_bubbles = bubbles; }

    // 是否可取消
    bool isCancelable() const { return m_cancelable; }
    void setCancelable(bool cancelable) { m_cancelable = cancelable; }

    // 设置事件目标（供 UINode::emit 使用）
    void setTarget(UINode* target) { m_target = target; }

protected:
    Type m_type;
    UINode* m_target;
    UINode* m_currentTarget;
    Phase m_phase;
    bool m_propagationStopped;
    bool m_immediatePropagationStopped;
    bool m_defaultPrevented;
    bool m_bubbles;
    bool m_cancelable;
};

// ==================== 具体事件类型 ====================

class UIMouseEvent : public UIEvent {
public:
    UIMouseEvent(Type type, UINode* target, float x, float y, int button = 0)
        : UIEvent(type, target)
        , m_x(x)
        , m_y(y)
        , m_button(button)
        , m_ctrlKey(false)
        , m_shiftKey(false)
        , m_altKey(false) {}

    float getX() const { return m_x; }
    float getY() const { return m_y; }
    int getButton() const { return m_button; }

    bool isCtrlPressed() const { return m_ctrlKey; }
    bool isShiftPressed() const { return m_shiftKey; }
    bool isAltPressed() const { return m_altKey; }

    void setModifiers(bool ctrl, bool shift, bool alt) {
        m_ctrlKey = ctrl;
        m_shiftKey = shift;
        m_altKey = alt;
    }

private:
    float m_x, m_y;
    int m_button;  // 0=left, 1=middle, 2=right
    bool m_ctrlKey, m_shiftKey, m_altKey;
};

// ==================== 事件监听器 ====================

using EventHandler = std::function<void(UIEvent&)>;

struct EventListener {
    EventHandler handler;
    bool useCapture;  // 是否在捕获阶段触发
    int priority;      // 优先级（高优先级先执行）

    EventListener(const EventHandler& h, bool capture = false, int prio = 0)
        : handler(h), useCapture(capture), priority(prio) {}
};

// ==================== 事件分发器 ====================

class UIEventDispatcher {
public:
    UIEventDispatcher() = default;
    ~UIEventDispatcher() = default;

    // 设置根节点
    void setRoot(UINode* root) { m_root = root; }

    // 分发事件（完整的捕获-目标-冒泡流程）
    void dispatchEvent(UIEvent& event);

    // 为节点添加事件监听器
    void addEventListener(UINode* node, UIEvent::Type type,
                         const EventHandler& handler,
                         bool useCapture = false,
                         int priority = 0);

    // 移除事件监听器
    void removeEventListener(UINode* node, UIEvent::Type type, bool useCapture = false);

    // 清除节点的所有监听器
    void clearEventListeners(UINode* node);

    // 处理鼠标输入（生成并分发事件）
    void handleMouseInput(float x, float y, bool leftDown);

private:
    // 构建事件路径（从根到目标）
    void buildEventPath(UINode* target, Container::Vector<UINode*>& path);

    // 触发节点的事件监听器
    void triggerListeners(UINode* node, UIEvent& event);

    // 查找鼠标下的节点
    UINode* findNodeUnderMouse(UINode* node, float x, float y);

private:
    UINode* m_root = nullptr;

    // 节点 -> 事件类型 -> 监听器列表
    using ListenerList = Container::Vector<EventListener>;
    using TypeListenerMap = std::unordered_map<UIEvent::Type, ListenerList>;
    std::unordered_map<UINode*, TypeListenerMap> m_listeners;

    // 鼠标状态跟踪
    UINode* m_hoveredNode = nullptr;
    UINode* m_pressedNode = nullptr;
    float m_mouseX = 0, m_mouseY = 0;
    bool m_mouseDown = false;
    bool m_mouseDownPrev = false;
};

// ==================== UINode 扩展 ====================

// 为 UINode 添加事件分发支持（可选，通过扩展方式）
class UINodeWithEvents : public UINode {
public:
    UINodeWithEvents(const std::string& name = "Node")
        : UINode(name) {}

    // 便捷的事件监听方法
    void on(UIEvent::Type type, const EventHandler& handler, bool useCapture = false) {
        if (m_dispatcher) {
            m_dispatcher->addEventListener(this, type, handler, useCapture);
        }
    }

    void off(UIEvent::Type type, bool useCapture = false) {
        if (m_dispatcher) {
            m_dispatcher->removeEventListener(this, type, useCapture);
        }
    }

    // 手动触发事件
    void emit(UIEvent& event) {
        if (m_dispatcher) {
            event.setTarget(this);
            m_dispatcher->dispatchEvent(event);
        }
    }

    void setEventDispatcher(UIEventDispatcher* dispatcher) {
        m_dispatcher = dispatcher;
    }

protected:
    UIEventDispatcher* m_dispatcher = nullptr;
};

} // namespace Tina::UI