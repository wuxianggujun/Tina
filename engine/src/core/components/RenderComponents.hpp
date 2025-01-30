#pragma once

#include "math/Vector.hpp"
#include "graphics/Color.hpp"
#include <bgfx/bgfx.h>

namespace Tina {

// 变换组件
struct TransformComponent {
    Vector2f position{0.0f, 0.0f};
    Vector2f scale{1.0f, 1.0f};
    float rotation{0.0f};
};

// 精灵渲染组件
struct SpriteRendererComponent {
    Color color = Color::White;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    Vector2f size{100.0f, 100.0f};
    float depth{0.0f};
    bool visible{true};
};

// 矩形渲染组件
struct QuadRendererComponent {
    Color color = Color::White;
    Vector2f size{100.0f, 100.0f};
    float depth{0.0f};
    bool visible{true};
};

// 自定义渲染组件
struct CustomRendererComponent {
    std::function<void()> renderFunction;
    float depth{0.0f};
    bool visible{true};
};

} 