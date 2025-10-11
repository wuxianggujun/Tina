//
// UI 树节点基类
// - 支持层级结构（parent-child）
// - 支持相对/绝对坐标变换
// - 支持可见性、激活状态
// - 提供事件接口（点击、hover等）
//
// 【重要】内存管理模型（优化版）：
// - UINode 拥有子节点的所有权（通过 UniquePtr）
// - addChild() 转移所有权到父节点
// - 析构函数自动清理所有子节点
// - 简化了Scene的UI管理，无需额外容器
//
// 使用示例：
//   auto root = Memory::MakeUnique<UINode>("Root");
//   auto child = root->createChild<UIButton>("Child");
//   child->setText("Button");
//   // 或者：
//   auto btn = Memory::MakeUnique<UIButton>("Btn");
//   UIButton* btnPtr = root->addChild(std::move(btn));

#pragma once

#include "../core/Container.hpp"
#include "../core/Math.hpp"
#include "../core/Memory.hpp"
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

    // === 层级管理（新版：拥有所有权） ===
    
    // 添加子节点（转移所有权）
    template<typename T>
    T* addChild(Memory::UniquePtr<T> child) {
        static_assert(std::is_base_of<UINode, T>::value, "T must derive from UINode");
        if (!child || child.get() == this) return nullptr;
        
        // 如果已有父节点，先移除
        if (child->m_parent) {
            child->m_parent->removeChild(child.get());
        }
        
        T* ptr = child.get();
        child->m_parent = this;
        child->m_dirty = true;
        m_children.push_back(std::move(child));
        return ptr;
    }
    
    // 创建并添加子节点（便捷方法）
    template<typename T, typename... Args>
    T* createChild(Args&&... args) {
        auto child = Memory::MakeUnique<T>(std::forward<Args>(args)...);
        return addChild(std::move(child));
    }
    
    // 移除子节点（返回所有权）
    Memory::UniquePtr<UINode> removeChild(UINode* child);
    
    // 从父节点移除自己
    void removeFromParent();
    
    // 查找子节点
    UINode* findChild(const std::string& name) const;
    template<typename T>
    T* findChild(const std::string& name) const {
        UINode* node = findChild(name);
        return node ? dynamic_cast<T*>(node) : nullptr;
    }
    
    // 获取父节点和子节点
    UINode* getParent() const { return m_parent; }
    size_t getChildCount() const { return m_children.size(); }
    UINode* getChild(size_t index) const {
        return (index < m_children.size()) ? m_children[index].get() : nullptr;
    }

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

    // === 布局系统 ===
    // 请求重新布局（标记布局失效，在下次访问前自动执行）
    void requestLayout();

    // 立即执行布局（强制同步布局，通常不需要手动调用）
    void performLayoutNow();

    // 检查布局是否需要更新
    bool needsLayout() const { return m_layoutDirty; }

    // === 渲染与更新 ===
    void update(float dt); // 递归更新自身及子节点（不包括布局）
    void render(uint16_t viewId, UIRenderer& renderer); // 递归渲染

    // === 事件回调（子类可覆盖，或通过函数对象绑定） ===
    virtual void onMouseEnter() {}
    virtual void onMouseLeave() {}
    virtual void onClick() {}

    // === 窗口尺寸变化回调（框架自动调用） ===
    // 默认实现：递归通知所有子节点
    // 子类可覆盖以实现自定义布局逻辑（如重新计算居中位置）
    virtual void onWindowSizeChanged(int width, int height) {
        // 默认递归通知所有子节点
        for (auto& child : m_children) {
            if (child) {
                child->onWindowSizeChanged(width, height);
            }
        }
    }

    // === 名称（调试用） ===
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

protected:
    // === 子类可覆盖的回调 ===

    // 布局回调：计算并设置子节点的 position 和 size
    // 在布局失效后，首次访问坐标前自动调用
    // 注意：只负责布局子节点，不要在这里做动画或状态更新
    virtual void onLayout() {}

    // 更新回调：处理动画、状态更新等（不包括布局）
    virtual void onUpdate(float dt) {}

    // 渲染回调：绘制节点自身
    virtual void onRender(uint16_t viewId, UIRenderer& renderer) {}

private:
    void updateWorldTransform();
    Tina::Math::Vec2 anchorOffset() const;

protected:
    std::string m_name;

    // 层级（新版：拥有子节点所有权）
    UINode* m_parent = nullptr;  // 弱引用，不拥有
    Tina::Container::Vector<Memory::UniquePtr<UINode>> m_children;  // 拥有所有权

    // 局部变换
    Tina::Math::Vec2 m_position{0, 0}; // 相对父节点的偏移
    Tina::Math::Vec2 m_size{100, 100};
    Anchor m_anchor = Anchor::TopLeft;
    LayoutDim m_layoutW = LayoutDim::Exact;
    LayoutDim m_layoutH = LayoutDim::Exact;
    float m_marginL = 0.0f, m_marginT = 0.0f, m_marginR = 0.0f, m_marginB = 0.0f;

    // 世界变换缓存
    Tina::Math::Vec2 m_worldPos{0, 0};
    bool m_dirty = true;        // 坐标脏标记（需要重新计算世界坐标）

    // 布局系统
    bool m_layoutDirty = true;  // 布局脏标记（需要重新布局子节点）

    // 状态
    bool m_visible = true;
    bool m_enabled = true;
};

} // namespace Tina::UI
