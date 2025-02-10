//
// Created by wuxianggujun on 2025/2/5.
//

#pragma once

#include "tina/event/Event.hpp"
#include <queue>
#include <mutex>

namespace Tina {
    class EventQueue {
    public:
        EventQueue() = default;
        ~EventQueue() = default;

        void pushEvent(const Event& event);
        bool pollEvent(Event& event);
        void clear();
        bool isEmpty() const;

    private:
        std::queue<Event> m_eventQueue;
        mutable std::mutex m_mutex;
    };
} // Tina
