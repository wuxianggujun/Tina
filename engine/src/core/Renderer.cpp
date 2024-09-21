#include "Renderer.hpp"

#include "bgfx/platform.h"
#include "bx/math.h"
#include "bx/timer.h"
#include "stb/stb_image.h"

namespace Tina {
    
    Renderer::PosNormalTangentTexcoordVertex s_cubeVertices[24] = {
        {-1.0f, 1.0f, 1.0f, 0, 0},
        {1.0f, 1.0f, 1.0f, 0x7fff, 0},
        {-1.0f, -1.0f, 1.0f, 0, 0x7fff},
        {1.0f, -1.0f, 1.0f, 0x7fff, 0x7fff},
        {-1.0f, 1.0f, -1.0f, 0, 0},
        {1.0f, 1.0f, -1.0f, 0x7fff, 0},
        {-1.0f, -1.0f, -1.0f, 0, 0x7fff},
        {1.0f, -1.0f, -1.0f, 0x7fff, 0x7fff},
        {-1.0f, 1.0f, 1.0f, 0, 0},
        {1.0f, 1.0f, 1.0f, 0x7fff, 0},
        {-1.0f, 1.0f, -1.0f, 0, 0x7fff},
        {1.0f, 1.0f, -1.0f, 0x7fff, 0x7fff},
        {-1.0f, -1.0f, 1.0f, 0, 0},
        {1.0f, -1.0f, 1.0f, 0x7fff, 0},
        {-1.0f, -1.0f, -1.0f, 0, 0x7fff},
        {1.0f, -1.0f, -1.0f, 0x7fff, 0x7fff},
        {1.0f, -1.0f, 1.0f, 0, 0},
        {1.0f, 1.0f, 1.0f, 0x7fff, 0},
        {1.0f, -1.0f, -1.0f, 0, 0x7fff},
        {1.0f, 1.0f, -1.0f, 0x7fff, 0x7fff},
        {-1.0f, -1.0f, 1.0f, 0, 0},
        {-1.0f, 1.0f, 1.0f, 0x7fff, 0},
        {-1.0f, -1.0f, -1.0f, 0, 0x7fff},
        {-1.0f, 1.0f, -1.0f, 0x7fff, 0x7fff},
    };

    static const uint16_t s_cubeIndices[36] =
    {
        0, 2, 1,
        1, 2, 3,
        4, 5, 6,
        5, 7, 6,

        8, 10, 9,
        9, 10, 11,
        12, 13, 14,
        13, 15, 14,

        16, 18, 17,
        17, 18, 19,
        20, 21, 22,
        21, 23, 22,
    };

    Renderer::Renderer(Vector2i size, int viewId): m_resolution(size){
        
        m_vbh.getLayout().begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Int16, true, true)
                .end();


        m_vbh.init(s_cubeVertices, sizeof(s_cubeVertices));
        m_ibh.init(s_cubeIndices, sizeof(s_cubeIndices));

        std::cout << "Renderer IBH: "<< m_ibh.handle() << std::endl;
        std::cout << "Renderer VBH: "<< m_vbh.handle() << std::endl;
        s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

        m_shader.loadFromFile("bump");

        m_textureColor = BgfxUtils::loadTexture("../resources/textures/grassland.png");

   
        bgfx::setViewClear(0,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, m_resolution.width, m_resolution.height);
    }

    void Renderer::render() {
        bgfx::touch(0);

        // Set vertex and index buffer.
        m_vbh.enable();
        m_ibh.enable();

        // Bind textures.
        bgfx::setTexture(0, s_texColor, m_textureColor);

        // Set render states.
        bgfx::setState(0
                       | BGFX_STATE_WRITE_RGB
                       | BGFX_STATE_WRITE_A
                       | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS
                       | BGFX_STATE_MSAA
        );
        // Submit primitive for rendering to view 0.
        bgfx::submit(0, m_shader.getProgram());
        
        bgfx::frame();
    }

    void Renderer::shutdown() {
        m_vbh.free();
        m_ibh.free();
        bgfx::destroy(m_textureColor);
        bgfx::destroy(s_texColor);
        bgfx::shutdown();
    }
}
