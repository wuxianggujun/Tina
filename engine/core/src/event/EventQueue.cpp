#include "tina/event/EventQueue.hpp"

namespace Tina
{
    EventQueue::EventQueue() = default;
    EventQueue::~EventQueue() = default;

    void EventQueue::postEvent(const Event& event)
    {
        try {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_eventQueue.push(event);
        } catch (const std::exception& e) {
            // 记录错误但不中断程序
            fmt::print(stderr, "Failed to post event: {}\n", e.what());
        }
    }

    Event EventQueue::pollEvent()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_eventQueue.empty()) {
            // 返回一个None类型的事件表示队列为空
            Event emptyEvent;
            emptyEvent.type = Event::None;
            return emptyEvent;
        }
        
        Event event = m_eventQueue.front();
        m_eventQueue.pop();
        return event;
    }

    bool EventQueue::peekEvent(Event& event)
    {
        try {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_eventQueue.empty()) {
                return false;
            }
            event = m_eventQueue.front();
            return true;
        } catch (const std::exception& e) {
            fmt::print(stderr, "Failed to peek event: {}\n", e.what());
            return false;
        }
    }
} // Tina
