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

    // 自动检测 UI 树版本变化，标记索引需要重建
    uint64_t ver = UINode::treeVersion();
    if (m_lastTreeVersion != ver) {
        m_lastTreeVersion = ver;
        m_needRebuildIndex = true;
    }

    // 查找鼠标下的节点
    UINode* hitNode = findNodeUnderMouseIndexed(x, y);

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
        // Mouse Down（仅对可点击目标）
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
            // 如果节点可聚焦，设置焦点
            if (m_pressedNode->isFocusable()) {
                setFocus(m_pressedNode);
            }

            UIMouseEvent clickEvent(UIEvent::Type::Click, m_pressedNode, x, y);
            dispatchEvent(clickEvent);
            if (!clickEvent.isDefaultPrevented()) {
                m_pressedNode->onClick();
            }
        }

        m_pressedNode = nullptr;
    }

    // Mouse Move 发送给可 hover 的目标
    if (m_hoveredNode) {
        UIMouseEvent moveEvent(UIEvent::Type::MouseMove, m_hoveredNode, x, y);
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
    Container::Reverse(path.begin(), path.end());
}

void UIEventDispatcher::triggerListeners(UINode* node, UIEvent& event) {
    auto nodeIt = m_listeners.find(node);
    if (nodeIt == m_listeners.end()) return;

    auto typeIt = nodeIt->second.find(event.getType());
    if (typeIt == nodeIt->second.end()) return;

    bool isCapture = (event.getPhase() == UIEvent::Phase::Capture);
    bool isTarget = (event.getPhase() == UIEvent::Phase::Target);

    // 使用快照遍历，避免回调中修改监听列表导致迭代不稳定
    auto listenersSnapshot = typeIt->second;
    for (const auto& listener : listenersSnapshot) {
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

// 基于扁平索引的命中检测（性能优先）
UINode* UIEventDispatcher::findNodeUnderMouseIndexed(float x, float y) {
    if (!m_root) return nullptr;

    // 优化：先检查根节点边界，提前退出
    auto rootPos = m_root->getWorldPosition();
    auto rootSize = m_root->getSize();
    if (x < rootPos.x || x > rootPos.x + rootSize.x ||
        y < rootPos.y || y > rootPos.y + rootSize.y) {
        return nullptr;  // 鼠标在整个UI树外部，直接返回
    }

    if (m_needRebuildIndex) {
        rebuildIndex();
        m_needRebuildIndex = false;
    }

    // 从后向前遍历（后面的元素z-index更高）
    for (int i = static_cast<int>(m_indexedNodes.size()) - 1; i >= 0; --i) {
        UINode* n = m_indexedNodes[static_cast<size_t>(i)];
        if (!n->isVisible() || !n->isEnabled()) continue;

        // 优化：先做粗略边界检查，避免调用 containsPoint
        auto pos = n->getWorldPosition();
        auto size = n->getSize();
        if (x < pos.x || x > pos.x + size.x ||
            y < pos.y || y > pos.y + size.y) {
            continue;  // 快速跳过不相交的节点
        }

        // 精确检查（如果节点有自定义形状）
        if (n->containsPoint(x, y)) return n;
    }
    return nullptr;
}

void UIEventDispatcher::rebuildIndex() {
    m_indexedNodes.clear();
    if (!m_root) return;

    // 收集所有节点并记录序号（用于稳定排序）
    Tina::Container::Vector<UINode*> all;
    collectAllNodes(m_root, all);

    struct Item { UINode* node; int z; size_t seq; };
    Tina::Container::Vector<Item> items;
    items.reserve(all.size());
    size_t seq = 0;
    for (auto* n : all) {
        if (!n) continue;
        if (!n->isInteractable()) continue; // 仅索引可交互节点
        items.push_back(Item{ n, n->zIndex(), seq++ });
    }

    // 按 zIndex 升序稳定排序（保证同 zIndex 按遍历顺序）
    Tina::Container::Sort(items.begin(), items.end(), [](const Item& a, const Item& b){
        if (a.z != b.z) return a.z < b.z;
        return a.seq < b.seq;
    });

    m_indexedNodes.reserve(items.size());
    for (const auto& it : items) m_indexedNodes.push_back(it.node);
}

void UIEventDispatcher::collectAllNodes(UINode* node, Container::Vector<UINode*>& outList) {
    if (!node) return;
    outList.push_back(node);
    const auto& children = node->getChildren();
    for (const auto& ch : children) {
        if (ch.get()) collectAllNodes(ch.get(), outList);
    }
}

// === 焦点管理实现 ===

void UIEventDispatcher::setFocus(UINode* node) {
    if (m_focusedNode == node) return;

    // 先发送 blur 事件给旧焦点
    if (m_focusedNode) {
        UIFocusEvent blurEvent(UIEvent::Type::Blur, m_focusedNode);
        dispatchEvent(blurEvent);
    }

    m_focusedNode = node;

    // 再发送 focus 事件给新焦点
    if (m_focusedNode) {
        UIFocusEvent focusEvent(UIEvent::Type::Focus, m_focusedNode);
        dispatchEvent(focusEvent);
    }
}

void UIEventDispatcher::focusNext() {
    if (!m_root) return;

    // 收集所有可聚焦节点
    Container::Vector<UINode*> focusableNodes;
    collectFocusableNodes(m_root, focusableNodes);

    if (focusableNodes.empty()) return;

    // 找到当前焦点的索引
    int currentIndex = -1;
    for (int i = 0; i < static_cast<int>(focusableNodes.size()); ++i) {
        if (focusableNodes[i] == m_focusedNode) {
            currentIndex = i;
            break;
        }
    }

    // 移动到下一个
    int nextIndex = (currentIndex + 1) % focusableNodes.size();
    setFocus(focusableNodes[nextIndex]);
}

void UIEventDispatcher::focusPrevious() {
    if (!m_root) return;

    Container::Vector<UINode*> focusableNodes;
    collectFocusableNodes(m_root, focusableNodes);

    if (focusableNodes.empty()) return;

    int currentIndex = -1;
    for (int i = 0; i < static_cast<int>(focusableNodes.size()); ++i) {
        if (focusableNodes[i] == m_focusedNode) {
            currentIndex = i;
            break;
        }
    }

    // 移动到上一个
    int prevIndex = (currentIndex <= 0) ? focusableNodes.size() - 1 : currentIndex - 1;
    setFocus(focusableNodes[prevIndex]);
}

void UIEventDispatcher::collectFocusableNodes(UINode* node, Container::Vector<UINode*>& outList) {
    if (!node || !node->isVisible() || !node->isEnabled()) return;

    if (node->isFocusable()) {
        outList.push_back(node);
    }

    const auto& children = node->getChildren();
    for (const auto& child : children) {
        collectFocusableNodes(child.get(), outList);
    }
}

void UIEventDispatcher::handleKeyInput(int key, bool down) {
    // Tab 键切换焦点
    if (key == 9 && down) {  // Tab key
        focusNext();
        return;
    }

    // Shift+Tab 反向切换
    if (key == 353 && down) {  // Shift+Tab
        focusPrevious();
        return;
    }

    // 将键盘事件发送给焦点节点
    if (m_focusedNode) {
        UIKeyEvent keyEvent(down ? UIEvent::Type::KeyDown : UIEvent::Type::KeyUp,
                            m_focusedNode, key);
        keyEvent.setBubbles(true);  // 键盘事件支持冒泡
        dispatchEvent(keyEvent);
    }
}

} // namespace Tina::UI





