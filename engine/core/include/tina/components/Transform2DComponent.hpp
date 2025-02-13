#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Tina {

class Transform2DComponent {
public:
    Transform2DComponent() = default;
    Transform2DComponent(const glm::vec2& position)
        : m_Position(position) {}
    
    // Position
    void setPosition(const glm::vec2& position) { m_Position = position; }
    const glm::vec2& getPosition() const { return m_Position; }
    
    // Rotation (in radians)
    void setRotation(float rotation) { m_Rotation = rotation; }
    float getRotation() const { return m_Rotation; }
    
    // Scale
    void setScale(const glm::vec2& scale) { m_Scale = scale; }
    const glm::vec2& getScale() const { return m_Scale; }
    
    // Get transform matrix
    glm::mat4 getTransform() const {
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, glm::vec3(m_Position, 0.0f));
        transform = glm::rotate(transform, m_Rotation, glm::vec3(0.0f, 0.0f, 1.0f));
        transform = glm::scale(transform, glm::vec3(m_Scale, 1.0f));
        return transform;
    }

private:
    glm::vec2 m_Position = {0.0f, 0.0f};
    float m_Rotation = 0.0f;
    glm::vec2 m_Scale = {1.0f, 1.0f};
};

} // namespace Tina 