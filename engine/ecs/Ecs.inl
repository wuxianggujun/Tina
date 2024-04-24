#pragma once

namespace Tina {
    namespace Ecs {
        template<class ...Components>
        entt::entity create(Components ...components)
        {
            auto entity = registry.create();
            auto x = {(registry.assign<Components>(entity,components),0)...};
            return entity;
        }

        template<class ...Components>
        void Ecs::assign(entt::entity entity, Components ...components)
        {
            auto x = {(registry.assign<Components>(entity,components),0)...};
        }
    }
}
