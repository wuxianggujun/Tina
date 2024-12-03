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
        struct PosNormalTangentTexcoordVertex {
            // 顶点坐标
            float m_x;
            float m_y;
            float m_z;
            // 纹理坐标
            int16_t m_u;
            int16_t m_v;
        };

        Renderer(Vector2i resolution, int viewId);

        ~Renderer() = default;

        void render();

        void frame();
        void shutdown();
        
        // 获取分辨率
        Vector2i getResolution() const { return m_resolution; }
    
    private:
        VertexBuffer m_vbh;
        IndexBuffer m_ibh;
        Shader m_shader;
        bgfx::UniformHandle s_texColor = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_textureColor = BGFX_INVALID_HANDLE;

        Vector2i m_resolution;
    };
}

#endif // TINA_CORE_RENDERER_HPP
