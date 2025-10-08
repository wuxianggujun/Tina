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
    // 析构时清理所有子节点
    for (auto* child : m_children) {
        child->m_parent = nullptr;
        delete child;
    }
    m_children.clear();
}

// === 层级管理 ===

void UINode::addChild(UINode* child)
{
    if (!child || child == this) return;

    // 如果已有父节点，先移除
    if (child->m_parent) {
        child->m_parent->removeChild(child);
    }

    child->m_parent = this;
    m_children.push_back(child);
    child->m_dirty = true;
}

void UINode::removeChild(UINode* child)
{
    if (!child) return;

    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        (*it)->m_parent = nullptr;
        m_children.erase(it);
    }
}

void UINode::removeFromParent()
{
    if (m_parent) {
        m_parent->removeChild(this);
    }
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

    if (m_parent) {
        Tina::Math::Vec2 parentWorld = m_parent->getWorldPosition();
        Tina::Math::Vec2 offset = anchorOffset();
        m_worldPos = parentWorld + offset + m_position;
    } else {
        // 根节点：世界坐标 = 局部坐标
        m_worldPos = m_position;
    }

    m_dirty = false;

    // 子节点也需要更新
    for (auto* child : m_children) {
        child->m_dirty = true;
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

    for (auto* child : m_children) {
        child->update(dt);
    }

    // 通用包裹（WrapContent）：依据子项包裹自身尺寸
    // 注意：MatchParent 的分配应由父容器完成，这里仅处理 WrapContent 回填
    if (m_layoutW == LayoutDim::WrapContent || m_layoutH == LayoutDim::WrapContent) {
        float maxRight = 0.0f;
        float maxBottom = 0.0f;
        for (auto* child : m_children) {
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

    for (auto* child : m_children) {
        child->render(viewId, renderer);
    }
}

} // namespace Tina::UI
