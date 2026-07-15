//
// SceneRenderer 实现
//

#include "SceneRenderer.hpp"
#include "../core/Log.hpp"
#include <cstring>

namespace Tina::Engine {

void SceneRenderer::initialize(Renderer::ShaderManager& shaders, int screenWidth, int screenHeight) {
    if (m_initialized) return;
    
    // 自动加载着色器（Scene无需关心）
    m_progColor = shaders.loadProgram("color", "color");
    if (!bgfx::isValid(m_progColor)) {
        TINA_ERROR("SceneRenderer: 加载color着色器失败");
        return;
    }
    
    // 自动设置顶点布局（Position + Color as float4）
    m_colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Float)
        .end();
    
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_initialized = true;
    
    TINA_INFO("SceneRenderer: 初始化成功 ({}x{})", screenWidth, screenHeight);
}

void SceneRenderer::drawGradientBackground(uint16_t viewId, 
                                           const Core::Color& topColor, 
                                           const Core::Color& bottomColor) {
    if (!m_initialized || !bgfx::isValid(m_progColor)) return;
    
    // 顶点结构（Scene无需了解）
    struct Vertex {
        float x, y, z;
        float r, g, b, a;
    };
    
    // 构建全屏四边形（渐变从上到下）
    Vertex vertices[4] = {
        { 0.0f,                 0.0f,                  0.0f, topColor.r(), topColor.g(), topColor.b(), topColor.a() },
        { (float)m_screenWidth, 0.0f,                  0.0f, topColor.r(), topColor.g(), topColor.b(), topColor.a() },
        { (float)m_screenWidth, (float)m_screenHeight, 0.0f, bottomColor.r(), bottomColor.g(), bottomColor.b(), bottomColor.a() },
        { 0.0f,                 (float)m_screenHeight, 0.0f, bottomColor.r(), bottomColor.g(), bottomColor.b(), bottomColor.a() },
    };
    
    const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    
    // 分配瞬态缓冲（Scene无需了解）
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    
    if (bgfx::getAvailTransientVertexBuffer(4, m_colorLayout) < 4 ||
        bgfx::getAvailTransientIndexBuffer(6) < 6) {
        TINA_WARN("SceneRenderer: 瞬态缓冲不足");
        return;
    }
    
    bgfx::allocTransientVertexBuffer(&tvb, 4, m_colorLayout);
    bgfx::allocTransientIndexBuffer(&tib, 6);
    std::memcpy(tvb.data, vertices, sizeof(vertices));
    std::memcpy(tib.data, indices, sizeof(indices));
    
    // 提交渲染（Scene无需了解Encoder）
    bgfx::Encoder* enc = bgfx::begin();
    if (enc) {
        enc->setVertexBuffer(0, &tvb);
        enc->setIndexBuffer(&tib);
        enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        enc->submit(viewId, m_progColor);
        bgfx::end(enc);
    }
}

void SceneRenderer::drawSolidBackground(uint16_t viewId, const Core::Color& color) {
    // 纯色背景就是上下同色的渐变
    drawGradientBackground(viewId, color, color);
}

void SceneRenderer::drawOverlay(uint16_t viewId, const Core::Color& color) {
    // 遮罩就是全屏矩形
    drawRect(viewId, 0, 0, (float)m_screenWidth, (float)m_screenHeight, color);
}

void SceneRenderer::drawRect(uint16_t viewId, float x, float y, float w, float h, 
                             const Core::Color& color) {
    if (!m_initialized || !bgfx::isValid(m_progColor)) return;
    
    struct Vertex {
        float x, y, z;
        float r, g, b, a;
    };
    
    Vertex vertices[4] = {
        { x,     y,     0.0f, color.r(), color.g(), color.b(), color.a() },
        { x + w, y,     0.0f, color.r(), color.g(), color.b(), color.a() },
        { x + w, y + h, 0.0f, color.r(), color.g(), color.b(), color.a() },
        { x,     y + h, 0.0f, color.r(), color.g(), color.b(), color.a() },
    };
    
    const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
    
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    
    if (bgfx::getAvailTransientVertexBuffer(4, m_colorLayout) < 4 ||
        bgfx::getAvailTransientIndexBuffer(6) < 6) {
        return;
    }
    
    bgfx::allocTransientVertexBuffer(&tvb, 4, m_colorLayout);
    bgfx::allocTransientIndexBuffer(&tib, 6);
    std::memcpy(tvb.data, vertices, sizeof(vertices));
    std::memcpy(tib.data, indices, sizeof(indices));
    
    bgfx::Encoder* enc = bgfx::begin();
    if (enc) {
        enc->setVertexBuffer(0, &tvb);
        enc->setIndexBuffer(&tib);
        enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        enc->submit(viewId, m_progColor);
        bgfx::end(enc);
    }
}

} // namespace Tina::Engine
