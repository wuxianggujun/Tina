/*#include "GuiSystem.hpp"
#include <bgfx/bgfx.h>
#include "tool/BgfxUtils.hpp"

namespace Tina {

bgfx::VertexLayout PosColorVertex::ms_layout;

void GuiSystem::Initialize(uint16_t width, uint16_t height) {
    m_width = width;
    m_height = height;
    
    PosColorVertex::init();
    InitializeShaders();
    CreateBuffers();
    
    // 设置正交投影
    bgfx::setViewRect(0, 0, 0, width, height);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
}

void GuiSystem::Shutdown() {
    if (bgfx::isValid(m_vbh)) {
        bgfx::destroy(m_vbh);
    }
    if (bgfx::isValid(m_ibh)) {
        bgfx::destroy(m_ibh);
    }
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
    }
}

void GuiSystem::Update(entt::registry& registry, float deltaTime) {
    auto view = registry.view<GuiComponent>();
    
    // 清除之前的顶点数据
    m_vertices.clear();
    m_indices.clear();
    
    for (auto entity : view) {
        auto& gui = view.get<GuiComponent>(entity);
        if (!gui.visible) continue;
        
        // 计算GUI元素的屏幕坐标
        float x = gui.position.x;
        float y = gui.position.y;
        float w = gui.size.x;
        float h = gui.size.y;
        
        // 转换为标准化设备坐标
        float nx = (x / m_width) * 2.0f - 1.0f;
        float ny = 1.0f - (y / m_height) * 2.0f;
        float nw = (w / m_width) * 2.0f;
        float nh = (h / m_height) * 2.0f;
        
        // 添加矩形顶点
        uint32_t color = 
            ((uint32_t)(gui.color.a * 255.0f) << 24) |
            ((uint32_t)(gui.color.b * 255.0f) << 16) |
            ((uint32_t)(gui.color.g * 255.0f) << 8) |
            ((uint32_t)(gui.color.r * 255.0f));
            
        uint16_t startIdx = static_cast<uint16_t>(m_vertices.size());
        
        m_vertices.push_back({nx, ny, 0.0f, color});
        m_vertices.push_back({nx + nw, ny, 0.0f, color});
        m_vertices.push_back({nx + nw, ny - nh, 0.0f, color});
        m_vertices.push_back({nx, ny - nh, 0.0f, color});
        
        // 添加索引
        m_indices.push_back(startIdx);
        m_indices.push_back(startIdx + 1);
        m_indices.push_back(startIdx + 2);
        m_indices.push_back(startIdx);
        m_indices.push_back(startIdx + 2);
        m_indices.push_back(startIdx + 3);
    }
}

void GuiSystem::Render(entt::registry& registry) {
    if (m_vertices.empty()) return;
    
    // 更新顶点和索引缓冲
    const bgfx::Memory* vbMem = bgfx::copy(m_vertices.data(), m_vertices.size() * sizeof(PosColorVertex));
    const bgfx::Memory* ibMem = bgfx::copy(m_indices.data(), m_indices.size() * sizeof(uint16_t));
    
    bgfx::update(m_vbh, 0, vbMem);
    bgfx::update(m_ibh, 0, ibMem);
    
    // 设置渲染状态
    bgfx::setState(BGFX_STATE_WRITE_RGB 
                | BGFX_STATE_WRITE_A 
                | BGFX_STATE_BLEND_ALPHA
                | BGFX_STATE_DEPTH_TEST_LESS  // 添加深度测试
                | BGFX_STATE_MSAA);           // 添加多重采样
                
    // 设置视图矩阵和投影矩阵
    float view[16];
    bx::mtxIdentity(view);
    
    float proj[16];
    bx::mtxOrtho(proj, 
        0.0f, static_cast<float>(m_width),
        static_cast<float>(m_height), 0.0f,
        0.0f, 100.0f,
        0.0f,
        bgfx::getCaps()->homogeneousDepth);
        
    bgfx::setViewTransform(0, view, proj);
    bgfx::setViewRect(0, 0, 0, m_width, m_height);
                
    // 提交绘制命令
    bgfx::setVertexBuffer(0, m_vbh);
    bgfx::setIndexBuffer(m_ibh);
    bgfx::submit(0, m_program);
}

void GuiSystem::InitializeShaders() {
    m_program = BgfxUtils::loadProgram("gui.vs", "gui.fs");
    if (!bgfx::isValid(m_program)) {
        throw std::runtime_error("Failed to load GUI shaders");
    }
}

void GuiSystem::CreateBuffers() {
    // 创建动态顶点缓冲
    m_vbh = bgfx::createDynamicVertexBuffer(
        1024, // 初始大小
        PosColorVertex::ms_layout,
        BGFX_BUFFER_ALLOW_RESIZE
    );
    
    // 创建动态索引缓冲
    m_ibh = bgfx::createDynamicIndexBuffer(
        1024, // 初始大小
        BGFX_BUFFER_ALLOW_RESIZE
    );
}

void GuiSystem::Resize(uint16_t width, uint16_t height) {
    m_width = width;
    m_height = height;
}

}  // namespace Engine */