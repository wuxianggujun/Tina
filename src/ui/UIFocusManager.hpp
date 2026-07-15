//
// UI 焦点管理系统
// - 管理UI组件的焦点状态
// - 支持Tab键切换焦点
// - 支持焦点视觉反馈
//

#pragma once

#include "UINode.hpp"
#include "../core/Container.hpp"
#include <functional>

namespace Tina::UI {

// ============================================================================
// UIFocusManager - 焦点管理器
// ============================================================================

class UIFocusManager {
public:
    UIFocusManager() = default;
    ~UIFocusManager() = default;

    // === 焦点控制 ===
    
    // 设置焦点到指定节点
    void setFocus(UINode* node);
    
    // 清除焦点
    void clearFocus();
    
    // 获取当前焦点节点
    UINode* getFocusedNode() const { return m_focusedNode; }
    
    // 检查节点是否有焦点
    bool hasFocus(UINode* node) const { return m_focusedNode == node; }
    
    // === Tab键导航 ===
    
    // 切换到下一个可聚焦节点（Tab键）
    void focusNext();
    
    // 切换到上一个可聚焦节点（Shift+Tab键）
    void focusPrev();
    
    // === 焦点列表管理 ===
    
    // 设置根节点（自动收集所有可聚焦节点）
    void setRoot(UINode* root);
    
    // 手动刷新可聚焦节点列表
    void refreshFocusableNodes();
    
    // === 焦点事件回调 ===
    
    using FocusCallback = std::function<void(UINode*)>;
    
    // 设置焦点获得回调
    void setOnFocusGained(FocusCallback callback) {
        m_onFocusGained = callback;
    }
    
    // 设置焦点失去回调
    void setOnFocusLost(FocusCallback callback) {
        m_onFocusLost = callback;
    }

private:
    // 收集所有可聚焦节点
    void collectFocusableNodes(UINode* node);
    
    // 查找节点在列表中的索引
    int findNodeIndex(UINode* node) const;
    
    UINode* m_root = nullptr;
    UINode* m_focusedNode = nullptr;
    Tina::Container::Vector<UINode*> m_focusableNodes;  // 可聚焦节点列表
    
    FocusCallback m_onFocusGained;
    FocusCallback m_onFocusLost;
};

} // namespace Tina::UI
