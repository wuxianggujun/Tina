#include "tina/ui/UIWidget.hpp"
#include "tina/log/Logger.hpp"

namespace Tina {

UIWidget::UIWidget(const std::string& name) : m_name(name) {
    TINA_LOG_DEBUG("Created UIWidget '{}'", name);
}

void UIWidget::setView(const std::shared_ptr<View>& view) {
    if (m_view.lock() != view) {
        if (auto oldView = m_view.lock()) {
            onDetach();
        }
        
        m_view = view;
        
        if (view) {
            onAttach();
        }
        
        // 递归设置子组件的视图
        for (auto& child : m_children) {
            child->setView(view);
        }
    }
}

void UIWidget::addChild(const std::shared_ptr<UIWidget>& child) {
    if (!child) return;
    
    // 如果child已经有父组件，先从原父组件中移除
    if (auto parent = child->getParent()) {
        parent->removeChild(child);
    }
    
    child->m_parent = shared_from_this();
    child->setView(getView());
    m_children.push_back(child);
    
    TINA_LOG_DEBUG("Added child '{}' to UIWidget '{}'", child->getName(), getName());
}

void UIWidget::removeChild(const std::shared_ptr<UIWidget>& child) {
    if (!child) return;
    
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        child->setView(nullptr);
        child->m_parent.reset();
        m_children.erase(it);
        
        TINA_LOG_DEBUG("Removed child '{}' from UIWidget '{}'", child->getName(), getName());
    }
}

bool UIWidget::setFocus(bool focused) {
    if (m_focused == focused) return true;
    
    if (focused && !m_focusable) return false;
    
    if (onFocus(focused)) {
        m_focused = focused;
        return true;
    }
    
    return false;
}

void UIWidget::update(float deltaTime) {
    if (!m_visible || !m_enabled) return;
    
    // 更新子组件
    for (auto& child : m_children) {
        child->update(deltaTime);
    }
}

void UIWidget::draw() {
    if (!m_visible) return;
    
    // 绘制子组件
    for (auto& child : m_children) {
        child->draw();
    }
}

void UIWidget::onAttach() {
    TINA_LOG_DEBUG("UIWidget '{}' attached to view", getName());
}

void UIWidget::onDetach() {
    TINA_LOG_DEBUG("UIWidget '{}' detached from view", getName());
}

bool UIWidget::onMouseButton(Event::MouseButton button, bool pressed, float x, float y) {
    if (!m_visible || !m_enabled) return false;
    
    // 从后向前遍历子组件，让最上层的组件先处理事件
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        if ((*it)->onMouseButton(button, pressed, x, y)) {
            return true;
        }
    }
    
    // 检查点击是否在组件范围内
    if (m_bounds.contains({x, y})) {
        if (m_focusable && pressed) {
            setFocus(true);
        }
        return true;
    }
    
    return false;
}

bool UIWidget::onMouseMove(float x, float y, float dx, float dy) {
    if (!m_visible || !m_enabled) return false;
    
    // 从后向前遍历子组件
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        if ((*it)->onMouseMove(x, y, dx, dy)) {
            return true;
        }
    }
    
    return m_bounds.contains({x, y});
}

bool UIWidget::onMouseScroll(float x, float y) {
    if (!m_visible || !m_enabled) return false;
    
    // 从后向前遍历子组件
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        if ((*it)->onMouseScroll(x, y)) {
            return true;
        }
    }
    
    return false;
}

bool UIWidget::onKeyboard(Event::KeyCode key, bool pressed) {
    if (!m_visible || !m_enabled || !m_focused) return false;
    
    // 从后向前遍历子组件
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        if ((*it)->onKeyboard(key, pressed)) {
            return true;
        }
    }
    
    return false;
}

bool UIWidget::onChar(unsigned int codepoint) {
    if (!m_visible || !m_enabled || !m_focused) return false;
    
    // 从后向前遍历子组件
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        if ((*it)->onChar(codepoint)) {
            return true;
        }
    }
    
    return false;
}

bool UIWidget::onFocus(bool focused) {
    TINA_LOG_DEBUG("UIWidget '{}' focus changed to {}", getName(), focused);
    return true;
}

void UIWidget::onResize(float width, float height) {
    // 根据约束调整大小
    float newWidth = std::max(m_constraints.minWidth,
        std::min(width, m_constraints.maxWidth > 0 ? m_constraints.maxWidth : width));
    float newHeight = std::max(m_constraints.minHeight,
        std::min(height, m_constraints.maxHeight > 0 ? m_constraints.maxHeight : height));
    
    setSize({newWidth, newHeight});
    
    // 根据对齐方式调整位置
    switch (m_alignment) {
        case Alignment::Center:
            setPosition({(width - newWidth) * 0.5f, (height - newHeight) * 0.5f});
            break;
        case Alignment::Right:
            setPosition({width - newWidth - m_constraints.margin, m_bounds.getY()});
            break;
        case Alignment::Bottom:
            setPosition({m_bounds.getX(), height - newHeight - m_constraints.margin});
            break;
        case Alignment::TopRight:
            setPosition({width - newWidth - m_constraints.margin, m_constraints.margin});
            break;
        case Alignment::BottomLeft:
            setPosition({m_constraints.margin, height - newHeight - m_constraints.margin});
            break;
        case Alignment::BottomRight:
            setPosition({width - newWidth - m_constraints.margin,
                height - newHeight - m_constraints.margin});
            break;
        case Alignment::TopLeft:
        case Alignment::None:
        default:
            setPosition({m_constraints.margin, m_constraints.margin});
            break;
    }
    
    // 递归调整子组件大小
    for (auto& child : m_children) {
        child->onResize(newWidth - m_constraints.padding * 2,
            newHeight - m_constraints.padding * 2);
    }
    
    TINA_LOG_DEBUG("UIWidget '{}' resized to {}x{}", getName(), newWidth, newHeight);
}

bool UIWidget::containsPoint(float x, float y) const {
    return m_bounds.contains({x, y});
}

std::shared_ptr<UIWidget> UIWidget::findChildAt(float x, float y) {
    // 从前到后检查子组件(顶层优先)
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        auto& child = *it;
        if (child->isVisible() && child->containsPoint(x, y)) {
            return child;
        }
    }
    return nullptr;
}

} // namespace Tina 