#include "Renderer.hpp"
#include <fstream>

#include "bgfx/platform.h"
#include "bx/math.h"

namespace Tina {
    static Renderer::PosColorVertex s_cubeVertices[] =
    {
        {0.5f, 0.5f, 0.0f, 0xff0000ff},
        {0.5f, -0.5f, 0.0f, 0xff0000ff},
        {-0.5f, -0.5f, 0.0f, 0xff00ff00},
        {-0.5f, 0.5f, 0.0f, 0xff00ff00}
    };

    static const uint16_t s_cubeTriList[] =
    {
        0, 1, 3,
        1, 2, 3
    };

    float vertices[] = {
        -0.5f, -0.5f, 0.0f,
        0.5f, -0.5f, 0.0f,
        0.0f, 0.5f, 0.0f
    };

    uint16_t indices[] = {
        0, 1, 2
    };


    Renderer::Renderer(Vector2i size, int viewId): _resolution(size), _viewId(viewId) {
        // bgfx::setDebug(BGFX_DEBUG_TEXT);
        // bgfx::setDebug(BGFX_DEBUG_WIREFRAME);

        /*m_vertexBuffer.getLayout().begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
                .end();*/

        m_vertexBuffer.getLayout().begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .end();


        m_vertexBuffer.init(vertices, sizeof(vertices));
        // m_vertexBuffer.init(s_cubeVertices, sizeof(s_cubeVertices));

        //m_indexBuffer.init(s_cubeTriList, sizeof(s_cubeTriList));
        m_indexBuffer.init(indices, sizeof(indices));

        m_shader.loadFromFile("simple");

        bgfx::setViewClear(0,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
    }

    void Renderer::render() {
        constexpr bx::Vec3 at = {0.0f, 0.0f, 0.0f};
        constexpr bx::Vec3 eye = {0.0f, 0.0f, 10.0f};

        // Set view and projection matrix for view 0.
        float view[16];
        bx::mtxLookAt(view, eye, at);

        float proj[16];
        bx::mtxProj(proj,
                    60.0f,
                    float(_resolution.width) / float(_resolution.height),
                    0.1f, 100.0f,
                    bgfx::getCaps()->homogeneousDepth);

        bgfx::setViewTransform(_viewId, view, proj);

        // Set view 0 default viewport.
        bgfx::setViewRect(0, 0, 0,
                          _resolution.width,
                          _resolution.height);

        bgfx::touch(_viewId);

        float mtx[16];
        bx::mtxRotateY(mtx, 0.0f);

        // position x,y,z
        mtx[12] = 0.0f;
        mtx[13] = 0.0f;
        mtx[14] = 0.0f;

        // Set model matrix for rendering.
        bgfx::setTransform(mtx);

        m_vertexBuffer.enable();
       m_indexBuffer.enable();

        // Set render states.
        bgfx::setState(BGFX_STATE_DEFAULT);

        // Submit primitive for rendering to view 0.
        bgfx::submit(_viewId, m_shader.getProgram());

        bgfx::frame();
    }

    void Renderer::shutdown() {
        m_indexBuffer.free();
        m_vertexBuffer.free();
    }
}
