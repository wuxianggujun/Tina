//
// UI布局管理器 - 自动处理布局依赖和更新顺序
// 解决父子节点布局时序问题，避免手动管理更新顺序
//

#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <algorithm>
#include "../core/Singleton.hpp"
#include "../core/Log.hpp"

namespace Tina::UI {

// 前向声明
class UINode;

class UILayoutManager : public Core::Singleton<UILayoutManager> {
    friend class Core::Singleton<UILayoutManager>;

public:
    // 注册/注销节点
    void registerNode(UINode* node);
    void unregisterNode(UINode* node);

    // 请求布局（标记为脏）
    void requestLayout(UINode* node);

    // 批量执行布局（在帧末尾调用）
    void performPendingLayouts();

    // 立即执行单个节点的布局（包括其依赖）
    void performLayoutNow(UINode* node);

    // 清理所有待处理的布局
    void clearPendingLayouts();

    // 获取统计信息
    size_t getNodeCount() const { return m_nodes.size(); }
    size_t getPendingLayoutCount() const { return m_dirtyNodes.size(); }

    // 启用/禁用批处理模式（默认启用）
    void setBatchMode(bool enabled) { m_batchMode = enabled; }
    bool isBatchMode() const { return m_batchMode; }

private:
    UILayoutManager() = default;
    ~UILayoutManager() = default;

    // 构建节点的布局依赖图
    void buildDependencyGraph(UINode* root, std::unordered_set<UINode*>& visited,
                            std::vector<UINode*>& sortedNodes);

    // 拓扑排序获取正确的布局顺序
    std::vector<UINode*> topologicalSort(const std::unordered_set<UINode*>& nodes);

    // 获取节点深度（用于排序）
    int getNodeDepth(UINode* node);

    // 执行单个节点的布局
    void performNodeLayout(UINode* node);

private:
    // 所有注册的节点
    std::unordered_set<UINode*> m_nodes;

    // 需要布局的脏节点
    std::unordered_set<UINode*> m_dirtyNodes;

    // 节点深度缓存（避免重复计算）
    mutable std::unordered_map<UINode*, int> m_depthCache;

    // 批处理模式（true=延迟到帧末，false=立即执行）
    bool m_batchMode = true;

    // 防止递归布局
    std::unordered_set<UINode*> m_layoutInProgress;

    // 统计信息
    size_t m_totalLayoutsPerformed = 0;
    size_t m_layoutBatchCount = 0;
};

// 便捷访问
inline UILayoutManager& GetLayoutManager() {
    return UILayoutManager::getInstance();
}

} // namespace Tina::UI