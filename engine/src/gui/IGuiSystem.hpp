#ifndef TINA_GUI_IGUI_SYSTEM_HPP
#define TINA_GUI_IGUI_SYSTEM_HPP

#include <entt/entt.hpp>

namespace Tina {

    class IGuiSystem {
    public:
        virtual ~IGuiSystem() = default;
        virtual void initialize(int width, int height) = 0;
        virtual void shutdown() = 0;
        virtual void update(entt::registry& registry, float deltaTime) = 0;
        virtual void render(entt::registry& registry) = 0;
    };
    
}


#endif