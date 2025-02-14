#pragma once

#include "ObjectPool.hpp"
#include <unordered_map>
#include <typeindex>
#include <memory>

namespace Tina {

class IComponentPool {
public:
    virtual ~IComponentPool() = default;
};

template<typename T>
class ComponentPool : public IComponentPool {
public:
    ComponentPool() : m_Pool(1000) {}

    template<typename... Args>
    T* createComponent(Args&&... args) {
        return m_Pool.acquire(std::forward<Args>(args)...);
    }

    void destroyComponent(T* component) {
        m_Pool.release(component);
    }

private:
    ObjectPool<T> m_Pool;
};

class ComponentPoolManager {
public:
    template<typename T>
    ComponentPool<T>* getOrCreatePool() {
        std::type_index typeId(typeid(T));
        
        auto it = m_Pools.find(typeId);
        if (it != m_Pools.end()) {
            return static_cast<ComponentPool<T>*>(it->second.get());
        }
        
        auto pool = std::make_unique<ComponentPool<T>>();
        auto* poolPtr = pool.get();
        m_Pools[typeId] = std::move(pool);
        return poolPtr;
    }

    template<typename T, typename... Args>
    T* createComponent(Args&&... args) {
        return getOrCreatePool<T>()->createComponent(std::forward<Args>(args)...);
    }

    template<typename T>
    void destroyComponent(T* component) {
        if (auto* pool = getOrCreatePool<T>()) {
            pool->destroyComponent(component);
        }
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> m_Pools;
};

} // namespace Tina 