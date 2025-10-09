//
// UI 树节点基类
// - 支持层级结构（parent-child）
// - 支持相对/绝对坐标变换
// - 支持可见性、激活状态
// - 提供事件接口（点击、hover等）
//
// 【重要】内存管理模型：
// - UINode 不拥有子节点的所有权，仅持有裸指针
// - addChild() 不会转移所有权，仅建立父子关系
// - 析构函数不会删除子节点
// - 节点的生命周期应由外部（如 Scene）使用智能指针统一管理
//
// 使用示例：
//   auto root = Memory::MakeUnique<UINode>("Root");
//   auto child = Memory::MakeUnique<UIButton>("Child");
//   root->addChild(child.get());  // 仅传递裸指针
//   // Scene 负责持有 root 和 child 的 UniquePtr

#pragma once

#include "../core/Container.hpp"
#include "../core/Math.hpp"
#include <string>
#include <functional>

namespace Tina::UI {

// 前向声明
class UIRenderer;

// 布局尺寸语义（类 Android）
enum class LayoutDim : uint8_t {
    Exact = 0,      // 使用节点自身 size 数值
    MatchParent,    // 填满父容器的可用空间（由父容器设置）
    WrapContent     // 由容器测量子项后回填（或通用节点根据子项包裹）
};

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
    void setSize(float w, float h) { m_size = {w, h}; m_dirty = true; m_layoutW = LayoutDim::Exact; m_layoutH = LayoutDim::Exact; }
    void setWidth(float w) { m_size.x = w; m_dirty = true; m_layoutW = LayoutDim::Exact; }
    void setHeight(float h) { m_size.y = h; m_dirty = true; m_layoutH = LayoutDim::Exact; }
    void setAnchor(Anchor anchor) { m_anchor = anchor; m_dirty = true; }

    // 布局尺寸语义（Android 风格）
    void setWidthMatch() { m_layoutW = LayoutDim::MatchParent; }
    void setHeightMatch() { m_layoutH = LayoutDim::MatchParent; }
    void setWidthWrap() { m_layoutW = LayoutDim::WrapContent; }
    void setHeightWrap() { m_layoutH = LayoutDim::WrapContent; }
    LayoutDim layoutWidth() const { return m_layoutW; }
    LayoutDim layoutHeight() const { return m_layoutH; }

    // 外边距（容器在布局时可考虑）
    void setMargin(float l, float t, float r, float b) { m_marginL = l; m_marginT = t; m_marginR = r; m_marginB = b; }
    float marginLeft() const { return m_marginL; }
    float marginTop() const { return m_marginT; }
    float marginRight() const { return m_marginR; }
    float marginBottom() const { return m_marginB; }

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
    LayoutDim m_layoutW = LayoutDim::Exact;
    LayoutDim m_layoutH = LayoutDim::Exact;
    float m_marginL = 0.0f, m_marginT = 0.0f, m_marginR = 0.0f, m_marginB = 0.0f;

    // 世界变换缓存
    Tina::Math::Vec2 m_worldPos{0, 0};
    bool m_dirty = true;

    // 状态
    bool m_visible = true;
    bool m_enabled = true;
};

} // namespace Tina::UI
