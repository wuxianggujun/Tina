#pragma once

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace Tina::Core {

template <typename Callback>
concept ScopeExitCallback = std::is_nothrow_invocable_v<Callback&>
    && std::is_nothrow_move_constructible_v<Callback>;

template <ScopeExitCallback Callback>
class [[nodiscard]] ScopeExit final {
public:
    template <typename Source>
        requires std::same_as<std::remove_cvref_t<Source>, Callback>
        && std::is_nothrow_constructible_v<Callback, Source&&>
    explicit ScopeExit(Source&& callback) noexcept
        : m_callback(std::forward<Source>(callback))
    {
    }

    ScopeExit(ScopeExit&& other) noexcept
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
    requires ScopeExitCallback<std::decay_t<Callback>>
    && std::is_nothrow_constructible_v<std::decay_t<Callback>, Callback&&>
[[nodiscard]] auto makeScopeExit(Callback&& callback) noexcept
{
    return ScopeExit<std::decay_t<Callback>>(std::forward<Callback>(callback));
}

} // namespace Tina::Core
