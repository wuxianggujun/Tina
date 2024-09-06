#include "Renderer.hpp"
#include <fstream>

#include "bgfx/platform.h"
#include "bx/math.h"

namespace Tina {
    /*const bgfx::EmbeddedShader shaders[3] = {
        BGFX_EMBEDDED_SHADER(vs_simple),
        BGFX_EMBEDDED_SHADER(fs_simple),
        BGFX_EMBEDDED_SHADER_END()
    };*/

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


    bgfx::VertexLayout Renderer::PosColorVertex::ms_decl;


    Renderer::Renderer(Vector2i size, int viewId): _resolution(size), _viewId(viewId),
                                                   _programHandle(BGFX_INVALID_HANDLE) {
        bgfx::setDebug(BGFX_DEBUG_TEXT);

        PosColorVertex::init();

        bx::FilePath imageFilePath("./resource/Tina.jpg");
        bx::FileReader fileReader;

        m_imageTexture = BgfxUtils::loadTexture(&fileReader, imageFilePath);

        m_cubeTexture = bgfx::createTexture2D(size.width, size.height, false, 1, bgfx::TextureFormat::RGBA8,
                                              BGFX_TEXTURE_RT);

        m_frameBuffer = bgfx::createFrameBuffer(1, &m_cubeTexture, false);


        m_cubeVBH = bgfx::createVertexBuffer(bgfx::makeRef(s_cubeVertices, sizeof(s_cubeVertices)),
                                             PosColorVertex::ms_decl);
        m_cubeIBH[0] = bgfx::createIndexBuffer(bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList)));
        m_cubeIBH[1] = bgfx::createIndexBuffer(bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList)));
        m_cubeIBH[2] = bgfx::createIndexBuffer(bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList)));
        m_cubeIBH[3] = bgfx::createIndexBuffer(bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList)));
        m_cubeIBH[4] = bgfx::createIndexBuffer(bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList)));

        bgfx::RendererType::Enum type = bgfx::getRendererType();

        /*
        _programHandle = bgfx::createProgram(bgfx::createEmbeddedShader(shaders, type, "vs_simple"),
                                             bgfx::createEmbeddedShader(shaders, type, "fs_simple"), true);
                                             */

        //bgfx::setViewFrameBuffer(_viewId, m_frameBuffer);
        bgfx::setViewClear(0,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
    }

    void Renderer::render() {
        const bx::Vec3 at = {0.0f, 0.0f, 0.0f};
        const bx::Vec3 eye = {0.0f, 0.0f, 10.0f};

        // Set view and projection matrix for view 0.
        float view[16];
        bx::mtxLookAt(view, eye, at);

        float proj[16];
        bx::mtxProj(proj,
                    60.0f,
                    float(_resolution.width) / float(_resolution.height),
                    0.1f, 100.0f,
                    bgfx::getCaps()->homogeneousDepth);

        bgfx::setViewTransform(-_viewId, view, proj);

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

        // Set vertex and index buffer.
        bgfx::setVertexBuffer(-_viewId, m_cubeVBH);
        bgfx::setIndexBuffer(m_cubeIBH[0]);

        // Set render states.
        bgfx::setState(BGFX_STATE_DEFAULT);

        // Submit primitive for rendering to view 0.
        bgfx::submit(_viewId, _programHandle);

        bgfx::frame();
    }

    void Renderer::shutdown() {
        bgfx::destroy(m_cubeVBH);
        bgfx::shutdown();
    }
}
