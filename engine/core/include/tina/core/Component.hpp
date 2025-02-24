//
// Created by wuxianggujun on 2025/2/24.
//

#pragma once
#include "tina/core/Core.hpp"

namespace Tina
{
    class Node;

    class TINA_CORE_API Component
    {
    public:
        Component() = default;
        virtual ~Component() = default;

        virtual void onAttach(Node* node) { m_node = node; }
        virtual void onDetach(Node* node) { m_node = nullptr; }

        virtual void update(float deltaTime)
        {
        }

        virtual void render()
        {
        }

        [[nodiscard]] Node* getNode() const { return m_node; }

    private:
        Node* m_node{nullptr};
    };
} // Tina
