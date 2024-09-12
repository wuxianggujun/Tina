#include "Renderer.hpp"
#include <fstream>

#include "bgfx/platform.h"
#include "bx/math.h"

namespace Tina {
    static Renderer::PosColorVertex s_cubeVertices[] =
    {
        {-1.0f, 1.0f, 1.0f, 0xff000000},
        {1.0f, 1.0f, 1.0f, 0xff0000ff},
        {-1.0f, -1.0f, 1.0f, 0xff00ff00},
        {1.0f, -1.0f, 1.0f, 0xff00ffff},
        {-1.0f, 1.0f, -1.0f, 0xffff0000},
        {1.0f, 1.0f, -1.0f, 0xffff00ff},
        {-1.0f, -1.0f, -1.0f, 0xffffff00},
        {1.0f, -1.0f, -1.0f, 0xffffffff}
    };

    static const uint16_t s_cubeTriList[] =
    {
        0, 1, 2, // 0
        1, 3, 2,
        4, 6, 5, // 2
        5, 6, 7,
        0, 2, 4, // 4
        4, 2, 6,
        1, 5, 3, // 6
        5, 7, 3,
        0, 4, 1, // 8
        4, 5, 1,
        2, 3, 6, // 10
        6, 3, 7,
    };

    static const uint16_t s_cubeTriStrip[] =
    {
        0, 1, 2,
        3,
        7,
        1,
        5,
        0,
        4,
        2,
        6,
        7,
        4,
        5,
    };

    static const uint16_t s_cubeLineList[] =
    {
        0, 1,
        0, 2,
        0, 4,
        1, 3,
        1, 5,
        2, 3,
        2, 6,
        3, 7,
        4, 5,
        4, 6,
        5, 7,
        6, 7,
    };

    static const uint16_t s_cubeLineStrip[] =
    {
        0, 2, 3, 1, 5, 7, 6, 4,
        0, 2, 6, 4, 5, 7, 3, 1,
        0,
    };

    static const uint16_t s_cubePoints[] =
    {
        0, 1, 2, 3, 4, 5, 6, 7
    };

    static const char *s_ptNames[]
    {
        "Triangle List",
        "Triangle Strip",
        "Lines",
        "Line Strip",
        "Points",
    };

    static const uint64_t s_ptState[]
    {
        UINT64_C(0),
        BGFX_STATE_PT_TRISTRIP,
        BGFX_STATE_PT_LINES,
        BGFX_STATE_PT_LINESTRIP,
        BGFX_STATE_PT_POINTS,
    };
    BX_STATIC_ASSERT(BX_COUNTOF(s_ptState) == BX_COUNTOF(s_ptNames));


    Renderer::Renderer(Vector2i size, int viewId): _resolution(size), _viewId(viewId) {
        // bgfx::setDebug(BGFX_DEBUG_TEXT);
        // bgfx::setDebug(BGFX_DEBUG_WIREFRAME);

        m_vertexBuffer.getLayout().begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
                .end();


        m_vertexBuffer.init(s_cubeVertices, sizeof(s_cubeVertices));

        m_indexBuffer[0].init(s_cubeTriList, sizeof(s_cubeTriList));
        m_indexBuffer[1].init(s_cubeTriStrip, sizeof(s_cubeTriStrip));
        m_indexBuffer[2].init(s_cubeLineList, sizeof(s_cubeLineList));
        m_indexBuffer[3].init(s_cubeLineStrip, sizeof(s_cubeLineStrip));
        m_indexBuffer[4].init(s_cubePoints, sizeof(s_cubePoints));


        m_shader.loadFromFile("cubes");

        bgfx::setViewClear(0,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
    }

    void Renderer::render() {
        constexpr bx::Vec3 at = {0.0f, 0.0f, 0.0f};
        constexpr bx::Vec3 eye = {0.0f, 0.0f, -35.0f};

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

        for (uint32_t yy = 0; yy < 11; ++yy) {
            for (uint32_t xx = 0; xx < 11; ++xx) {
                float mtx[16];
                bx::mtxRotateXY(mtx, xx * 0.21f, yy * 0.37f);
                // position x,y,z
                mtx[12] = -15.0f + static_cast<float>(xx) * 3.0f;
                mtx[13] = -15.0f + static_cast<float>(yy) * 3.0f;
                mtx[14] = 0.0f;

                // Set model matrix for rendering.
                bgfx::setTransform(mtx);

                m_vertexBuffer.enable();
                m_indexBuffer[0].enable();

                // Set render states.
                bgfx::setState(BGFX_STATE_DEFAULT);

                // Submit primitive for rendering to view 0.
                bgfx::submit(_viewId, m_shader.getProgram());
            }
        }

        bgfx::frame();
    }

    void Renderer::shutdown() {
        m_indexBuffer[0].free();
        m_vertexBuffer.free();
    }
}
