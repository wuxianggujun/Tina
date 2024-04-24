#ifndef TINA_ECS_HPP
#define TINA_ECS_HPP

#include <entt/entt.hpp>

namespace Tina{
    namespace Ecs {
        extern entt::registry registry;

        template<class ...Components>
        entt::entity create(Components... components);

        template<class ...Components>
        void assign(entt::entity entity, Components... components);
  
    }
}

#include "Ecs.inl"

#endif
