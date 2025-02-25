//
// Created by wuxianggujun on 2025/2/24.
//

#include "tina/core/Node.hpp"

namespace Tina
{
    Node::~Node() = default;

    void Node::update(float deltaTime)
    {
        // 更新所有组件
        for (auto& component : m_components)
        {
            component->update(deltaTime);
        }
    }

    void Node::render()
    {
        // 渲染所有组件
        for (auto& component : m_components)
        {
            component->render();
        }
    }

} // namespace Tina