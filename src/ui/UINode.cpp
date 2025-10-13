#include "UINode.hpp"
#include "UICore.hpp"
#include "UILayoutManager.hpp"
#include "../engine/Application.hpp"
#include "../core/Log.hpp"
#include <algorithm>

namespace Tina::UI {

// 静态 UI 树版本号，用于命中索引置脏
std::atomic<uint64_t> UINode::s_treeVersion{1};
uint64_t UINode::treeVersion() { return s_treeVersion.load(std::memory_order_relaxed); }
void UINode::bumpTreeVersion() { s_treeVersion.fetch_add(1, std::memory_order_relaxed); }

UINode::UINode(const std::string& name)
    : m_name(name)
{
    // 注册到布局管理器
    GetLayoutManager().registerNode(this);
    
    // 自动获取并设置引擎事件系统
    if (auto* app = Engine::Application::instance()) {
        m_eventSystem = &app->events();
    }
}

UINode::~UINode()
{
    // 从布局管理器注销
    GetLayoutManager().unregisterNode(this);

    // 新版：自动清理所有子节点（通过UniquePtr）
    // 子节点会递归销毁其子节点
    m_children.clear();
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

    switch (m_anchor) {
    case Anchor::TopLeft:       ox = 0;          oy = 0;          break;
    case Anchor::TopCenter:     ox = psize.x/2;  oy = 0;          break;
    case Anchor::TopRight:      ox = psize.x;    oy = 0;          break;
    case Anchor::MiddleLeft:    ox = 0;          oy = psize.y/2;  break;
    case Anchor::MiddleCenter:  ox = psize.x/2;  oy = psize.y/2;  break;
    case Anchor::MiddleRight:   ox = psize.x;    oy = psize.y/2;  break;
    case Anchor::BottomLeft:    ox = 0;          oy = psize.y;    break;
    case Anchor::BottomCenter:  ox = psize.x/2;  oy = psize.y;    break;
    case Anchor::BottomRight:   ox = psize.x;    oy = psize.y;    break;
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
    if (m_layoutDirty) return;  // 已经标记，无需重复

    m_layoutDirty = true;

    // 布局变化会影响坐标，标记坐标也需要更新
    m_dirty = true;

    // 通知布局管理器
    GetLayoutManager().requestLayout(this);
}

void UINode::performLayoutNow()
{
    if (!m_layoutDirty) return;  // 布局是最新的，无需重新计算

    GetLayoutManager().performLayoutNow(this);
}

// === 点测试 ===

bool UINode::containsPoint(float x, float y)
{
    // ✅ 关键修复：在访问坐标前，确保布局是最新的
    if (m_layoutDirty) {
        performLayoutNow();
    }

    Tina::Math::Vec2 wp = getWorldPosition();
    return x >= wp.x && x < wp.x + m_size.x &&
           y >= wp.y && y < wp.y + m_size.y;
}

// === 更新与渲染 ===

void UINode::update(float dt)
{
    if (!m_enabled) return;

    // ✅ 在更新前确保布局是最新的
    if (m_layoutDirty) {
        performLayoutNow();
    }

    // 更新自身状态（动画、交互等）
    onUpdate(dt);

    // 递归更新所有子节点
    for (auto& child : m_children) {
        if (child) child->update(dt);
    }
}

void UINode::render(uint16_t viewId, UIRenderer& renderer)
{
    if (!m_visible) return;

    // ✅ 在渲染前确保布局是最新的
    // 这很重要：如果场景恢复时没有先 update()，布局可能还没执行
    if (m_layoutDirty) {
        performLayoutNow();
    }

    onRender(viewId, renderer);

    for (auto& child : m_children) {
        if (child) child->render(viewId, renderer);
    }
}

} // namespace Tina::UI




