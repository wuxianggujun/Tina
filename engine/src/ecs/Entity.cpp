//
// Created by wuxianggujun on 2024/5/21.
//

#include "Entity.hpp"

namespace Tina {
    Entity::Entity() {

    }

    Entity::Entity(entt::entity s_entity_handle, Scene *s_scene) : entity_handle(s_entity_handle), scene(s_scene) {

    }

} // Tina