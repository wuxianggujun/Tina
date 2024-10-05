#ifndef TINA_WINDOW_EVENT_HANDLE_HPP
#define TINA_WINDOW_EVENT_HANDLE_HPP

#include <memory>
#include <mutex>
#include <unordered_map>
#include <typeindex>
#include "core/Core.hpp"
#include "EventListenerList.hpp"

namespace Tina {
    class EventHandle {
    public:
        template<class T, class F>
        void addEventListener(F &&func) {
            std::lock_guard<std::mutex> lock(m_mutex);
            getEventListenerList<T>().addEventListener(std::forward<F>(func));
        }


        template<class T, class F, class I>
        void addEventListener(F &&func, I *instance) {
            std::lock_guard<std::mutex> lock(m_mutex);
            getEventListenerList<T>().addEventListener(std::bind(func, instance, std::placeholders::_1), instance);
        }

        void removeEventListener(void *listener) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto &pair: m_listenerLists) {
                pair.second->removeEventListener(listener);
            }
        }

        template<class T>
        void pushEvent(const T &event) {
            std::lock_guard<std::mutex> lock(m_mutex);
            getEventListenerList<T>().pushEvent(event);
        }

        template<class T, class... Args>
        void emplaceEvent(Args &&... args) {
            std::lock_guard<std::mutex> lock(m_mutex);
            getEventListenerList<T>().pushEvent(T{std::forward<Args>(args)...});
        }

        void processEvents() {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto &pair: m_listenerLists) {
                pair.second->processEvents();
            }
        }

    private:
        template<class T>
        EventListenerList<T> &getEventListenerList() {
            auto it = m_listenerLists.find(typeid(T));
            if (it == m_listenerLists.end()) {
                m_listenerLists[typeid(T)] = createRefPtr<EventListenerList<T> >();
                return *std::static_pointer_cast<EventListenerList<T> >(m_listenerLists[typeid(T)]);
            }
            return *std::static_pointer_cast<EventListenerList<T> >(it->second);
        }

        std::unordered_map<std::type_index, RefPtr<IEventListener> > m_listenerLists;
        std::mutex m_mutex;
    };
}


#endif // TINA_WINDOW_EVENT_HANDLE_HPP
