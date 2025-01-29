#pragma once

#include <bgfx/bgfx.h>

namespace Tina
{
    struct PosColorVertex
    {
        float x;
        float y;
        float z;
        uint32_t color;

        static void init()
        {
            ms_layout
                .begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
                .end();
        }

        static bgfx::VertexLayout ms_layout;
    };


    class Renderer2D
    {

    public:
        Renderer2D();
        ~Renderer2D();

        void initialize();
        void setViewProjection(float width, float height);
        void render();

    private:
        bgfx::ProgramHandle m_program;
        bgfx::VertexBufferHandle m_vbh;
        bgfx::IndexBufferHandle m_ibh;
    };
}