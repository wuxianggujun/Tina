#include "Renderer.hpp"
#include <fstream>

#include "bgfx/platform.h"
#include "bx/math.h"

namespace Tina {
    const bgfx::EmbeddedShader shaders[3] = {
        BGFX_EMBEDDED_SHADER(vs_simple),
        BGFX_EMBEDDED_SHADER(fs_simple),
        BGFX_EMBEDDED_SHADER_END()
    };

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

    bgfx::VertexLayout Renderer::PosColorVertex::ms_decl;


    Renderer::Renderer(Vector2i size, int viewId): _resolution(size), _viewId(viewId),
                                                   _programHandle(BGFX_INVALID_HANDLE) {
        bgfx::setDebug(BGFX_DEBUG_TEXT);

        PosColorVertex::init();

        bx::FilePath imageFilePath("./resource/Tina.jpg");
        bx::FileReader fileReader;
        
        m_imageTexture = BgfxUtils::loadTexture(&fileReader,imageFilePath);
        
        m_cubeTexture = bgfx::createTexture2D(size.width, size.height, false, 1, bgfx::TextureFormat::RGBA8,
                                              BGFX_TEXTURE_RT);

        m_frameBuffer = bgfx::createFrameBuffer(1, &m_cubeTexture, false);


        m_cubeVBH = bgfx::createVertexBuffer(bgfx::makeRef(s_cubeVertices, sizeof(s_cubeVertices)),
                                             PosColorVertex::ms_decl);
        m_cubeIBH = bgfx::createIndexBuffer(bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList)));

        bgfx::RendererType::Enum type = bgfx::getRendererType();

        _programHandle = bgfx::createProgram(bgfx::createEmbeddedShader(shaders, type, "vs_simple"),
                                             bgfx::createEmbeddedShader(shaders, type, "fs_simple"), true);

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
        bgfx::setIndexBuffer(m_cubeIBH);

        // Set render states.
        bgfx::setState(BGFX_STATE_DEFAULT);

        // Submit primitive for rendering to view 0.
        bgfx::submit(_viewId, _programHandle);

        bgfx::frame();
    }

    void Renderer::shutdown() {
        bgfx::shutdown();
    }


}
