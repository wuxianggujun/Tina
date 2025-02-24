#pragma once

#include "tina/core/Core.hpp"
#include <atomic>

namespace Tina {

/**
 * @brief 引用计数类
 * 
 * 提供线程安全的引用计数功能。
 */
class TINA_CORE_API RefCount {
public:
    /**
     * @brief 构造函数
     */
    RefCount() : m_count(0) {}

    /**
     * @brief 增加引用计数
     */
    void increment() {
        m_count.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief 减少引用计数
     * @return 减少后的引用计数
     */
    int decrement() {
        return m_count.fetch_sub(1, std::memory_order_acq_rel);
    }

    /**
     * @brief 获取当前引用计数
     * @return 当前引用计数
     */
    int use_count() const {
        return m_count.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> m_count;  ///< 原子引用计数
};

} // namespace Tina 