#include "UINode.hpp"
#include "UILayoutManager.hpp"
#include "../core/Log.hpp"
#include <limits>
#include <algorithm>

namespace Tina::UI {

// 静态 UI 树版本号，用于命中索引置脏
std::atomic<uint64_t> UINode::s_treeVersion{1};
uint64_t UINode::treeVersion() { return s_treeVersion.load(std::memory_order_relaxed); }
void UINode::bumpTreeVersion() { s_treeVersion.fetch_add(1, std::memory_order_relaxed); }

UINode::UINode(const std::string& name)
    : m_name(name)
{
    bumpTreeVersion();
    // 布局与事件上下文均由 Scene 在 addUIRoot() 时显式注入。
}

UINode::~UINode()
{
    bumpTreeVersion();
    // 从布局管理器注销（如果有）
    if (m_layoutManager) {
        m_layoutManager->unregisterNode(this);
    }

    // 新版：自动清理所有子节点（通过UniquePtr）
    // 子节点会递归销毁其子节点
    m_children.clear();
}

void UINode::setLayoutManager(UILayoutManager* layoutManager)
{
    if (m_layoutManager == layoutManager) return;

    if (m_layoutManager) {
        m_layoutManager->unregisterNode(this);
    }
    m_layoutManager = layoutManager;
    if (m_layoutManager) {
        m_layoutManager->registerNode(this);
        if (m_layoutDirty) m_layoutManager->requestLayout(this);
    }

    for (auto& child : m_children) {
        if (child) child->setLayoutManager(layoutManager);
    }
}

void UINode::setEventSystem(Tina::Engine::EventSystem* eventSystem)
{
    m_eventSystem = eventSystem;
    for (auto& child : m_children) {
        if (child) child->setEventSystem(eventSystem);
    }
}

// === 层级管理（新版） ===

Memory::UniquePtr<UINode> UINode::removeChild(UINode* child)
{
    if (!child) return nullptr;

    auto it = std::find_if(m_children.begin(), m_children.end(),
        [child](const Memory::UniquePtr<UINode>& ptr) {
            return ptr.get() == child;
        });
    
    if (it != m_children.end()) {
        Memory::UniquePtr<UINode> removed = std::move(*it);
        removed->m_parent = nullptr;
        removed->setLayoutManager(nullptr);
        m_children.erase(it);
        bumpTreeVersion();
        return removed;
    }
    
    return nullptr;
}

void UINode::removeFromParent()
{
    if (m_parent) {
        // 注意：这会导致自己被销毁（如果父节点拥有所有权）
        // 调用者需要确保不再使用this指针
        m_parent->removeChild(this);
        bumpTreeVersion();
    }
}

UINode* UINode::findChild(const std::string& name) const
{
    for (const auto& child : m_children) {
        if (child && child->getName() == name) {
            return child.get();
        }
    }
    return nullptr;
}

// === 世界坐标计算 ===

Tina::Math::Vec2 UINode::anchorOffset() const
{
    if (!m_parent) return {0, 0};

    const auto psize = m_parent->getSize();
    float ox = 0, oy = 0;

    // 水平锚点偏移
    switch (m_hAlign) {
        case HAlign::Left:   ox = 0; break;
        case HAlign::Center: ox = psize.x * 0.5f; break;
        case HAlign::Right:  ox = psize.x; break;
    }

    // 垂直锚点偏移
    switch (m_vAlign) {
        case VAlign::Top:    oy = 0; break;
        case VAlign::Middle: oy = psize.y * 0.5f; break;
        case VAlign::Bottom: oy = psize.y; break;
    }

    return {ox, oy};
}

void UINode::updateWorldTransform()
{
    if (!m_dirty) return;

    // ✅ 保存旧的世界坐标，用于检测是否真正变化
    Tina::Math::Vec2 oldWorldPos = m_worldPos;

    if (m_parent) {
        Tina::Math::Vec2 parentWorld = m_parent->getWorldPosition();
        Tina::Math::Vec2 offset = anchorOffset();
        m_worldPos = parentWorld + offset + m_position;
    } else {
        // 根节点：世界坐标 = 局部坐标
        m_worldPos = m_position;
    }

    m_dirty = false;

    // ✅ 只有世界坐标真正变化时才标记子节点（减少不必要的重算）
    if (oldWorldPos.x != m_worldPos.x || oldWorldPos.y != m_worldPos.y) {
        for (auto& child : m_children) {
            if (child) child->m_dirty = true;
        }
    }
}

Tina::Math::Vec2 UINode::getWorldPosition()
{
    updateWorldTransform();
    return m_worldPos;
}

// === 布局系统（使用布局管理器） ===

void UINode::requestLayout()
{
    m_layoutDirty = true;
    m_dirty = true;
    // 失效测量缓存
    m_measureCacheValid = false;

    // 通知布局管理器（如果有）
    if (m_layoutManager) {
        m_layoutManager->requestLayout(this);
    }

    // 向上冒泡，保证父容器根据子变化重新测量/布局
    if (m_parent) {
        m_parent->requestLayout();
    }
}

void UINode::performLayoutNow()
{
    if (!m_layoutDirty) return;  // 布局是最新的，无需重新计算

    if (m_layoutManager) {
        m_layoutManager->performLayoutNow(this);
    }
}

// === 点测试 ===

bool UINode::containsPoint(float x, float y)
{
    // hit-test 必须是只读阶段。Scene::updateFrame() 会先批量提交布局，
    // 再执行一次命中测试，禁止在这里产生第二次布局。
    Tina::Math::Vec2 wp = getWorldPosition();
    return x >= wp.x && x < wp.x + m_size.x &&
           y >= wp.y && y < wp.y + m_size.y;
}

// === 更新与渲染 ===

void UINode::update(float dt)
{
    if (!m_enabled) return;

    // 更新自身状态（动画、交互等）
    onUpdate(dt);

    // 递归更新所有子节点
    for (auto& child : m_children) {
        if (child) child->update(dt);
    }
}

} // namespace Tina::UI

// ====== 测量实现 ======

namespace Tina::UI {

Tina::Math::Vec2 UINode::measure(float availableWidth, float availableHeight)
{
    // 若约束与缓存一致，直接返回
    if (m_measureCacheValid &&
        m_measureConstraintW == availableWidth &&
        m_measureConstraintH == availableHeight) {
        return m_measuredSize;
    }

    // 调用子类测量
    m_measuredSize = measureContent(availableWidth, availableHeight);

    // 应用约束
    m_measuredSize.x = std::max(m_minSize.x, std::min(m_maxSize.x, m_measuredSize.x));
    m_measuredSize.y = std::max(m_minSize.y, std::min(m_maxSize.y, m_measuredSize.y));

    // 缓存
    m_measureCacheValid = true;
    m_measureConstraintW = availableWidth;
    m_measureConstraintH = availableHeight;
    return m_measuredSize;
}

void UINode::setMeasuredSize(const Tina::Math::Vec2& size)
{
    m_measuredSize = size;
    m_measureCacheValid = true;
}

void UINode::applyMeasuredSize()
{
    // 仅更新实际尺寸，不修改 layoutW/H 语义
    if (m_size.x != m_measuredSize.x || m_size.y != m_measuredSize.y) {
        m_size = m_measuredSize;
        m_dirty = true;
        markChildrenDirty();
    }
}

void UINode::applyMeasuredWidth()
{
    if (m_size.x != m_measuredSize.x) {
        m_size.x = m_measuredSize.x;
        m_dirty = true;
        markChildrenDirty();
    }
}

void UINode::applyMeasuredHeight()
{
    if (m_size.y != m_measuredSize.y) {
        m_size.y = m_measuredSize.y;
        m_dirty = true;
        markChildrenDirty();
    }
}

} // namespace Tina::UI
