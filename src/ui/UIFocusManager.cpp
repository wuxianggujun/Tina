#include "UIFocusManager.hpp"

namespace Tina::UI {

void UIFocusManager::setFocus(UINode* node) {
    if (m_focusedNode == node) return;
    
    // 失去焦点
    if (m_focusedNode) {
        if (m_onFocusLost) {
            m_onFocusLost(m_focusedNode);
        }
    }
    
    // 获得焦点
    m_focusedNode = node;
    if (m_focusedNode) {
        if (m_onFocusGained) {
            m_onFocusGained(m_focusedNode);
        }
    }
}

void UIFocusManager::clearFocus() {
    setFocus(nullptr);
}

void UIFocusManager::focusNext() {
    if (m_focusableNodes.empty()) return;
    
    int currentIndex = findNodeIndex(m_focusedNode);
    int nextIndex = (currentIndex + 1) % m_focusableNodes.size();
    
    setFocus(m_focusableNodes[nextIndex]);
}

void UIFocusManager::focusPrev() {
    if (m_focusableNodes.empty()) return;
    
    int currentIndex = findNodeIndex(m_focusedNode);
    int prevIndex = (currentIndex - 1 + m_focusableNodes.size()) % m_focusableNodes.size();
    
    setFocus(m_focusableNodes[prevIndex]);
}

void UIFocusManager::setRoot(UINode* root) {
    m_root = root;
    refreshFocusableNodes();
}

void UIFocusManager::refreshFocusableNodes() {
    m_focusableNodes.clear();
    if (m_root) {
        collectFocusableNodes(m_root);
    }
}

void UIFocusManager::collectFocusableNodes(UINode* node) {
    if (!node || !node->isVisible() || !node->isEnabled()) return;
    
    // 如果节点可聚焦，添加到列表
    if (node->isFocusable()) {
        m_focusableNodes.push_back(node);
    }
    
    // 递归收集子节点
    auto& children = node->getChildren();
    for (auto& child : children) {
        if (child) {
            collectFocusableNodes(child.get());
        }
    }
}

int UIFocusManager::findNodeIndex(UINode* node) const {
    for (size_t i = 0; i < m_focusableNodes.size(); ++i) {
        if (m_focusableNodes[i] == node) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace Tina::UI
