//
// Created by wuxianggujun on 2024/5/21.
//

#ifndef TINA_ECS_ENTITY_HPP
#define TINA_ECS_ENTITY_HPP

#include "Scene.hpp"

namespace Tina {

    class Entity {
    private:
        entt::entity entity_handle = entt::entity(0);
        Scene *scene = nullptr;
    public:
        Entity();

        Entity(entt::entity s_entity_handle, Scene *s_scene);

        template<class T, class... Args>
        T &addComponent(Args &&... args) {
            /*if (hasComponent<T>())
                printf("Entity already has the component");*/
            return scene->registry.emplace<T>(entity_handle, std::forward<Args>(args)...);
        }

        template<class T>
        T &getComponent() {
            return scene->registry.get<T>(entity_handle);
        }

        template<class T>
        void removeComponent() {
            scene->registry.remove<T>(entity_handle);
        }

        template<class T>
        bool hasComponent() {
            return scene->registry.valid(entity_handle);
        }


    };

} // Tina

#endif //TINA_ECS_ENTITY_HPP
