#include "UIEventSystem.hpp"
#include "../core/Log.hpp"

namespace Tina::UI {

UIEventSystem::UIEventSystem()
    : m_dispatcher(std::make_unique<UIEventDispatcher>())
{
}

UIEventSystem::~UIEventSystem()
{
}

void UIEventSystem::setRoot(UINode* root)
{
    m_root = root;
    m_hoveredNode = nullptr;
    m_pressedNode = nullptr;
    m_mouseDown = false;
    m_mouseDownPrev = false;

    if (m_dispatcher) {
        m_dispatcher->setRoot(root);
    }
}

void UIEventSystem::updateMouse(float mouseX, float mouseY, bool mouseDown)
{
    m_mouseX = mouseX;
    m_mouseY = mouseY;
    m_mouseDownPrev = m_mouseDown;
    m_mouseDown = mouseDown;
}

void UIEventSystem::processEvents()
{
    if (!m_root || !m_dispatcher) return;
    // 统一使用高级事件分发（捕获/目标/冒泡）
    m_dispatcher->handleMouseInput(m_mouseX, m_mouseY, m_mouseDown);
}

} // namespace Tina::UI

