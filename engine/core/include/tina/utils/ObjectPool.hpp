#pragma once

#include <vector>
#include <mutex>
#include <memory>
#include <cassert>

namespace Tina {

template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t initialSize = 1000) {
        m_Pool.reserve(initialSize);
        for (size_t i = 0; i < initialSize; ++i) {
            m_Pool.push_back(std::make_unique<T>());
        }
    }

    template<typename... Args>
    T* acquire(Args&&... args) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        
        T* object = nullptr;
        if (m_Pool.empty()) {
            // 如果池为空,创建新对象
            object = new T(std::forward<Args>(args)...);
        } else {
            // 从池中获取对象
            object = m_Pool.back().release();
            m_Pool.pop_back();
            // 重新初始化对象
            *object = T(std::forward<Args>(args)...);
        }
        
        return object;
    }

    void release(T* object) {
        if (!object) return;
        
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pool.push_back(std::unique_ptr<T>(object));
    }

    size_t getAvailableCount() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Pool.size();
    }

private:
    std::vector<std::unique_ptr<T>> m_Pool;
    mutable std::mutex m_Mutex;
};

} // namespace Tina 