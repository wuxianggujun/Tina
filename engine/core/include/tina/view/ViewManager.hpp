//
// Created by wuxianggujun on 2025/2/16.
//

#pragma once
#include <atomic>
#include <mutex>

namespace Tina
{
    class ViewManager
    {
    public:
        static ViewManager& getInstance()
        {
            static ViewManager instance;
            return instance;
        }

        uint16_t allocateViewId()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_nextViewId++;
        }

        void resetViewIds()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_nextViewId = 0;
        }

        void releaseViewId(uint16_t viewId)
        {
            // TODO: 实现viewId回收机制
            resetViewIds();
        }

    private:
        ViewManager() = default;
        std::atomic<uint16_t> m_nextViewId{0};
        std::mutex m_mutex;
    };
}
