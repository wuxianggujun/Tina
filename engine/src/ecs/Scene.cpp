//
// Created by wuxianggujun on 2024/5/21.
//

#include "Scene.hpp"
#include "Entity.hpp"
#include "Components.hpp"
#include <glm/glm.hpp>

namespace Tina {

    struct TransformComponent {
        glm::mat4 transform;

        TransformComponent() = default;

        TransformComponent(const TransformComponent &) = default;

        explicit TransformComponent(const glm::mat4 &m_transform) : transform(m_transform) {}

        explicit operator const glm::mat4 &() const {
            return transform;
        }
    };

    Scene::Scene() {
        rootEntity = registry.create();
    }

    Scene::~Scene() {
    }

    Entity Scene::createEntity(std::string tag) {
        Entity entity(rootEntity, this);
        entity.addComponent<Components::TagComponent>(tag);
        return entity;
    }

} // Tina