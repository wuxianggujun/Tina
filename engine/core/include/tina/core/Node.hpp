//
// Created by wuxianggujun on 2025/2/24.
//

#pragma once

#include "tina/core/Core.hpp"
#include <entt/entity/entity.hpp>
#include "tina/core/Component.hpp"
#include <vector>
#include <string>

namespace Tina
{
    class Scene;
    
    class TINA_CORE_API Node
    {
    public:
        explicit Node(const std::string& name) : m_scene(nullptr), m_name(name), m_entity(entt::null) {}
        explicit Node(Scene* scene, const std::string& name) : m_scene(scene), m_name(name), m_entity(entt::null) {}
        virtual ~Node();

        template <typename T, typename... Args>
        T* addComponent(Args&&... args)
        {
            T* component = new T(std::forward<Args>(args)...);
            m_components.push_back(UniquePtr<Component>(component));
            return component;
        }

        template <typename T>
        T* getComponent() const
        {
            for (auto& component : m_components)
            {
                if (T* result = dynamic_cast<T*>(component.get()))
                {
                    return result;
                }
            }
            return nullptr;
        }

        void update(float deltaTime);
        void render();

        [[nodiscard]] const std::string& getName() const { return m_name; }
        [[nodiscard]] Scene* getScene() const { return m_scene; }
        [[nodiscard]] entt::entity getEntity() const { return m_entity; }

    private:
        Scene* m_scene;
        std::string m_name;
        entt::entity m_entity;
        std::vector<UniquePtr<Component>> m_components;
    };
} // Tina
