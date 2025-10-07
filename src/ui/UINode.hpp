//
// UI 树节点基类
// - 支持层级结构（parent-child）
// - 支持相对/绝对坐标变换
// - 支持可见性、激活状态
// - 提供事件接口（点击、hover等）

#pragma once

#include "../core/Container.hpp"
#include "../core/Math.hpp"
#include <string>
#include <functional>

namespace Tina::UI {

// 前向声明
class UIRenderer;

// UI 锚点：决定子节点相对父节点的对齐方式
enum class Anchor : uint8_t {
    TopLeft = 0,
    TopCenter,
    TopRight,
    MiddleLeft,
    MiddleCenter,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

// UI 节点基类
class UINode {
public:
    UINode(const std::string& name = "UINode");
    virtual ~UINode();

    // === 层级管理 ===
    void addChild(UINode* child);
    void removeChild(UINode* child);
    void removeFromParent();
    UINode* getParent() const { return m_parent; }
    const Tina::Container::Vector<UINode*>& getChildren() const { return m_children; }

    // === 变换（相对于父节点） ===
    void setPosition(float x, float y) { m_position = {x, y}; m_dirty = true; }
    void setSize(float w, float h) { m_size = {w, h}; m_dirty = true; }
    void setAnchor(Anchor anchor) { m_anchor = anchor; m_dirty = true; }

    Tina::Math::Vec2 getPosition() const { return m_position; }
    Tina::Math::Vec2 getSize() const { return m_size; }
    Anchor getAnchor() const { return m_anchor; }

    // === 世界坐标（自动计算） ===
    Tina::Math::Vec2 getWorldPosition();
    Tina::Math::Vec2 getWorldSize() { return m_size; } // 暂不支持缩放

    // === 状态 ===
    void setVisible(bool v) { m_visible = v; }
    void setEnabled(bool e) { m_enabled = e; }
    bool isVisible() const { return m_visible; }
    bool isEnabled() const { return m_enabled; }

    // === 点测试（用于事件分发） ===
    bool containsPoint(float worldX, float worldY);

    // === 渲染与更新 ===
    void update(float dt); // 递归更新自身及子节点
    void render(uint16_t viewId, UIRenderer& renderer); // 递归渲染

    // === 事件回调（子类可覆盖，或通过函数对象绑定） ===
    virtual void onMouseEnter() {}
    virtual void onMouseLeave() {}
    virtual void onClick() {}

    // === 名称（调试用） ===
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

protected:
    // 子类实现具体绘制逻辑
    virtual void onUpdate(float dt) {}
    virtual void onRender(uint16_t viewId, UIRenderer& renderer) {}

private:
    void updateWorldTransform();
    Tina::Math::Vec2 anchorOffset() const;

protected:
    std::string m_name;

    // 层级
    UINode* m_parent = nullptr;
    Tina::Container::Vector<UINode*> m_children;

    // 局部变换
    Tina::Math::Vec2 m_position{0, 0}; // 相对父节点的偏移
    Tina::Math::Vec2 m_size{100, 100};
    Anchor m_anchor = Anchor::TopLeft;

    // 世界变换缓存
    Tina::Math::Vec2 m_worldPos{0, 0};
    bool m_dirty = true;

    // 状态
    bool m_visible = true;
    bool m_enabled = true;
};

} // namespace Tina::UI
