//
// Created by wuxianggujun on 2024/5/19.
//

#include "RenderContext.hpp"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace Tina {
    RenderContext::~RenderContext() {
    }

    void RenderContext::initialize(GraphicsBackend backend, void *hwd) {
        bgfx::Init initDesc;
        switch (backend) {
            case GraphicsBackend::OpenGL:
                initDesc.type = bgfx::RendererType::OpenGL;
                break;
            case GraphicsBackend::OpenGLES:
                initDesc.type = bgfx::RendererType::OpenGLES;
                break;
            case GraphicsBackend::Direct3DX11:
                initDesc.type = bgfx::RendererType::Direct3D11;
                break;
            case GraphicsBackend::Direct3DX12:
                initDesc.type = bgfx::RendererType::Direct3D12;
                break;
            case GraphicsBackend::Vulkan:
                initDesc.type = bgfx::RendererType::Vulkan;
                break;
            case GraphicsBackend::Metal:
                initDesc.type = bgfx::RendererType::Metal;
                break;
            case GraphicsBackend::Noop:
            default: {
                initDesc.type = bgfx::RendererType::Noop;
                break;
            }
        }

#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
        bgfxInit.platformData.ndt = glfwGetX11Display();
#endif

        initDesc.platformData.nwh = hwd;
        bgfx::init(initDesc);
    }

    void RenderContext::onResize(uint16_t width, uint16_t height) {
        bgfx::reset(width, height, BGFX_RESET_VSYNC);
        backBufferWidth = width;
        backBufferHeight = height;
    }

    void RenderContext::beginFrame() {
    }

    void RenderContext::submit(uint16_t viewID, uint16_t programHandle) {
    }

    void RenderContext::endFrame() {
        bgfx::frame();
    }
} // Tina
