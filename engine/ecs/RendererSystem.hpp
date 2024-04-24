#pragma once

#include <bgfx/bgfx.h>
#include <entt/entt.hpp>

namespace Tina {
    namespace RendererSystem {
        void init();
        void render(entt::registry& registry);
        void shutdown();
    }

}

