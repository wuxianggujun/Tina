//
// SceneRenderer - Scene专用高层渲染器
// 用途：提供背景、遮罩、全屏特效等常用绘制功能
// 优势：Scene无需了解bgfx底层API、着色器、顶点布局
//

#pragma once

#include <bgfx/bgfx.h>
#include "../core/Color.hpp"
#include "../core/Memory.hpp"
#include "../renderer/ShaderManager.hpp"

namespace Tina::Engine {

// Scene专用渲染器：封装底层细节，提供高层API
class SceneRenderer {
public:
    SceneRenderer() = default;
    ~SceneRenderer() = default;
    
    // 初始化（Scene基类自动调用，Scene子类无需关心）
    void initialize(Renderer::ShaderManager& shaders, int screenWidth, int screenHeight);
    
    // 更新屏幕尺寸（窗口调整时调用）
    void setScreenSize(int width, int height) {
        m_screenWidth = width;
        m_screenHeight = height;
    }
    
    // === 高层绘制API（Scene直接调用，简单易用） ===
    
    // 绘制全屏渐变背景（从上到下）
    // 用途：菜单背景、游戏场景背景
    void drawGradientBackground(uint16_t viewId, 
                                const Core::Color& topColor, 
                                const Core::Color& bottomColor);
    
    // 绘制纯色背景
    // 用途：简单场景背景
    void drawSolidBackground(uint16_t viewId, const Core::Color& color);
    
    // 绘制半透明遮罩（覆盖整个屏幕）
    // 用途：暂停菜单遮罩、模态对话框背景
    void drawOverlay(uint16_t viewId, const Core::Color& color);
    
    // 绘制矩形（通用API）
    // 用途：自定义绘制
    void drawRect(uint16_t viewId, float x, float y, float w, float h, 
                  const Core::Color& color);
    
private:
    bgfx::ProgramHandle m_progColor = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_colorLayout{};
    int m_screenWidth = 1280;
    int m_screenHeight = 720;
    bool m_initialized = false;
};

} // namespace Tina::Engine
