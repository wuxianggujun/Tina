#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>

namespace Tina
{
    // 变换组件
    struct TransformComponent
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};

        TransformComponent() = default;
        TransformComponent(const glm::vec3& pos)
            : position(pos) {}

        glm::mat4 getTransform() const
        {
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
            transform = glm::rotate(transform, rotation.x, glm::vec3(1, 0, 0));
            transform = glm::rotate(transform, rotation.y, glm::vec3(0, 1, 0));
            transform = glm::rotate(transform, rotation.z, glm::vec3(0, 0, 1));
            transform = glm::scale(transform, scale);
            return transform;
        }
    };

    // 标签组件
    struct TagComponent
    {
        std::string tag;

        TagComponent() = default;
        TagComponent(const std::string& t)
            : tag(t) {}
    };

    // 网格渲染器组件
    struct MeshRendererComponent
    {
        // TODO: 添加网格和材质引用
        bool visible = true;

        MeshRendererComponent() = default;
    };

    // 相机组件
    struct CameraComponent
    {
        float fov = 45.0f;
        float nearClip = 0.1f;
        float farClip = 1000.0f;
        bool primary = true;

        CameraComponent() = default;
    };
}
