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
        EventQueue();
        ~EventQueue();

        void postEvent(const Event& event);
        Event pollEvent();
        bool peekEvent(Event& event);

    private:
        std::queue<Event> m_eventQueue;
        std::mutex m_mutex;
    };
} // Tina
