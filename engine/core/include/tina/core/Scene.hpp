//
// Created by wuxianggujun on 2025/2/24.
//

#pragma once
#include "tina/core/Core.hpp"
#include <entt/entity/registry.hpp>
#include <string>

namespace Tina
{
    class Node;

    class TINA_CORE_API Scene
    {
    public:
        explicit Scene(const std::string& name);
        virtual ~Scene();

        virtual void onEnter();
        virtual void onExit();
        virtual void onUpdate(float deltaTime);
        virtual void render();

        Node* createNode(const std::string& name);
        void destroyNode(Node* node);

        entt::registry& getRegistry()
        {
            return m_registry;
        }

        [[nodiscard]] const std::string& getName() const
        {
            return m_name;
        }

    private:
        std::string m_name;
        entt::registry m_registry;
        std::vector<UniquePtr<Node>> m_nodes;
    };
} // Tina
