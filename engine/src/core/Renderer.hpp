#ifndef TINA_CORE_RENDERER_HPP
#define TINA_CORE_RENDERER_HPP

// https://www.sandeepnambiar.com/getting-started-with-bgfx/
#include <bgfx/bgfx.h>
#include "math/Vector.hpp"
#include "core/Shader.hpp"
#include "graphics/IndexBuffer.hpp"
#include "graphics/VertexBuffer.hpp"
#include "tool/BgfxUtils.hpp"

namespace Tina {
    class Renderer {
    public:
        
        struct PosColorVertex {
            float m_x;
            float m_y;
            float m_z;
            uint32_t m_abgr;
        };

        Renderer(Vector2i resolution, int viewId);

        ~Renderer() = default;

        void render();

        void shutdown();

    private:
        bgfx::TextureHandle loadTexture(const char* fileName);

    private:
        Vector2i _resolution;
        bgfx::ViewId _viewId;
        Shader m_shader;
        bgfx::UniformHandle s_texColor;
        IndexBuffer m_indexBuffer;
        VertexBuffer m_vertexBuffer;
        bgfx::TextureHandle m_texture;
    };
}

#endif // TINA_CORE_RENDERER_HPP
