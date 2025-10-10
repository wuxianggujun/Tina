#include "UINode.hpp"
#include "UICore.hpp"
#include <algorithm>

namespace Tina::UI {

UINode::UINode(const std::string& name)
    : m_name(name)
{
}

UINode::~UINode()
{
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

// === 点测试 ===

bool UINode::containsPoint(float worldX, float worldY)
{
    Tina::Math::Vec2 wp = getWorldPosition();
    return worldX >= wp.x && worldX < wp.x + m_size.x &&
           worldY >= wp.y && worldY < wp.y + m_size.y;
}

// === 更新与渲染 ===

void UINode::update(float dt)
{
    if (!m_enabled) return;

    onUpdate(dt);

    for (auto& child : m_children) {
        if (child) child->update(dt);
    }

    // 通用包裹（WrapContent）：依据子项包裹自身尺寸
    // 注意：MatchParent 的分配应由父容器完成，这里仅处理 WrapContent 回填
    if (m_layoutW == LayoutDim::WrapContent || m_layoutH == LayoutDim::WrapContent) {
        float maxRight = 0.0f;
        float maxBottom = 0.0f;
        for (auto& childPtr : m_children) {
            UINode* child = childPtr.get();
            if (!child || !child->isVisible()) continue;
            Tina::Math::Vec2 cp = child->getPosition(); // 局部坐标（容器 onUpdate 应以 TopLeft 放置）
            Tina::Math::Vec2 cs = child->getSize();
            if (cp.x + cs.x > maxRight) maxRight = cp.x + cs.x;
            if (cp.y + cs.y > maxBottom) maxBottom = cp.y + cs.y;
        }
        if (m_layoutW == LayoutDim::WrapContent) m_size.x = maxRight;
        if (m_layoutH == LayoutDim::WrapContent) m_size.y = maxBottom;
        // 尺寸变化影响变换
        m_dirty = true;
    }
}

void UINode::render(uint16_t viewId, UIRenderer& renderer)
{
    if (!m_visible) return;

    onRender(viewId, renderer);

    for (auto& child : m_children) {
        if (child) child->render(viewId, renderer);
    }
}

} // namespace Tina::UI
