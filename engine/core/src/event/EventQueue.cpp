#include "tina/event/EventQueue.hpp"

namespace Tina
{
    EventQueue::EventQueue() = default;
    EventQueue::~EventQueue() = default;

    void EventQueue::postEvent(const Event& event)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_eventQueue.push(event);
    }

    Event EventQueue::pollEvent()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Event event = m_eventQueue.front();
        m_eventQueue.pop();
        return event;
    }

    bool EventQueue::peekEvent(Event& event)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_eventQueue.empty())
        {
            return false;
        }
        event = m_eventQueue.front();
        return true;
    }
} // Tina
