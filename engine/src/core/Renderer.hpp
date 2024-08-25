#ifndef TINA_CORE_RENDERER_HPP
#define TINA_CORE_RENDERER_HPP

// https://www.sandeepnambiar.com/getting-started-with-bgfx/
#include <bgfx/bgfx.h>
#include "math/Vector.hpp"
#include <bgfx/embedded_shader.h>
#include "generated/shaders/engine/all.h"
#include "tool/bgfx_utils.h" 

namespace Tina {

    struct PosColorVertex {
        float m_x;
        float m_y;
        float m_z;
        uint32_t m_abgr;

        static void init() {
            ms_decl.begin()
               .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
               .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
               .end();
        }
        static bgfx::VertexLayout ms_decl;
    };
    
    class Renderer {
    public:
        Renderer(Vector2i resolution, int viewId);

        ~Renderer();

        void render();

        void shutdown();

    private:
        Vector2i _resolution;
        bgfx::ViewId _viewId;
        bgfx::ProgramHandle _programHandle;
    };
}

#endif // TINA_CORE_RENDERER_HPP
