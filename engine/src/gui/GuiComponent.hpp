#pragma once
#include <bgfx/bgfx.h>
#include <string>
#include <glm/glm.hpp>

namespace Tina {

struct GuiComponent {
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 size{100.0f, 50.0f};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    bool visible{true};
    std::string text;
    
    // 可以添加更多GUI属性
    float opacity{1.0f};
    bool interactive{true};
};

}  // namespace Engine 