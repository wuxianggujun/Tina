#ifndef TINA_WINDOW_EVENT_LISTENER_LIST_HPP
#define TINA_WINDOW_EVENT_LISTENER_LIST_HPP

#include <functional>
#include <queue>
#include <vector>

namespace Tina {
    class IEventListener {
    public:
        virtual ~IEventListener() = default;

        virtual void processEvents() = 0;

        virtual void removeEventListener(void *listener) = 0;
    };

    template<class T>
    class EventListenerList : public IEventListener {
    public:
        void addEventListener(std::function<void(const T &)> &&function) {
            m_listeners.emplace_back(function, nullptr);
        }

        void addEventListener(std::function<void(const T &)> &&function, void *listener) {
            m_listeners.emplace_back(function, listener);
        }

        void removeEventListener(void *listener) override {
            for (auto it = m_listeners.begin(); it != m_listeners.end();) {
                if (it->second == listener)
                    it = m_listeners.erase(it);
                else
                    ++it;
            }
        }

        void pushEvent(const T &event) {
            m_events.emplace(event);
        }

        template<class... Args>
        void emplaceEvent(Args &&... args) {
            m_events.emplace(std::forward<Args>(args)...);
        }

        void processEvents() override {
            while (!m_events.empty()) {
                const T &event = m_events.front();
                for (auto &listener: m_listeners) {
                    listener.first(event);
                }
                m_events.pop();
            }
        }

    private:
        std::queue<T> m_events;
        std::vector<std::pair<std::function<void(const T &)>, void *> > m_listeners;
    };
}


#endif //TINA_WINDOW_EVENT_LISTENER_LIST_HPP
