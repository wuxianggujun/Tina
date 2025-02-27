#pragma once

#include <array>

#include "tina/core/Core.hpp"
#include "tina/event/KeyCode.hpp"
#include "tina/core/Singleton.hpp"
#include <unordered_map>

#include "tina/math/Vec.hpp"

namespace Tina
{
    /**
     * 鼠标按钮枚举
     * 说明：
     * - 基本按钮：Left(左键), Right(右键), Middle(中键/滚轮按下)
     * - 扩展按钮：Button4-Button8为扩展按钮，支持更多按钮的游戏鼠标
     * - 现代游戏鼠标通常具有更多按钮，例如：前进、后退、DPI切换等
     * - Last值表示最后一个有效按钮
     * - Count表示总按钮数，用于数组分配
     */
    enum class MouseButton
    {
        Left = 0,
        Right = 1,
        Middle = 2,
        Button4 = 3,
        Button5 = 4,
        Button6 = 5,
        Button7 = 6,
        Button8 = 7,
        Last = Button8,
        Count = 8
    };

    class TINA_CORE_API InputManager : public Singleton<InputManager>
    {
        friend class Singleton<InputManager>;

    public:
        void initialize();
        void shutdown();
        void update();

        bool isKeyPressed(KeyCode key) const
        {
            auto it = m_keyStates.find(static_cast<int>(key));
            return it != m_keyStates.end() && it->second;
        }

        bool isKeyJustPressed(KeyCode key) const
        {
            auto it = m_keyStates.find(static_cast<int>(key));
            auto prevIt = m_prevKeyStates.find(static_cast<int>(key));
            return it != m_keyStates.end() && it->second &&
                (prevIt == m_prevKeyStates.end() || !prevIt->second);
        }

        bool isKeyJustReleased(KeyCode key) const {
            auto it = m_keyStates.find(static_cast<int>(key));
            auto prevIt = m_prevKeyStates.find(static_cast<int>(key));
            return (it == m_keyStates.end() || !it->second) && 
                   prevIt != m_prevKeyStates.end() && prevIt->second;
        }


        void setKeyState(KeyCode key, bool pressed)
        {
            m_keyStates[static_cast<int>(key)] = pressed;
        }

        // 鼠标输入
        bool isMouseButtonPressed(MouseButton button) const {
            int index = static_cast<int>(button);
            return index >= 0 && index < static_cast<int>(MouseButton::Count) && m_mouseButtonStates[index];
        }
    
        bool isMouseButtonJustPressed(MouseButton button) const {
            int index = static_cast<int>(button);
            return index >= 0 && index < static_cast<int>(MouseButton::Count) && 
                   m_mouseButtonStates[index] && !m_prevMouseButtonStates[index];
        }
    
        bool isMouseButtonJustReleased(MouseButton button) const {
            int index = static_cast<int>(button);
            return index >= 0 && index < static_cast<int>(MouseButton::Count) && 
                   !m_mouseButtonStates[index] && m_prevMouseButtonStates[index];
        }
        
        void setMouseButtonState(MouseButton button, bool pressed) {
            int index = static_cast<int>(button);
            if (index >= 0 && index < static_cast<int>(MouseButton::Count)) {
                m_mouseButtonStates[index] = pressed;
            }
        }


        // 鼠标位置
        const Math::Vec2f& getMousePosition() const { return m_mousePosition; }
        const Math::Vec2f& getMouseDelta() const { return m_mouseDelta; }
        const Math::Vec2f& getScrollDelta() const { return m_scrollDelta; }

        void setMousePosition(const Math::Vec2f& position) {
            m_mousePosition = position;
        }
    
        void setScrollDelta(const Math::Vec2f& delta) {
            m_scrollDelta = delta;
        }
        
        // 重置状态
        void reset() {
            m_keyStates.clear();
            m_prevKeyStates.clear();
            for (int i = 0; i < static_cast<int>(MouseButton::Count); ++i) {
                m_mouseButtonStates[i] = false;
                m_prevMouseButtonStates[i] = false;
            }
            m_mousePosition = Math::Vec2f(0.0f, 0.0f);
            m_prevMousePosition = Math::Vec2f(0.0f, 0.0f);
            m_mouseDelta = Math::Vec2f(0.0f, 0.0f);
            m_scrollDelta = Math::Vec2f(0.0f, 0.0f);
        }


    private:
        InputManager() = default;
        ~InputManager() override = default;

        // 键盘状态
        std::unordered_map<int, bool> m_keyStates;
        std::unordered_map<int, bool> m_prevKeyStates;

        // 鼠标状态
        std::array<bool, static_cast<size_t>(MouseButton::Count)> m_mouseButtonStates{};
        std::array<bool, static_cast<size_t>(MouseButton::Count)> m_prevMouseButtonStates{};

        // 鼠标位置
        Math::Vec2f m_mousePosition{0.0f, 0.0f};
        Math::Vec2f m_prevMousePosition{0.0f, 0.0f};
        Math::Vec2f m_mouseDelta{0.0f, 0.0f};
        Math::Vec2f m_scrollDelta{0.0f, 0.0f};
    };
} // namespace Tina
