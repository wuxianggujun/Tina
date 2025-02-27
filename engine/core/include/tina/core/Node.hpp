//
// Created by wuxianggujun on 2025/2/24.
//

#pragma once

#include "tina/core/Core.hpp"
#include <entt/entity/entity.hpp>
#include "tina/core/Component.hpp"
#include <vector>
#include <string>

#include "tina/log/Log.hpp"

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
            // 创建组件
            auto component = MakeUnique<T>(std::forward<Args>(args)...);
            T* componentPtr = component.get();
    
            // 关联组件与节点
            componentPtr->onAttach(this);  // 确保调用onAttach
    
            // 添加到组件列表
            m_components.push_back(std::move(component));
            m_componentsByType[typeid(T).hash_code()] = componentPtr;
    
            TINA_ENGINE_INFO("组件添加成功: {}", typeid(T).name());

            return componentPtr;
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
        // 添加这个新成员变量，用于按类型快速查找组件
        std::unordered_map<size_t, Component*> m_componentsByType;
    };
} // Tina
