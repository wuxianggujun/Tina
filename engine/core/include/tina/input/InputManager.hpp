#pragma once

#include "tina/core/Core.hpp"
#include "tina/input/KeyCode.hpp"
#include "tina/core/Singleton.hpp"
#include <unordered_map>

namespace Tina {

class TINA_CORE_API InputManager : public Singleton<InputManager> {
    friend class Singleton<InputManager>;

public:
    bool isKeyPressed(KeyCode key) const {
        auto it = m_keyStates.find(static_cast<int>(key));
        return it != m_keyStates.end() && it->second;
    }

    void setKeyState(KeyCode key, bool pressed) {
        m_keyStates[static_cast<int>(key)] = pressed;
    }

    void reset() {
        m_keyStates.clear();
    }

private:
    InputManager() = default;
    ~InputManager() override = default;

    std::unordered_map<int, bool> m_keyStates;
};

} // namespace Tina