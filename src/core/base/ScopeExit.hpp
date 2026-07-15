#pragma once

#include <functional>
#include <type_traits>
#include <utility>

namespace Tina::Core {

template <typename Callback>
    requires std::is_nothrow_invocable_v<Callback&>
class [[nodiscard]] ScopeExit final {
public:
    explicit ScopeExit(Callback callback) noexcept(std::is_nothrow_move_constructible_v<Callback>)
        : m_callback(std::move(callback))
    {
    }

    ScopeExit(ScopeExit&& other) noexcept(std::is_nothrow_move_constructible_v<Callback>)
        : m_callback(std::move(other.m_callback)), m_active(std::exchange(other.m_active, false))
    {
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

    ~ScopeExit() noexcept
    {
        if (m_active) {
            std::invoke(m_callback);
        }
    }

    void release() noexcept
    {
        m_active = false;
    }

private:
    Callback m_callback;
    bool m_active = true;
};

template <typename Callback>
    requires std::is_nothrow_invocable_v<std::decay_t<Callback>&>
[[nodiscard]] auto makeScopeExit(Callback&& callback)
{
    return ScopeExit<std::decay_t<Callback>>(std::forward<Callback>(callback));
}

} // namespace Tina::Core
