//
// Created by wuxianggujun on 2024/7/1.
//

#ifndef FRAMEWORK_GRAPHICSBACKEND_HPP
#define FRAMEWORK_GRAPHICSBACKEND_HPP

namespace Tina {
    enum class GraphicsBackend {
        Noop,
        OpenGLES,
        OpenGL,
        Direct3DX11,
        Direct3DX12,
        Metal,
        Vulkan,
        Count
    };
}

#endif //FRAMEWORK_GRAPHICSBACKEND_HPP
