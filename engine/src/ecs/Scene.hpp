//
// Created by wuxianggujun on 2024/5/21.
//

#ifndef TINA_ECS_SCENE_HPP
#define TINA_ECS_SCENE_HPP

#include <entt/entt.hpp>

namespace Tina {

    class Entity;

    class Scene {
    private:
        entt::registry registry;
        entt::entity rootEntity;
    public:
        Scene();

        ~Scene();

        Entity createEntity(std::string tag);

        friend class Entity;

    };

} // Tina

#endif //TINA_ECS_SCENE_HPP
