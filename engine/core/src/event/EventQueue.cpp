#include "tina/event/EventQueue.hpp"

namespace Tina {
    void EventQueue::pushEvent(const Event& event) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_eventQueue.push(event);
    }

    bool EventQueue::pollEvent(Event& event) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_eventQueue.empty()) {
            return false;
        }
        event = m_eventQueue.front();
        m_eventQueue.pop();
        return true;
    }

    void EventQueue::clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::queue<Event> empty;
        std::swap(m_eventQueue, empty);
    }

    bool EventQueue::isEmpty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_eventQueue.empty();
    }
} // Tina
