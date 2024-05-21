//
// Created by wuxianggujun on 2024/5/21.
//

#include "Scene.hpp"
#include "Entity.hpp"
#include "Components.hpp"

namespace Tina {
    Scene::Scene() {

    }

    Scene::~Scene() {

    }

    Entity Scene::createEntity(std::string tag) {
        Entity entity(registry.create(), this);
        entity.addComponent<Components::TagComponent>(tag);
        return entity;
    }

} // Tina