//
// Created by wuxianggujun on 2025/2/27.
//

#include "tina/event/InputManager.hpp"
#include "tina/event/EventManager.hpp"
#include "tina/core/Engine.hpp"
#include "tina/log/Log.hpp"

namespace Tina
{
    void InputManager::initialize()
    {
        TINA_ENGINE_INFO("Initializing InputManager");
        
        // 注册事件监听器
        auto eventManager = Engine::getInstance()->getEventManager();
        
        // 监听键盘事件
        eventManager->addListener(
            [this](const EventPtr& event) {
                auto keyCode = event->key.key;
                int action = event->key.action;
                
                // 将Event::KeyCode转换为InputManager::KeyCode
                // 暂时简单处理，只取一些常用按键
                KeyCode ourKeyCode = KeyCode::Unknown;
                
                // 字母键
                if (keyCode >= Event::KeyCode::A && keyCode <= Event::KeyCode::Z) {
                    ourKeyCode = static_cast<KeyCode>(static_cast<int>(keyCode));
                }
                // 数字键
                else if (keyCode >= Event::KeyCode::Key0 && keyCode <= Event::KeyCode::Key9) {
                    ourKeyCode = static_cast<KeyCode>(static_cast<int>(keyCode));
                }
                // 功能键
                else if (keyCode == Event::KeyCode::Space) ourKeyCode = KeyCode::Space;
                else if (keyCode == Event::KeyCode::Enter) ourKeyCode = KeyCode::Enter;
                else if (keyCode == Event::KeyCode::Tab) ourKeyCode = KeyCode::Tab;
                else if (keyCode == Event::KeyCode::Escape) ourKeyCode = KeyCode::Escape;
                else if (keyCode == Event::KeyCode::Backspace) ourKeyCode = KeyCode::Backspace;
                // 方向键
                else if (keyCode == Event::KeyCode::Left) ourKeyCode = KeyCode::Left;
                else if (keyCode == Event::KeyCode::Right) ourKeyCode = KeyCode::Right;
                else if (keyCode == Event::KeyCode::Up) ourKeyCode = KeyCode::Up;
                else if (keyCode == Event::KeyCode::Down) ourKeyCode = KeyCode::Down;
                // 修饰键
                else if (keyCode == Event::KeyCode::LeftShift || keyCode == Event::KeyCode::RightShift) 
                    ourKeyCode = KeyCode::Shift;
                else if (keyCode == Event::KeyCode::LeftControl || keyCode == Event::KeyCode::RightControl) 
                    ourKeyCode = KeyCode::Control;
                else if (keyCode == Event::KeyCode::LeftAlt || keyCode == Event::KeyCode::RightAlt) 
                    ourKeyCode = KeyCode::Alt;
                
                // 更新按键状态
                if (ourKeyCode != KeyCode::Unknown) {
                    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                        setKeyState(ourKeyCode, true);
                    } else if (action == GLFW_RELEASE) {
                        setKeyState(ourKeyCode, false);
                    }
                }
            },
            Event::Key
        );
        
        // 监听鼠标按钮事件
        eventManager->addListener(
            [this](const EventPtr& event) {
                auto button = event->mouseButton.button;
                int action = event->mouseButton.action;
                
                // 将Event::MouseButton转换为InputManager::MouseButton
                MouseButton ourButton;
                
                // 正确映射GLFW的鼠标按钮到我们的MouseButton枚举
                switch (button) {
                    case Event::MouseButton::Left:
                        ourButton = MouseButton::Left;
                        break;
                    case Event::MouseButton::Right:
                        ourButton = MouseButton::Right;
                        break;
                    case Event::MouseButton::Middle:
                        ourButton = MouseButton::Middle;
                        break;
                    default:
                        // 对于未知按钮，默认使用Left（或者也可以忽略这个事件）
                        ourButton = MouseButton::Left;
                        break;
                }
                
                // 使用GLFW的按键动作常量
                if (action == GLFW_PRESS) {
                    TINA_ENGINE_TRACE("鼠标按钮按下：{}", static_cast<int>(ourButton));
                    setMouseButtonState(ourButton, true);
                } else if (action == GLFW_RELEASE) {
                    TINA_ENGINE_TRACE("鼠标按钮释放：{}", static_cast<int>(ourButton));
                    setMouseButtonState(ourButton, false);
                }
            },
            Event::MouseButtonEvent
        );
        
        // 监听鼠标移动事件
        eventManager->addListener(
            [this](const EventPtr& event) {
                Math::Vec2f position(static_cast<float>(event->mousePos.x), 
                                    static_cast<float>(event->mousePos.y));
                setMousePosition(position);
            },
            Event::MouseMove
        );
        
        // 监听鼠标滚轮事件
        eventManager->addListener(
            [this](const EventPtr& event) {
                Math::Vec2f scrollDelta(static_cast<float>(event->mouseScroll.xoffset), 
                                       static_cast<float>(event->mouseScroll.yoffset));
                setScrollDelta(scrollDelta);
            },
            Event::MouseScroll
        );
        
        // 初始化状态
        reset();
    }

    void InputManager::shutdown()
    {
        TINA_ENGINE_INFO("Shutting down InputManager");
        reset();
    }
    
    void InputManager::update()
    {
        // 保存前一帧的状态
        m_prevKeyStates = m_keyStates;
        m_prevMouseButtonStates = m_mouseButtonStates;
        m_prevMousePosition = m_mousePosition;
        
        // 计算鼠标增量
        m_mouseDelta.x = m_mousePosition.x - m_prevMousePosition.x;
        m_mouseDelta.y = m_mousePosition.y - m_prevMousePosition.y;
        
        // 每帧重置滚轮增量
        m_scrollDelta = Math::Vec2f(0.0f, 0.0f);
    }
}
