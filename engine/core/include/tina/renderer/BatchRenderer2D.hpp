#pragma once

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include "tina/renderer/Color.hpp"
#include "tina/renderer/Texture2D.hpp"
#include <vector>
#include <memory>

namespace Tina {

class BatchRenderer2D {
public:
    static constexpr uint32_t MAX_QUADS = 20000;
    static constexpr uint32_t MAX_VERTICES = MAX_QUADS * 4;
    static constexpr uint32_t MAX_INDICES = MAX_QUADS * 6;
    
    struct QuadVertex {
        glm::vec3 Position;
        glm::vec2 TexCoord;
        uint32_t Color;
    };

    struct InstanceData {
        glm::vec4 Transform;    // x, y, scale_x, scale_y
        glm::vec4 Color;       // rgba color as normalized floats
        glm::vec4 TextureData; // x,y,w,h for UV coordinates
        float TextureIndex;    // texture slot index
        float Padding[3];      // padding for alignment
    };

    BatchRenderer2D();
    ~BatchRenderer2D();

    void init(bgfx::ProgramHandle shader);
    void shutdown();

    void begin();
    void end();

    // 快速绘制方法
    void drawQuad(const glm::vec2& position, const glm::vec2& size, const Color& color);
    void drawQuads(const std::vector<InstanceData>& instances);

    // 纹理绘制方法
    void drawTexturedQuad(const glm::vec2& position, const glm::vec2& size, 
                         const std::shared_ptr<Texture2D>& texture,
                         const glm::vec4& textureCoords = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                         const Color& tint = Color::White);

    // 批量纹理绘制
    void drawTexturedQuads(const std::vector<InstanceData>& instances,
                          const std::vector<std::shared_ptr<Texture2D>>& textures);

private:
    void createBuffers();
    void flushBatch();

private:
    bgfx::ProgramHandle m_Shader = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_VertexBuffer = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_IndexBuffer = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle m_InstanceBuffer = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_TextureSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_UseTexture = BGFX_INVALID_HANDLE;

    static bgfx::VertexLayout s_VertexLayout;
    static bgfx::VertexLayout s_InstanceLayout;
    
    std::vector<InstanceData> m_Instances;
    std::vector<std::shared_ptr<Texture2D>> m_Textures;
    uint32_t m_QuadCount = 0;
    bool m_Initialized = false;
};

} // namespace Tina 