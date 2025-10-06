//
// 轻量级信号-槽（Signal/Slot）机制（Header-only）
// - 目标：统一事件分发，避免每处手写回调字段
// - 特性：connect / connect_once / 断开 / RAII 连接；支持成员函数与 lambda
// - 线程：默认单线程使用（游戏主线程）；如需跨线程请在外部加锁

#pragma once

#include <functional>
#include <cstdint>
#include <utility>
#include <algorithm>
#include "Container.hpp"

namespace Tina::Core {

// Signal<Args...> 使用示例：
//   Tina::Core::Signal<int, float> sig;
//   auto conn = sig.connect([](int a, float b){ /* ... */ });
//   sig.emit(42, 0.5f);
//   conn.disconnect();
//
//   // 一次性槽
//   sig.connect_once([](int, float){ /* 仅触发一次 */ });
//
//   // 成员函数
//   struct Obj { void onEvt(int, float) {} } o;
//   auto c2 = sig.connect_member(&o, &Obj::onEvt);

template <typename... Args>
class Signal {
public:
    using SlotFn = std::function<void(Args...)>;  // 建议主线程使用；如需 EASTL function 可自行替换

    struct Connection {
        Connection() = default;
        Connection(Signal* s, uint32_t id) : sig(s), slotId(id) {}

        // 禁止复制，避免多实例管理同一连接
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        // 允许移动，便于转移所有权
        Connection(Connection&& other) noexcept : sig(other.sig), slotId(other.slotId) {
            other.sig = nullptr; other.slotId = 0;
        }
        Connection& operator=(Connection&& other) noexcept {
            if (this != &other) {
                disconnect();
                sig = other.sig; slotId = other.slotId;
                other.sig = nullptr; other.slotId = 0;
            }
            return *this;
        }

        ~Connection() { disconnect(); }

        void disconnect() {
            if (sig) { sig->disconnect(slotId); sig = nullptr; slotId = 0; }
        }
        bool connected() const { return sig != nullptr && slotId != 0; }
    private:
        Signal* sig = nullptr;
        uint32_t slotId = 0;
        friend class Signal;
    };

    Signal() = default;
    ~Signal() = default;

    // 连接 lambda/函数
    Connection connect(SlotFn fn) {
        return addSlot(std::move(fn), /*once=*/false);
    }

    // 连接一次性槽（触发一次后自动断开）
    Connection connect_once(SlotFn fn) {
        return addSlot(std::move(fn), /*once=*/true);
    }

    // 连接成员函数（原始指针版本，调用方需自行保证对象生命周期）
    template <typename T>
    Connection connect_member(T* obj, void (T::*method)(Args...)) {
        return connect([obj, method](Args... args) {
            (obj->*method)(std::forward<Args>(args)...);
        });
    }

    // 预分配，减少增长带来的重分配
    void reserve(std::size_t n) { m_slots.reserve(n); }

    // 触发事件（完美转发）
    template <typename... FwdArgs>
    void emit(FwdArgs&&... args) {
        // 允许触发期间进行延迟删除（标记无效，结束后清理）
        m_emitting = true;
        for (auto& s : m_slots) {
            if (!s.valid) continue;
            s.fn(std::forward<FwdArgs>(args)...);
            if (s.once) s.valid = false;
        }
        m_emitting = false;
        cleanup();
    }

    // 断开所有槽
    void clear() {
        m_slots.clear();
    }

    // 槽数量（有效）
    std::size_t size() const {
        std::size_t n = 0;
        for (auto& s : m_slots) if (s.valid) ++n;
        return n;
    }

    // 手动断开指定连接
    void disconnect(uint32_t id) {
        for (auto& s : m_slots) {
            if (s.id == id && s.valid) { s.valid = false; break; }
        }
        if (!m_emitting) cleanup();
    }

private:
    struct Slot {
        uint32_t id = 0;
        bool once = false;
        bool valid = true;
        SlotFn fn;
    };

    Connection addSlot(SlotFn fn, bool once) {
        Slot s{};
        s.id = ++m_nextId;
        s.once = once;
        s.valid = true;
        s.fn = std::move(fn);
        m_slots.push_back(std::move(s));
        return Connection(this, m_nextId);
    }

    void cleanup() {
        // 使用 eastl::remove_if 移除无效槽
        auto it = eastl::remove_if(m_slots.begin(), m_slots.end(), [](const Slot& s){ return !s.valid; });
        m_slots.erase(it, m_slots.end());
    }

private:
    Tina::Container::Vector<Slot> m_slots;
    bool m_emitting = false;
    uint32_t m_nextId = 0; // 每个 Signal 实例独立计数
};

} // namespace Tina::Core
