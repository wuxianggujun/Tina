#include "Renderer.hpp"
#include <fstream>

#include "bgfx/platform.h"
#include "bx/math.h"

namespace Tina {

    bgfx::VertexLayout PosColorVertex::ms_decl;

    static PosColorVertex s_cubeVertices[] =
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

    // 保存实际的顶点缓冲区和索引缓冲区
    bgfx::VertexBufferHandle m_cubeVBH;
    bgfx::IndexBufferHandle m_cubeIBH;
    

    Renderer::Renderer(Vector2i size, int viewId): _resolution(size), _viewId(viewId), _programHandle(BGFX_INVALID_HANDLE) {
        bgfx::setDebug(BGFX_DEBUG_TEXT);

        PosColorVertex::init();

        m_cubeVBH = bgfx::createVertexBuffer(bgfx::makeRef(s_cubeVertices, sizeof(s_cubeVertices)),
                                             PosColorVertex::ms_decl);
        m_cubeIBH = bgfx::createIndexBuffer(bgfx::makeRef(s_cubeTriList, sizeof(s_cubeTriList)));
        
        // 创建 FileReader 对象
        FileReader reader;
        bx::Error error;
        // 尝试打开文件
        if (!reader.open(bx::FilePath("./engine/assets/shaders"), &error)) {
            // 处理打开文件失败的情况
            printf("Failed to open shader directory\n");
            // 可能需要设置一个无效的程序句柄或采取其他错误恢复措施
        } else {
            // 打开成功，使用 reader 调用 loadProgram
            _programHandle = loadProgram(&reader, "vs_cubes", "fs_cubes");
            // 关闭 FileReader，如果不需要保持打开状态的话
            bx::close(&reader);
        }

        
        bgfx::setViewClear(0,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
    }

    Renderer::~Renderer() {
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
