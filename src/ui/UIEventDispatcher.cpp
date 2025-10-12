//
// UIEventDispatcher.cpp - UI事件分发系统实现
//

#include "UIEventDispatcher.hpp"
#include "../core/Log.hpp"
#include <algorithm>

namespace Tina::UI {

void UIEventDispatcher::dispatchEvent(UIEvent& event) {
    if (!event.getTarget()) {
        TINA_WARN("UIEventDispatcher: 事件没有目标节点");
        return;
    }

    // 1. 构建事件路径（从根到目标）
    Container::Vector<UINode*> eventPath;
    buildEventPath(event.getTarget(), eventPath);

    if (eventPath.empty()) {
        return;
    }

    // 2. 捕获阶段（从根到目标的父节点）
    event.setPhase(UIEvent::Phase::Capture);
    for (size_t i = 0; i < eventPath.size() - 1; ++i) {
        if (event.isPropagationStopped()) break;

        event.setCurrentTarget(eventPath[i]);
        triggerListeners(eventPath[i], event);
    }

    // 3. 目标阶段
    if (!event.isPropagationStopped()) {
        event.setPhase(UIEvent::Phase::Target);
        event.setCurrentTarget(event.getTarget());
        triggerListeners(event.getTarget(), event);
    }

    // 4. 冒泡阶段（从目标的父节点到根）
    if (event.bubbles() && !event.isPropagationStopped()) {
        event.setPhase(UIEvent::Phase::Bubble);
        for (int i = eventPath.size() - 2; i >= 0; --i) {
            if (event.isPropagationStopped()) break;

            event.setCurrentTarget(eventPath[i]);
            triggerListeners(eventPath[i], event);
        }
    }
}

void UIEventDispatcher::addEventListener(UINode* node, UIEvent::Type type,
                                         const EventHandler& handler,
                                         bool useCapture, int priority) {
    if (!node || !handler) return;

    auto& nodeListeners = m_listeners[node];
    auto& typeListeners = nodeListeners[type];

    // 添加监听器
    typeListeners.emplace_back(handler, useCapture, priority);

    // 按优先级排序（高优先级在前）
    std::sort(typeListeners.begin(), typeListeners.end(),
        [](const EventListener& a, const EventListener& b) {
            return a.priority > b.priority;
        });
}

void UIEventDispatcher::removeEventListener(UINode* node, UIEvent::Type type, bool useCapture) {
    auto nodeIt = m_listeners.find(node);
    if (nodeIt == m_listeners.end()) return;

    auto typeIt = nodeIt->second.find(type);
    if (typeIt == nodeIt->second.end()) return;

    // 移除匹配的监听器
    auto& listeners = typeIt->second;
    listeners.erase(
        std::remove_if(listeners.begin(), listeners.end(),
            [useCapture](const EventListener& l) {
                return l.useCapture == useCapture;
            }),
        listeners.end()
    );

    // 清理空容器
    if (listeners.empty()) {
        nodeIt->second.erase(typeIt);
        if (nodeIt->second.empty()) {
            m_listeners.erase(nodeIt);
        }
    }
}

void UIEventDispatcher::clearEventListeners(UINode* node) {
    m_listeners.erase(node);
}

void UIEventDispatcher::handleMouseInput(float x, float y, bool leftDown) {
    m_mouseX = x;
    m_mouseY = y;
    m_mouseDownPrev = m_mouseDown;
    m_mouseDown = leftDown;

    if (!m_root) return;

    // 查找鼠标下的节点
    UINode* hitNode = findNodeUnderMouse(m_root, x, y);

    // 处理 hover 事件
    if (hitNode != m_hoveredNode) {
        // Mouse Leave
        if (m_hoveredNode) {
            UIMouseEvent leaveEvent(UIEvent::Type::MouseLeave, m_hoveredNode, x, y);
            dispatchEvent(leaveEvent);
            m_hoveredNode->onMouseLeave();
        }

        m_hoveredNode = hitNode;

        // Mouse Enter
        if (m_hoveredNode) {
            UIMouseEvent enterEvent(UIEvent::Type::MouseEnter, m_hoveredNode, x, y);
            dispatchEvent(enterEvent);
            m_hoveredNode->onMouseEnter();
        }
    }

    // 处理点击事件
    if (leftDown && !m_mouseDownPrev) {
        // Mouse Down
        m_pressedNode = hitNode;
        if (m_pressedNode) {
            UIMouseEvent downEvent(UIEvent::Type::MouseDown, m_pressedNode, x, y);
            dispatchEvent(downEvent);
            m_pressedNode->onMouseDown(x, y);
        }
    } else if (!leftDown && m_mouseDownPrev) {
        // Mouse Up
        if (hitNode) {
            UIMouseEvent upEvent(UIEvent::Type::MouseUp, hitNode, x, y);
            dispatchEvent(upEvent);
            hitNode->onMouseUp(x, y);
        }

        // Click (只在同一节点上按下和释放才触发)
        if (m_pressedNode && m_pressedNode == hitNode) {
            UIMouseEvent clickEvent(UIEvent::Type::Click, m_pressedNode, x, y);
            dispatchEvent(clickEvent);
            if (!clickEvent.isDefaultPrevented()) {
                m_pressedNode->onClick();
            }
        }

        m_pressedNode = nullptr;
    }

    // Mouse Move
    if (hitNode) {
        UIMouseEvent moveEvent(UIEvent::Type::MouseMove, hitNode, x, y);
        dispatchEvent(moveEvent);
    }
}

void UIEventDispatcher::buildEventPath(UINode* target, Container::Vector<UINode*>& path) {
    UINode* node = target;
    while (node) {
        path.push_back(node);
        node = node->getParent();
    }
    // 反转路径，使其从根到目标
    std::reverse(path.begin(), path.end());
}

void UIEventDispatcher::triggerListeners(UINode* node, UIEvent& event) {
    auto nodeIt = m_listeners.find(node);
    if (nodeIt == m_listeners.end()) return;

    auto typeIt = nodeIt->second.find(event.getType());
    if (typeIt == nodeIt->second.end()) return;

    bool isCapture = (event.getPhase() == UIEvent::Phase::Capture);
    bool isTarget = (event.getPhase() == UIEvent::Phase::Target);

    for (const auto& listener : typeIt->second) {
        if (event.isImmediatePropagationStopped()) break;

        // 检查监听器是否应该在当前阶段触发
        bool shouldTrigger = false;
        if (isTarget) {
            // 目标阶段：所有监听器都触发
            shouldTrigger = true;
        } else if (isCapture && listener.useCapture) {
            // 捕获阶段：只触发捕获监听器
            shouldTrigger = true;
        } else if (!isCapture && !listener.useCapture) {
            // 冒泡阶段：只触发非捕获监听器
            shouldTrigger = true;
        }

        if (shouldTrigger) {
            listener.handler(event);
        }
    }
}

UINode* UIEventDispatcher::findNodeUnderMouse(UINode* node, float x, float y) {
    if (!node || !node->isVisible() || !node->isEnabled()) {
        return nullptr;
    }

    // 先检查子节点（后绘制的在上层）
    const auto& children = node->getChildren();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        UINode* hit = findNodeUnderMouse(it->get(), x, y);
        if (hit) return hit;
    }

    // 再检查自己
    auto pos = node->getWorldPosition();
    auto size = node->getSize();
    if (x >= pos.x && x <= pos.x + size.x &&
        y >= pos.y && y <= pos.y + size.y) {
        return node;
    }

    return nullptr;
}

} // namespace Tina::UI
