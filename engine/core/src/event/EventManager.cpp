#include "tina/event/EventManager.hpp"
#include "tina/log/Log.hpp"

namespace Tina {

EventManager* EventManager::s_instance = nullptr;

EventManager* EventManager::getInstance() {
    if (!s_instance) {
        s_instance = new EventManager();
    }
    return s_instance;
}

EventManager::EventManager() : m_activeQueue(0) {}

EventManager::~EventManager() {
    m_eventListeners.clear();
    m_queues[0].clear();
    m_queues[1].clear();
    
    std::lock_guard<std::mutex> lock(m_threadedEventListenerMutex);
    m_threadedEventListeners.clear();
}

void EventManager::addListener(const EventCallback& callback, EventID eventId) {
    auto& listeners = m_eventListeners[eventId];
    for (const auto& existingCallback : listeners) {
        if (callback.target_type() == existingCallback.target_type()) {
            return;
        }
    }
    listeners.push_back(callback);
}

void EventManager::removeListener(const EventCallback& callback, EventID eventId) {
    auto it = m_eventListeners.find(eventId);
    if (it != m_eventListeners.end()) {
        auto& listeners = it->second;
        for (auto listIt = listeners.begin(); listIt != listeners.end(); ++listIt) {
            if (callback.target_type() == listIt->target_type()) {
                listeners.erase(listIt);
                break;
            }
        }
    }
}

void EventManager::triggerEvent(const EventPtr& event) {
    if (!event) return;

    auto it = m_eventListeners.find(event->getEventID());
    if (it != m_eventListeners.end()) {
        auto& listeners = it->second;
        for (const auto& callback : listeners) {
            callback(event);
            if (event->handled) break;
        }
    }
}

void EventManager::queueEvent(const EventPtr& event) {
    if (!event) return;

    auto it = m_eventListeners.find(event->getEventID());
    if (it != m_eventListeners.end()) {
        std::lock_guard<std::mutex> lock(m_queues[m_activeQueue].mutex);
        m_queues[m_activeQueue].events.push(event);
    }
}

bool EventManager::abortEvent(EventID eventId, bool allOfType) {
    auto& eventQueue = m_queues[m_activeQueue];
    std::lock_guard<std::mutex> lock(eventQueue.mutex);

    bool success = false;
    std::queue<EventPtr> tempQueue;
    
    while (!eventQueue.events.empty()) {
        auto& event = eventQueue.events.front();
        if (event->getEventID() != eventId) {
            tempQueue.push(event);
        } else {
            success = true;
            if (!allOfType) {
                break;
            }
        }
        eventQueue.events.pop();
    }

    while (!tempQueue.empty()) {
        eventQueue.events.push(tempQueue.front());
        tempQueue.pop();
    }

    return success;
}

bool EventManager::update(uint64_t maxMillis) {
    const size_t queueToProcess = m_activeQueue;
    m_activeQueue = (m_activeQueue + 1) % MAX_QUEUES;
    
    auto& processQueue = m_queues[queueToProcess];
    std::lock_guard<std::mutex> lock(processQueue.mutex);

    const auto start = std::chrono::high_resolution_clock::now();
    bool processedEvents = false;

    while (!processQueue.events.empty()) {
        auto event = processQueue.events.front();
        processQueue.events.pop();

        auto it = m_eventListeners.find(event->getEventID());
        if (it != m_eventListeners.end()) {
            auto& listeners = it->second;
            for (const auto& callback : listeners) {
                callback(event);
                if (event->handled) break;
            }
        }

        processedEvents = true;

        if (maxMillis > 0) {
            auto now = std::chrono::high_resolution_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
            if (diff.count() >= static_cast<int64_t>(maxMillis)) {
                break;
            }
        }
    }

    return processedEvents;
}

void EventManager::addThreadedListener(const EventCallback& callback, EventID eventId) {
    std::lock_guard<std::mutex> lock(m_threadedEventListenerMutex);
    
    auto& listeners = m_threadedEventListeners[eventId];
    for (const auto& existingCallback : listeners) {
        if (callback.target_type() == existingCallback.target_type()) {
            return;
        }
    }
    listeners.push_back(callback);
}

void EventManager::removeThreadedListener(const EventCallback& callback, EventID eventId) {
    std::lock_guard<std::mutex> lock(m_threadedEventListenerMutex);
    
    auto it = m_threadedEventListeners.find(eventId);
    if (it != m_threadedEventListeners.end()) {
        auto& listeners = it->second;
        for (auto listIt = listeners.begin(); listIt != listeners.end(); ++listIt) {
            if (callback.target_type() == listIt->target_type()) {
                listeners.erase(listIt);
                break;
            }
        }
    }
}

void EventManager::triggerThreadedEvent(const EventPtr& event) {
    if (!event) return;

    std::lock_guard<std::mutex> lock(m_threadedEventListenerMutex);
    
    auto it = m_threadedEventListeners.find(event->getEventID());
    if (it != m_threadedEventListeners.end()) {
        auto& listeners = it->second;
        for (const auto& callback : listeners) {
            callback(event);
            if (event->handled) break;
        }
    }
}

} // namespace Tina 