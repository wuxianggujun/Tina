#include "UIEventSystem.hpp"
#include "UIComponents.hpp"
#include "../core/Log.hpp"

namespace Tina::UI {

UIEventSystem::UIEventSystem()
    : m_dispatcher(std::make_unique<UIEventDispatcher>())
{
}

UIEventSystem::~UIEventSystem()
{
}

void UIEventSystem::setGlobalEventSystem(::Tina::Engine::EventSystem* engineEvents)
{
    m_engineEvents = engineEvents;
    // 若已存在根节点，立即注入到现有树
    if (m_root && m_dispatcher) {
        setDispatcherRecursive(m_root, m_dispatcher.get());
    }
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

        // 递归设置所有节点的 dispatcher，并在有需要时注入事件系统
        setDispatcherRecursive(root, m_dispatcher.get());
    }
}

void UIEventSystem::setDispatcherRecursive(UINode* node, UIEventDispatcher* dispatcher)
{
    if (!node) return;

    // 已移除 setEventDispatcher，直接设置引擎事件系统
    if (m_engineEvents) {
        node->setEventSystem(m_engineEvents);
    }

    for (size_t i = 0; i < node->getChildCount(); ++i) {
        setDispatcherRecursive(node->getChild(i), dispatcher);
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
