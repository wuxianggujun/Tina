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
#include <atomic>
#include <string>
#include <functional>

// 引擎事件系统前向声明（必须在 namespace Tina::UI 外部）
namespace Tina::Engine { class EventSystem; }

namespace Tina::UI {

// 前向声明
class UIRenderer;
class UILayoutManager;

// 布局尺寸语义（类 Android）
enum class LayoutDim : uint8_t {
    Exact = 0,      // 使用节点自身 size 数值
    MatchParent,    // 填满父容器的可用空间（由父容器设置）
    WrapContent     // 由容器测量子项后回填（或通用节点根据子项包裹）
};

// UI 对齐系统：支持水平和垂直方向独立对齐
// 
// 设计理念：
//   - 水平对齐（HAlign）和垂直对齐（VAlign）独立控制
//   - 可以任意组合，灵活性强
//   - 自动计算position，无需手动计算
// 
// 工作原理：
//   worldPos = parentWorld + anchorOffset(hAlign, vAlign) + position
// 
// 使用示例：
//   // 居中
//   panel->setAlign(HAlign::Center, VAlign::Middle);
//   
//   // 右下角
//   panel->setAlign(HAlign::Right, VAlign::Bottom);
//   
//   // 只设置水平居中，垂直保持原样
//   panel->setHAlign(HAlign::Center);
//   
//   // 只设置垂直居中，水平保持原样
//   panel->setVAlign(VAlign::Middle);

// 水平对齐方式
enum class HAlign : uint8_t {
    Left = 0,    // 左对齐：锚点在父节点左边缘 (0)
    Center,      // 水平居中：锚点在父节点水平中心 (parentW/2)
    Right        // 右对齐：锚点在父节点右边缘 (parentW)
};

// 垂直对齐方式
enum class VAlign : uint8_t {
    Top = 0,     // 顶部对齐：锚点在父节点顶部 (0)
    Middle,      // 垂直居中：锚点在父节点垂直中心 (parentH/2)
    Bottom       // 底部对齐：锚点在父节点底部 (parentH)
};

// 【兼容性】保留旧的Anchor枚举（内部转换为HAlign+VAlign）
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
// ✅ 支持 shared_from_this，允许从 this 获取 shared_ptr
class UINode : public eastl::enable_shared_from_this<UINode> {
public:
    UINode(const std::string& name = "UINode");
    virtual ~UINode();
    
    // === 智能指针支持（新增） ===
    
    /**
     * 获取指向自身的 shared_ptr
     * 注意：只有当节点已被 shared_ptr 管理时才能调用
     */
    Memory::SharedPtr<UINode> getSharedPtr() {
        return shared_from_this();
    }
    
    /**
     * 获取指向自身的 weak_ptr（用于观察）
     */
    Memory::WeakPtr<UINode> getWeakPtr() {
        return weak_from_this();
    }

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
        bumpTreeVersion();
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
    const Tina::Container::Vector<Memory::UniquePtr<UINode>>& getChildren() const {
        return m_children;
    }

    // === 变换（相对于父节点） ===
    
    // 设置位置（支持链式调用）
    UINode* setPosition(float x, float y) { 
        m_position = {x, y}; 
        m_dirty = true;
        m_manualPosition = true;  // 标记为手动设置的position
        return this;
    }
    
    // 设置尺寸（支持链式调用）
    UINode* setSize(float w, float h) { 
        m_size = {w, h}; 
        m_dirty = true; 
        m_layoutW = LayoutDim::Exact; 
        m_layoutH = LayoutDim::Exact;
        
        // ⚠️ 重要：尺寸变化会影响子节点的Anchor偏移
        markChildrenDirty();
        
        // ✅ 智能：如果之前设置过对齐且没有手动设置position，自动重新应用对齐
        if ((m_hAlign != HAlign::Left || m_vAlign != VAlign::Top) && !m_manualPosition) {
            applyAlignment();
        }
        
        return this;
    }
    
    // 设置宽度（支持链式调用）
    UINode* setWidth(float w) { 
        m_size.x = w; 
        m_dirty = true; 
        m_layoutW = LayoutDim::Exact;
        markChildrenDirty();
        
        if ((m_hAlign != HAlign::Left || m_vAlign != VAlign::Top) && !m_manualPosition) {
            applyAlignment();
        }
        
        return this;
    }
    
    // 设置高度（支持链式调用）
    UINode* setHeight(float h) { 
        m_size.y = h; 
        m_dirty = true; 
        m_layoutH = LayoutDim::Exact;
        markChildrenDirty();
        
        if ((m_hAlign != HAlign::Left || m_vAlign != VAlign::Top) && !m_manualPosition) {
            applyAlignment();
        }
        
        return this;
    }
    
    // === 对齐系统（推荐使用，灵活性强） ===
    
    // 设置水平和垂直对齐（支持链式调用）
    UINode* setAlign(HAlign hAlign, VAlign vAlign) {
        m_hAlign = hAlign;
        m_vAlign = vAlign;
        m_dirty = true;
        m_manualPosition = false;
        applyAlignment();
        return this;
    }
    
    // 只设置水平对齐（垂直保持不变）
    UINode* setHAlign(HAlign hAlign) {
        m_hAlign = hAlign;
        m_dirty = true;
        m_manualPosition = false;
        applyAlignment();
        return this;
    }
    
    // 只设置垂直对齐（水平保持不变）
    UINode* setVAlign(VAlign vAlign) {
        m_vAlign = vAlign;
        m_dirty = true;
        m_manualPosition = false;
        applyAlignment();
        return this;
    }
    
    // 设置对齐和额外偏移
    UINode* setAlignWithOffset(HAlign hAlign, VAlign vAlign, float offsetX, float offsetY) {
        m_hAlign = hAlign;
        m_vAlign = vAlign;
        m_dirty = true;
        applyAlignment();
        m_position.x += offsetX;
        m_position.y += offsetY;
        m_manualPosition = false;
        return this;
    }
    
    // 【兼容性】旧的Anchor API（内部转换为HAlign+VAlign）
    UINode* setAnchor(Anchor anchor) {
        // 转换Anchor到HAlign+VAlign
        switch (anchor) {
            case Anchor::TopLeft:       return setAlign(HAlign::Left, VAlign::Top);
            case Anchor::TopCenter:     return setAlign(HAlign::Center, VAlign::Top);
            case Anchor::TopRight:      return setAlign(HAlign::Right, VAlign::Top);
            case Anchor::MiddleLeft:    return setAlign(HAlign::Left, VAlign::Middle);
            case Anchor::MiddleCenter:  return setAlign(HAlign::Center, VAlign::Middle);
            case Anchor::MiddleRight:   return setAlign(HAlign::Right, VAlign::Middle);
            case Anchor::BottomLeft:    return setAlign(HAlign::Left, VAlign::Bottom);
            case Anchor::BottomCenter:  return setAlign(HAlign::Center, VAlign::Bottom);
            case Anchor::BottomRight:   return setAlign(HAlign::Right, VAlign::Bottom);
        }
        return this;
    }

    // === 流式布局API（便捷方法） ===
    
    // 完全居中
    UINode* center() { return setAlign(HAlign::Center, VAlign::Middle); }
    
    // 水平居中（垂直保持不变）
    UINode* centerH() { return setHAlign(HAlign::Center); }
    
    // 垂直居中（水平保持不变）
    UINode* centerV() { return setVAlign(VAlign::Middle); }
    
    // 快速对齐（组合方式）
    UINode* alignTop() { return setAlign(HAlign::Center, VAlign::Top); }
    UINode* alignBottom() { return setAlign(HAlign::Center, VAlign::Bottom); }
    UINode* alignLeft() { return setAlign(HAlign::Left, VAlign::Middle); }
    UINode* alignRight() { return setAlign(HAlign::Right, VAlign::Middle); }
    UINode* alignTopLeft() { return setAlign(HAlign::Left, VAlign::Top); }
    UINode* alignTopRight() { return setAlign(HAlign::Right, VAlign::Top); }
    UINode* alignBottomLeft() { return setAlign(HAlign::Left, VAlign::Bottom); }
    UINode* alignBottomRight() { return setAlign(HAlign::Right, VAlign::Bottom); }
    
    // 带边距的对齐
    UINode* alignBottomRightWithMargin(float marginX, float marginY) {
        return setAlignWithOffset(HAlign::Right, VAlign::Bottom, -marginX, -marginY);
    }
    
    UINode* alignTopLeftWithMargin(float marginX, float marginY) {
        return setAlignWithOffset(HAlign::Left, VAlign::Top, marginX, marginY);
    }

    // 布局尺寸语义（Android 风格）
    UINode* setWidthMatch() { m_layoutW = LayoutDim::MatchParent; return this; }
    UINode* setHeightMatch() { m_layoutH = LayoutDim::MatchParent; return this; }
    UINode* setWidthWrap() { m_layoutW = LayoutDim::WrapContent; return this; }
    UINode* setHeightWrap() { m_layoutH = LayoutDim::WrapContent; return this; }
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
    HAlign getHAlign() const { return m_hAlign; }
    VAlign getVAlign() const { return m_vAlign; }

    // === 世界坐标（自动计算） ===
    Tina::Math::Vec2 getWorldPosition();
    Tina::Math::Vec2 getWorldSize() { return m_size; } // 暂不支持缩放

    // === 状态（支持链式调用） ===
    UINode* setVisible(bool v) { if (m_visible != v) { m_visible = v; bumpTreeVersion(); } return this; }
    UINode* setEnabled(bool e) { if (m_enabled != e) { m_enabled = e; bumpTreeVersion(); } return this; }
    bool isVisible() const { return m_visible; }
    bool isEnabled() const { return m_enabled; }
    // 交互能力开关（用于命中与事件过滤）
    UINode* setInteractable(bool i) { if (m_interactable != i) { m_interactable = i; bumpTreeVersion(); } return this; }
    bool isInteractable() const { return m_interactable; }
    UINode* setClickable(bool v) { if (m_clickable != v) { m_clickable = v; bumpTreeVersion(); } return this; }
    bool isClickable() const { return m_clickable; }
    UINode* setHoverable(bool v) { if (m_hoverable != v) { m_hoverable = v; bumpTreeVersion(); } return this; }
    bool isHoverable() const { return m_hoverable; }
    UINode* setFocusable(bool v) { if (m_focusable != v) { m_focusable = v; bumpTreeVersion(); } return this; }
    bool isFocusable() const { return m_focusable; }
    // 呈现顺序（越大越上层）
    UINode* setZIndex(int z) { if (m_zIndex != z) { m_zIndex = z; bumpTreeVersion(); } return this; }
    int zIndex() const { return m_zIndex; }

    // === 渲染分层（UIRenderer 层号） ===
    void setLayer(int layer) { m_layer = layer; }
    int layer() const { return m_layer; }
    // 便捷：同时设置事件命中顺序与渲染层
    void setZAndLayer(int z, int layer) { setZIndex(z); setLayer(layer); }

    // === 点测试（用于事件分发） ===
    bool containsPoint(float worldX, float worldY);

    // === 布局系统 ===
    // 请求重新布局（标记布局失效，使用布局管理器处理）
    void requestLayout();

    // 立即执行布局（通过布局管理器处理依赖）
    void performLayoutNow();

    // 检查布局是否需要更新
    bool needsLayout() const { return m_layoutDirty; }

    // 标记布局已完成（由布局管理器调用）
    void markLayoutClean() { m_layoutDirty = false; }

    // === 渲染与更新 ===
    void update(float dt); // 递归更新自身及子节点（不包括布局）
    void render(uint16_t viewId, UIRenderer& renderer); // 递归渲染

    // === 事件回调（子类可覆盖） ===
    virtual void onMouseEnter() {}
    virtual void onMouseLeave() {}
    virtual void onMouseDown(float x, float y) { (void)x; (void)y; }
    virtual void onMouseUp(float x, float y) { (void)x; (void)y; }
    virtual void onClick() {}
    // 新增：鼠标滚轮事件（dx/dy 为像素/刻度）
    virtual void onMouseWheel(float dx, float dy) { (void)dx; (void)dy; }

    // === 窗口尺寸变化回调（框架自动调用） ===
    // 默认实现：递归通知所有子节点
    // 子类可覆盖以实现自定义布局逻辑（如重新计算居中位置）
    virtual void onWindowSizeChanged(int width, int height) {
        // ⚠️ 重要：标记所有子节点为dirty，让Anchor偏移重新计算
        // 因为父节点尺寸变化会影响Anchor的锚点位置
        for (auto& child : m_children) {
            if (child) {
                child->m_dirty = true;  // 标记为dirty，强制重新计算世界坐标
                child->onWindowSizeChanged(width, height);
            }
        }
    }

    // === 名称（调试用） ===
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    // === 引擎事件系统支持 ===
    void setEventSystem(Tina::Engine::EventSystem* eventSystem) { m_eventSystem = eventSystem; }
    Tina::Engine::EventSystem* eventSystem() const { return m_eventSystem; }

    // === 布局管理器支持 ===
    void setLayoutManager(UILayoutManager* layoutManager) { m_layoutManager = layoutManager; }
    UILayoutManager* layoutManager() const { return m_layoutManager; }

protected:
    // === 子类可覆盖的回调 ===

    // 布局回调：计算并设置子节点的 position 和 size
    // 在布局失效后，由布局管理器调用
    // 注意：只负责布局子节点，不要在这里做动画或状态更新
    virtual void onLayout() {}

    // 友元类：允许布局管理器访问内部状态
    friend class UILayoutManager;

    // 更新回调：处理动画、状态更新等（不包括布局）
    virtual void onUpdate(float dt) {}

    // 渲染回调：绘制节点自身
    virtual void onRender(uint16_t viewId, UIRenderer& renderer) {}

private:
    void updateWorldTransform();
    Tina::Math::Vec2 anchorOffset() const;
    
    // 标记所有子节点为dirty（当父节点尺寸变化时调用）
    void markChildrenDirty() {
        for (auto& child : m_children) {
            if (child) child->m_dirty = true;
        }
    }
    
    // 根据HAlign和VAlign自动计算并应用对齐位置
    void applyAlignment() {
        // 水平对齐
        switch (m_hAlign) {
            case HAlign::Left:   m_position.x = 0; break;
            case HAlign::Center: m_position.x = -m_size.x * 0.5f; break;
            case HAlign::Right:  m_position.x = -m_size.x; break;
        }
        
        // 垂直对齐
        switch (m_vAlign) {
            case VAlign::Top:    m_position.y = 0; break;
            case VAlign::Middle: m_position.y = -m_size.y * 0.5f; break;
            case VAlign::Bottom: m_position.y = -m_size.y; break;
        }
    }

protected:
    std::string m_name;

    // 层级（新版：拥有子节点所有权）
    UINode* m_parent = nullptr;  // 弱引用，不拥有
    Tina::Container::Vector<Memory::UniquePtr<UINode>> m_children;  // 拥有所有权

    // 局部变换
    Tina::Math::Vec2 m_position{0, 0}; // 相对父节点的偏移
    Tina::Math::Vec2 m_size{100, 100};
    HAlign m_hAlign = HAlign::Left;    // 水平对齐方式
    VAlign m_vAlign = VAlign::Top;     // 垂直对齐方式
    bool m_manualPosition = false;     // 是否手动设置了position（用于智能对齐重新应用）
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
    bool m_interactable = true;
    bool m_clickable = true;
    bool m_hoverable = true;
    bool m_focusable = false;
    int m_zIndex = 0;
    int m_layer = 0;  // 渲染层（较大者在上），由 UIRenderer 控制

    // 引擎事件系统（由场景设置）
    Tina::Engine::EventSystem* m_eventSystem = nullptr;

    // 布局管理器（由场景设置）
    UILayoutManager* m_layoutManager = nullptr;

    // ==================== 结构变更版本（用于命中索引置脏） ====================
public:
    static uint64_t treeVersion();
    static void bumpTreeVersion();
private:
    static std::atomic<uint64_t> s_treeVersion;
};

} // namespace Tina::UI
