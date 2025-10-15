#include "UILayoutManager.hpp"
#include "UINode.hpp"
#include "../core/Log.hpp"
#include <stack>
#include <limits>

namespace Tina::UI {

void UILayoutManager::registerNode(UINode* node)
{
    if (!node) return;

    m_nodes.insert(node);
    // TINA_DEBUG("UILayoutManager: 注册节点 '{}', 总节点数: {}",
    //            node->getName(), m_nodes.size());
}

void UILayoutManager::unregisterNode(UINode* node)
{
    if (!node) return;

    m_nodes.erase(node);
    m_dirtyNodes.erase(node);
    m_depthCache.erase(node);
    m_layoutInProgress.erase(node);

    // TINA_DEBUG("UILayoutManager: 注销节点 '{}', 剩余节点数: {}",
    //            node->getName(), m_nodes.size());
}

void UILayoutManager::requestLayout(UINode* node)
{
    if (!node || m_layoutInProgress.count(node)) return;

    // 标记节点及其所有子节点为脏
    std::stack<UINode*> stack;
    stack.push(node);

    while (!stack.empty()) {
        UINode* current = stack.top();
        stack.pop();

        if (m_dirtyNodes.insert(current).second) {
            // 新加入的脏节点，处理其子节点
            for (size_t i = 0; i < current->getChildCount(); ++i) {
                if (auto* child = current->getChild(i)) {
                    stack.push(child);
                }
            }
        }
    }

    // TINA_DEBUG("UILayoutManager: 请求布局 '{}', 脏节点数: {}",
    //                node->getName(), m_dirtyNodes.size());

    // 非批处理模式下立即执行
    if (!m_batchMode) {
        performPendingLayouts();
    }
}

void UILayoutManager::performPendingLayouts()
{
    if (m_dirtyNodes.empty()) return;

    // TINA_DEBUG("UILayoutManager: 执行批量布局, 脏节点数: {}", m_dirtyNodes.size());

    // 拓扑排序获取正确的布局顺序
    auto sortedNodes = topologicalSort(m_dirtyNodes);

    // 清理深度缓存
    m_depthCache.clear();

    // 按顺序执行布局
    for (UINode* node : sortedNodes) {
        if (m_dirtyNodes.count(node)) {
            performNodeLayout(node);
        }
    }

    // 清空脏节点集合
    m_dirtyNodes.clear();

    // 更新统计
    m_layoutBatchCount++;
    m_totalLayoutsPerformed += sortedNodes.size();

    TINA_DEBUG("UILayoutManager: 批量布局完成, 处理了 {} 个节点", sortedNodes.size());
}

void UILayoutManager::performLayoutNow(UINode* node)
{
    if (!node) return;

    // 防止递归
    if (m_layoutInProgress.count(node)) {
        TINA_WARN("UILayoutManager: 检测到递归布局 '{}'", node->getName());
        return;
    }

    // 构建此节点的依赖图
    std::unordered_set<UINode*> visited;
    std::vector<UINode*> sortedNodes;
    buildDependencyGraph(node, visited, sortedNodes);

    // 按依赖顺序执行布局
    for (UINode* n : sortedNodes) {
        performNodeLayout(n);
    }

    // 从脏节点中移除已处理的
    for (UINode* n : sortedNodes) {
        m_dirtyNodes.erase(n);
    }
}

void UILayoutManager::clearPendingLayouts()
{
    m_dirtyNodes.clear();
    m_layoutInProgress.clear();
    TINA_DEBUG("UILayoutManager: 清空所有待处理布局");
}

void UILayoutManager::buildDependencyGraph(UINode* root,
                                          std::unordered_set<UINode*>& visited,
                                          std::vector<UINode*>& sortedNodes)
{
    if (!root || visited.count(root)) return;

    visited.insert(root);

    // 先处理父节点（如果有）
    if (auto* parent = root->getParent()) {
        buildDependencyGraph(parent, visited, sortedNodes);
    }

    // 然后处理当前节点
    sortedNodes.push_back(root);

    // 最后处理子节点
    for (size_t i = 0; i < root->getChildCount(); ++i) {
        if (auto* child = root->getChild(i)) {
            buildDependencyGraph(child, visited, sortedNodes);
        }
    }
}

std::vector<UINode*> UILayoutManager::topologicalSort(const std::unordered_set<UINode*>& nodes)
{
    std::vector<UINode*> result(nodes.begin(), nodes.end());

    // 按深度排序：父节点先于子节点
    Container::Sort(result.begin(), result.end(), [this](UINode* a, UINode* b) {
        int depthA = getNodeDepth(a);
        int depthB = getNodeDepth(b);

        if (depthA != depthB) {
            return depthA < depthB;  // 深度小的（父节点）优先
        }

        // 同深度按名称排序，保证稳定性
        return a->getName() < b->getName();
    });

    return result;
}

int UILayoutManager::getNodeDepth(UINode* node)
{
    if (!node) return -1;

    // 检查缓存
    auto it = m_depthCache.find(node);
    if (it != m_depthCache.end()) {
        return it->second;
    }

    // 计算深度
    int depth = 0;
    UINode* parent = node->getParent();
    while (parent) {
        depth++;
        parent = parent->getParent();
    }

    // 缓存结果
    m_depthCache[node] = depth;
    return depth;
}

void UILayoutManager::performNodeLayout(UINode* node)
{
    if (!node || !node->needsLayout()) return;

    // 标记正在布局（防止递归）
    m_layoutInProgress.insert(node);

    // 先测量：如有 WrapContent 轴，使用父尺寸作为约束
    if (node->layoutWidth() == LayoutDim::WrapContent ||
        node->layoutHeight() == LayoutDim::WrapContent) {
        float availW = node->getParent() ? node->getParent()->getSize().x : std::numeric_limits<float>::infinity();
        float availH = node->getParent() ? node->getParent()->getSize().y : std::numeric_limits<float>::infinity();
        node->measure(availW, availH);
        if (node->layoutWidth() == LayoutDim::WrapContent) node->applyMeasuredWidth();
        if (node->layoutHeight() == LayoutDim::WrapContent) node->applyMeasuredHeight();
    }

    // 执行节点的布局逻辑
    node->onLayout();

    // 标记布局完成
    node->markLayoutClean();

    // 移除正在布局标记
    m_layoutInProgress.erase(node);

    // TINA_DEBUG("UILayoutManager: 完成节点 '{}' 的布局", node->getName());
}

} // namespace Tina::UI
