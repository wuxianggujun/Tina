#pragma once

#include "../core/Container.hpp"
#include "../core/base/ScopeExit.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace Tina::UI {

enum class UIActionDispatchResult : uint8_t {
    Dispatched,
    Reentrant
};

// Owns a UI command callback and keeps dispatch valid if the owning widget is
// removed, the callback replaces itself, or an exception leaves the callback.
class UIAction final {
public:
    using Handler = Container::FixedFunction<128, void>;

    UIAction()
        : m_state(std::make_shared<State>())
    {
    }

    UIAction(const UIAction&) = delete;
    UIAction& operator=(const UIAction&) = delete;
    UIAction(UIAction&&) = delete;
    UIAction& operator=(UIAction&&) = delete;

    template<typename F>
    void setHandler(F&& handler)
    {
        m_state->handler = {};
        m_state->handler = Container::Forward<F>(handler);
    }

    void clearHandler()
    {
        m_state->handler = {};
    }

    bool hasHandler() const noexcept
    {
        return static_cast<bool>(m_state->handler);
    }

    template<typename Dispatcher>
    UIActionDispatchResult dispatch(Dispatcher&& dispatcher)
    {
        const std::shared_ptr<State> state = m_state;
        if (state->dispatching) return UIActionDispatchResult::Reentrant;

        state->dispatching = true;
        auto resetDispatching = Core::makeScopeExit([state]() noexcept {
            state->dispatching = false;
        });

        // The snapshot makes clear/replace/destruction during the callback safe.
        Handler handler = state->handler;
        std::invoke(std::forward<Dispatcher>(dispatcher), handler);
        return UIActionDispatchResult::Dispatched;
    }

private:
    struct State final {
        Handler handler;
        bool dispatching = false;
    };

    std::shared_ptr<State> m_state;
};

} // namespace Tina::UI
