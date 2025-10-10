//
// TypedEventBus - 基于 entt::dispatcher 的强类型事件总线（Header-only）
// - 零兼容层，直接使用 entt 的连接模型：编译期成员函数指针
// - API：connect<E, &T::method>(obj), trigger<E>(...), enqueue<E>(...), update()

#pragma once

#include <entt/entt.hpp>
#include <functional>

namespace Tina::Engine {

class TypedEventBus {
public:
    // 连接句柄（RAII 自动断开）
    class Connection {
    public:
        Connection() = default;
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& other) noexcept { *this = std::move(other); }
        Connection& operator=(Connection&& other) noexcept {
            if (this != &other) {
                disconnect();
                m_disconnect = std::move(other.m_disconnect);
                other.m_disconnect = nullptr;
            }
            return *this;
        }
        ~Connection() { disconnect(); }
        void disconnect() { if (m_disconnect) { m_disconnect(); m_disconnect = nullptr; } }
    private:
        friend class TypedEventBus;
        explicit Connection(std::function<void()> fn) : m_disconnect(std::move(fn)) {}
        std::function<void()> m_disconnect{};
    };

    TypedEventBus() = default;
    ~TypedEventBus() = default;

    // 成员函数订阅（非 const / const 都可），要求：void T::method(const E&)
    template<typename E, auto Fn, typename T>
    Connection connect(T& obj) {
        m_dispatcher.sink<E>().connect<Fn>(obj);
        return Connection([this, &obj]() { m_dispatcher.sink<E>().disconnect<Fn>(obj); });
    }

    // 自由函数/静态函数订阅：void fn(const E&)
    template<typename E, auto Fn>
    Connection connect() {
        m_dispatcher.sink<E>().connect<Fn>();
        return Connection([this]() { m_dispatcher.sink<E>().disconnect<Fn>(); });
    }

    // 触发（同步）
    template<typename E, typename... Args>
    void trigger(Args&&... args) { m_dispatcher.trigger<E>(E{std::forward<Args>(args)...}); }

    // 入队（异步）：在 update() 中派发
    template<typename E, typename... Args>
    void enqueue(Args&&... args) { m_dispatcher.enqueue<E>(E{std::forward<Args>(args)...}); }

    // 派发入队事件（建议在主循环每帧调用）
    void update() { m_dispatcher.update(); }

private:
    entt::dispatcher m_dispatcher;
};

} // namespace Tina::Engine
