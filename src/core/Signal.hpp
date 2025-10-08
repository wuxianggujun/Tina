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
#include <thread>
#include <cassert>

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

    // 连接记录：独立于 Signal/Connection 的共享小对象
    struct ConnectionRecord {
        Signal* sig = nullptr;
        uint32_t id = 0;
        bool valid = true;
    };

    struct Connection {
        Connection() = default;
        explicit Connection(ConnectionRecord* r) : rec(r) {}

        // 禁止复制，避免多实例管理同一连接
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        // 允许移动，便于转移所有权
        Connection(Connection&& other) noexcept : rec(other.rec) {
            other.rec = nullptr;
        }
        Connection& operator=(Connection&& other) noexcept {
            if (this != &other) {
                disconnect();
                rec = other.rec;
                other.rec = nullptr;
            }
            return *this;
        }

        ~Connection() { disconnect(); }

        void disconnect() {
            if (rec) {
                // Signal 仍然存在：走正常注销流程，并将记录从 Signal 移除
                if (rec->sig) {
                    rec->sig->disconnect(rec->id);
                    rec->sig->unregisterRecord(rec);
                }
                rec->valid = false;
                rec->sig = nullptr;
                delete rec;
                rec = nullptr;
            }
        }
        bool connected() const { return rec != nullptr && rec->sig != nullptr && rec->id != 0; }
    private:
        ConnectionRecord* rec = nullptr;
        friend class Signal;
    };

    Signal() = default;

    // 连接 lambda/函数
    Connection connect(SlotFn fn) {
        checkThread();
        return addSlot(std::move(fn), /*once=*/false);
    }

    // 连接一次性槽（触发一次后自动断开）
    Connection connect_once(SlotFn fn) {
        checkThread();
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
        checkThread();
        // 允许触发期间进行延迟删除（标记无效，结束后清理）
        m_emitting = true;
        for (auto& s : m_slots) {
            if (!s.valid) continue;
            s.fn(std::forward<FwdArgs>(args)...);
            if (s.once) s.valid = false;
        }
        m_emitting = false;
        cleanup();
        ++m_emitCount;
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
        checkThread();
        for (auto& s : m_slots) {
            if (s.id == id && s.valid) { s.valid = false; break; }
        }
        if (!m_emitting) cleanup();
        ++m_disconnectCount;
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
        // 创建连接记录并登记到 Signal，确保 Signal 析构时可将其标记失效
        ConnectionRecord* rec = new ConnectionRecord();
        rec->sig = this;
        rec->id = m_nextId;
        rec->valid = true;
        m_records.push_back(rec);
        ++m_connectCount;
        return Connection(rec);
    }

    void cleanup() {
        // 使用 eastl::remove_if 移除无效槽
        auto it = eastl::remove_if(m_slots.begin(), m_slots.end(), [](const Slot& s){ return !s.valid; });
        m_slots.erase(it, m_slots.end());
    }

    // 连接记录注销：由 Connection 在断开时调用
    void unregisterRecord(ConnectionRecord* rec) {
        if (!rec) return;
        for (auto it = m_records.begin(); it != m_records.end(); ++it) {
            if (*it == rec) { m_records.erase(it); break; }
        }
    }

public:
    ~Signal() {
        // 将所有仍在存活的 Connection 记录标记为失效，避免 Connection 析构时访问已销毁的 Signal
        for (auto* rec : m_records) {
            if (!rec) continue;
            rec->sig = nullptr;
            rec->valid = false;
        }
        m_records.clear();
    }

    // 调试统计与断言接口
    std::size_t debugSlotCount() const {
        std::size_t n = 0; for (auto& s : m_slots) if (s.valid) ++n; return n;
    }
    std::size_t debugEmitCount() const { return m_emitCount; }
    std::size_t debugConnectCount() const { return m_connectCount; }
    std::size_t debugDisconnectCount() const { return m_disconnectCount; }
    void debugAssertSingleThread() const { assert(std::this_thread::get_id() == m_ownerThread && "Signal used from multiple threads"); }

private:
    Tina::Container::Vector<Slot> m_slots;
    bool m_emitting = false;
    uint32_t m_nextId = 0; // 每个 Signal 实例独立计数

    // 跟踪所有活跃的连接记录，用于 Signal 析构时批量失效
    Tina::Container::Vector<ConnectionRecord*> m_records;

    // 线程安全断言（默认绑定创建线程）
    std::thread::id m_ownerThread = std::this_thread::get_id();
    void checkThread() const {
        assert(std::this_thread::get_id() == m_ownerThread && "Signal used from multiple threads");
    }
    // 调试计数器
    std::size_t m_emitCount = 0;
    std::size_t m_connectCount = 0;
    std::size_t m_disconnectCount = 0;
};

} // namespace Tina::Core
