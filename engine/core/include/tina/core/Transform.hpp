//
// Created by wuxianggujun on 2025/2/24.
//

#pragma once

#include "tina/core/Core.hpp"
#include "tina/core/Component.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Tina
{
    class TINA_CORE_API Transform : public Component
    {
    public:
        Transform() = default;
        void setPosition(const glm::vec3& position) { m_position = position; }
        void setRotation(const glm::quat& rotation) { m_rotation = rotation; }
        void setScale(const glm::vec3& scale) { m_scale = scale; }

        [[nodiscard]] const glm::vec3& getPosition() const { return m_position; }
        [[nodiscard]] const glm::quat& getRotation() const { return m_rotation; }
        [[nodiscard]] const glm::vec3& getScale() const { return m_scale; }

        [[nodiscard]] glm::mat4 getWorldMatrix() const;

    private:
        glm::vec3 m_position{0.0f};
        glm::quat m_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 m_scale{1.0f};
    };
}
