#pragma once

#include "tina/core/Core.hpp"
#include "tina/event/Event.hpp"
#include <array>
#include <queue>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Tina {

    constexpr size_t MAX_QUEUES = 2;

    class TINA_CORE_API EventManager {
    public:
        using EventCallback = std::function<void(const EventPtr&)>;
        using EventListenerList = std::vector<EventCallback>;
        using EventListenerMap = std::unordered_map<EventID, EventListenerList>;

        static EventManager* getInstance();

        void addListener(const EventCallback& callback, EventID eventId);
        void removeListener(const EventCallback& callback, EventID eventId);
        void triggerEvent(const EventPtr& event);
        void queueEvent(const EventPtr& event);
        bool abortEvent(EventID eventId, bool allOfType = false);
        bool update(uint64_t maxMillis = 0);

        void addThreadedListener(const EventCallback& callback, EventID eventId);
        void removeThreadedListener(const EventCallback& callback, EventID eventId);
        void triggerThreadedEvent(const EventPtr& event);

    private:
        EventManager();
        ~EventManager();

        struct EventQueue {
            std::queue<EventPtr> events;
            std::mutex mutex;
            void clear() { 
                std::queue<EventPtr> empty;
                std::swap(events, empty);
            }
        };

        static EventManager* s_instance;
        EventListenerMap m_eventListeners;
        EventListenerMap m_threadedEventListeners;
        std::mutex m_threadedEventListenerMutex;
        
        std::array<EventQueue, MAX_QUEUES> m_queues;
        size_t m_activeQueue;
    };

} // namespace Tina 