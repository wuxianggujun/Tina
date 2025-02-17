#pragma once

#include "tina/event/Event.hpp"
#include "tina/math/Rect.hpp"
#include "tina/view/View.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Tina {

    /**
 * @brief UI组件的布局约束
 */
struct LayoutConstraints {
    float minWidth = 0;      ///< 最小宽度
    float minHeight = 0;     ///< 最小高度
    float maxWidth = 0;      ///< 最大宽度(0表示无限制)
    float maxHeight = 0;     ///< 最大高度(0表示无限制)
    float padding = 5.0f;    ///< 内边距
    float margin = 5.0f;     ///< 外边距
};

/**
 * @brief UI组件的对齐方式
 */
enum class Alignment {
    None,           ///< 无对齐
    Left,           ///< 左对齐
    Center,         ///< 居中对齐
    Right,          ///< 右对齐
    Top,            ///< 顶部对齐
    Bottom,         ///< 底部对齐
    TopLeft,        ///< 左上对齐
    TopRight,       ///< 右上对齐
    BottomLeft,     ///< 左下对齐
    BottomRight     ///< 右下对齐
};

/**
 * @brief UI组件基类
 */
class UIWidget : public std::enable_shared_from_this<UIWidget> {
public:
    /**
     * @brief 构造函数
     * @param name 组件名称
     */
    explicit UIWidget(const std::string& name = "UIWidget");
    
    /**
     * @brief 析构函数
     */
    virtual ~UIWidget() = default;
    
    /**
     * @brief 获取组件名称
     * @return 组件名称
     */
    const std::string& getName() const { return m_name; }
    
    /**
     * @brief 设置组件名称
     * @param name 组件名称
     */
    void setName(const std::string& name) { m_name = name; }
    
    /**
     * @brief 获取父组件
     * @return 父组件
     */
    std::shared_ptr<UIWidget> getParent() const { return m_parent.lock(); }
    
    /**
     * @brief 获取所属视图
     * @return 所属视图
     */
    std::shared_ptr<View> getView() const { return m_view.lock(); }
    
    /**
     * @brief 设置所属视图
     * @param view 所属视图
     */
    void setView(const std::shared_ptr<View>& view);
    
    /**
     * @brief 添加子组件
     * @param child 子组件
     */
    void addChild(const std::shared_ptr<UIWidget>& child);
    
    /**
     * @brief 移除子组件
     * @param child 子组件
     */
    void removeChild(const std::shared_ptr<UIWidget>& child);
    
    /**
     * @brief 获取子组件列表
     * @return 子组件列表
     */
    const std::vector<std::shared_ptr<UIWidget>>& getChildren() const { return m_children; }
    
    /**
     * @brief 设置布局约束
     * @param constraints 布局约束
     */
    void setConstraints(const LayoutConstraints& constraints) { m_constraints = constraints; }
    
    /**
     * @brief 获取布局约束
     * @return 布局约束
     */
    const LayoutConstraints& getConstraints() const { return m_constraints; }
    
    /**
     * @brief 设置对齐方式
     * @param alignment 对齐方式
     */
    void setAlignment(Alignment alignment) { m_alignment = alignment; }
    
    /**
     * @brief 获取对齐方式
     * @return 对齐方式
     */
    Alignment getAlignment() const { return m_alignment; }
    
    /**
     * @brief 设置位置
     * @param position 位置
     */
    void setPosition(const glm::vec2& position) { m_bounds.setPosition(position); }
    
    /**
     * @brief 获取位置
     * @return 位置
     */
    const glm::vec2& getPosition() const { return m_bounds.getPosition(); }
    
    /**
     * @brief 设置大小
     * @param size 大小
     */
    void setSize(const glm::vec2& size) { m_bounds.setSize(size); }
    
    /**
     * @brief 获取大小
     * @return 大小
     */
    const glm::vec2& getSize() const { return m_bounds.getSize(); }
    
    /**
     * @brief 获取边界矩形
     * @return 边界矩形
     */
    const Rect& getBounds() const { return m_bounds; }
    
    /**
     * @brief 设置是否可见
     * @param visible 是否可见
     */
    void setVisible(bool visible) { m_visible = visible; }
    
    /**
     * @brief 是否可见
     * @return 是否可见
     */
    bool isVisible() const { return m_visible; }
    
    /**
     * @brief 设置是否启用
     * @param enabled 是否启用
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }
    
    /**
     * @brief 是否启用
     * @return 是否启用
     */
    bool isEnabled() const { return m_enabled; }
    
    /**
     * @brief 设置是否可获得焦点
     * @param focusable 是否可获得焦点
     */
    void setFocusable(bool focusable) { m_focusable = focusable; }
    
    /**
     * @brief 是否可获得焦点
     * @return 是否可获得焦点
     */
    bool isFocusable() const { return m_focusable; }
    
    /**
     * @brief 是否有焦点
     * @return 是否有焦点
     */
    bool isFocused() const { return m_focused; }
    
    /**
     * @brief 设置焦点
     * @param focused 是否获得焦点
     * @return 是否成功设置焦点
     */
    bool setFocus(bool focused);
    
    /**
     * @brief 更新组件
     * @param deltaTime 时间增量
     */
    virtual void update(float deltaTime);
    
    /**
     * @brief 绘制组件
     */
    virtual void draw();
    
protected:
    /**
     * @brief 处理组件附加到视图
     */
    virtual void onAttach();
    
    /**
     * @brief 处理组件从视图分离
     */
    virtual void onDetach();
    
    /**
     * @brief 处理鼠标按钮事件
     * @param button 按钮类型
     * @param pressed 是否按下
     * @param x 鼠标x坐标
     * @param y 鼠标y坐标
     * @return 是否处理了事件
     */
    virtual bool onMouseButton(Event::MouseButton button, bool pressed, float x, float y);
    
    /**
     * @brief 处理鼠标移动事件
     * @param x 鼠标x坐标
     * @param y 鼠标y坐标
     * @param dx x方向移动距离
     * @param dy y方向移动距离
     * @return 是否处理了事件
     */
    virtual bool onMouseMove(float x, float y, float dx, float dy);
    
    /**
     * @brief 处理鼠标滚轮事件
     * @param x 滚轮x偏移
     * @param y 滚轮y偏移
     * @return 是否处理了事件
     */
    virtual bool onMouseScroll(float x, float y);
    
    /**
     * @brief 处理键盘事件
     * @param key 按键码
     * @param pressed 是否按下
     * @return 是否处理了事件
     */
    virtual bool onKeyboard(Event::KeyCode key, bool pressed);
    
    /**
     * @brief 处理字符输入事件
     * @param codepoint Unicode码点
     * @return 是否处理了事件
     */
    virtual bool onChar(unsigned int codepoint);
    
    /**
     * @brief 处理焦点变化事件
     * @param focused 是否获得焦点
     * @return 是否处理了事件
     */
    virtual bool onFocus(bool focused);
    
    /**
     * @brief 处理大小变化事件
     * @param width 新宽度
     * @param height 新高度
     */
    virtual void onResize(float width, float height);
    bool containsPoint(float x, float y) const;
    std::shared_ptr<UIWidget> findChildAt(float x, float y);

private:
    std::string m_name;                                  ///< 组件名称
    std::weak_ptr<UIWidget> m_parent;                    ///< 父组件
    std::weak_ptr<View> m_view;                          ///< 所属视图
    std::vector<std::shared_ptr<UIWidget>> m_children;   ///< 子组件列表
    LayoutConstraints m_constraints;                      ///< 布局约束
    Alignment m_alignment = Alignment::None;             ///< 对齐方式
    Rect m_bounds;                                       ///< 边界矩形
    bool m_visible = true;                               ///< 是否可见
    bool m_enabled = true;                               ///< 是否启用
    bool m_focusable = false;                            ///< 是否可获得焦点
    bool m_focused = false;                              ///< 是否有焦点
    
    friend class View;
};

} // namespace Tina 
