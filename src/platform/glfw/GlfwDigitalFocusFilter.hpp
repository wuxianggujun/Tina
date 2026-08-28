#pragma once

#include <tina/platform/Input.hpp>

#include <bitset>

namespace Tina::Platform::Detail {

// GLFW may emit synthetic releases after focus loss. Runtime receives one
// explicit cancel instead; these masks consume only the stale releases while
// preserving later genuine presses/releases after focus returns.
class GlfwDigitalFocusFilter final {
  public:
    void reset(bool focused) noexcept
    {
        acceptsInput_ = focused;
        suppressedKeys_.reset();
        suppressedPointerButtons_.reset();
    }

    void onFocusLost(const WindowInputSnapshot& input) noexcept
    {
        acceptsInput_ = false;
        suppressedKeys_ |= input.heldKeys;
        suppressedPointerButtons_ |= input.pointers[Platform::PrimaryPointerId].heldButtons;
    }

    void onFocusGained() noexcept
    {
        acceptsInput_ = true;
    }

    [[nodiscard]] bool shouldAccept(Key key, DigitalTransition transition) noexcept
    {
        const usize index = static_cast<usize>(key);
        if (index >= suppressedKeys_.size())
        {
            return false;
        }
        if (transition == DigitalTransition::Up && suppressedKeys_.test(index))
        {
            suppressedKeys_.reset(index);
            return false;
        }
        if (!acceptsInput_)
        {
            return false;
        }
        if (transition == DigitalTransition::Down)
        {
            suppressedKeys_.reset(index);
        }
        return true;
    }

    [[nodiscard]] bool shouldAccept(PointerButton button, DigitalTransition transition) noexcept
    {
        const usize index = static_cast<usize>(button);
        if (index >= suppressedPointerButtons_.size())
        {
            return false;
        }
        if (transition == DigitalTransition::Up && suppressedPointerButtons_.test(index))
        {
            suppressedPointerButtons_.reset(index);
            return false;
        }
        if (!acceptsInput_)
        {
            return false;
        }
        if (transition == DigitalTransition::Down)
        {
            suppressedPointerButtons_.reset(index);
        }
        return true;
    }

  private:
    std::bitset<KeyCount> suppressedKeys_{};
    std::bitset<PointerButtonCount> suppressedPointerButtons_{};
    bool acceptsInput_ = false;
};

} // namespace Tina::Platform::Detail
