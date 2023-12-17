//
// Created by WuXiangGuJun on 2023/12/15.
//

#ifndef GAMEOBJECT_HPP
#define GAMEOBJECT_HPP

#include <unordered_set>
#include "Component.hpp"

class World;

class GameObject
{
public:
    GameObject(World* world);
    virtual ~GameObject();

    void destory();

    virtual void update(float dt);
    virtual void render(SDL_Renderer* renderer);

    template<class T>
    T* addComponent()
    {
        T* new_component = new T();
        m_components.emplace(new_component);
        return new_component;
    }

    template<class T>
    T* getComponent()
    {
        for (auto& comp: m_components)
        {
            if (dynamic_cast<T*>(comp))
                return dynamic_cast<T*>(comp);
        }
        return nullptr;
    }


    World* getWorld()
    {
        return m_world_instance;
    }


private:
    std::unordered_set<Component*> m_components;
    World* m_world_instance;
    
};

#endif //GAMEOBJECT_HPP
