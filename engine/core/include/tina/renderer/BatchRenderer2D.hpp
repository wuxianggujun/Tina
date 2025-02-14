#pragma once

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include "tina/renderer/Color.hpp"
#include "tina/renderer/Texture2D.hpp"
#include "tina/renderer/RenderCommand.hpp"
#include <vector>
#include <memory>
#include <mutex>

namespace Tina {

// 渲染命令
class DrawQuadBatchCommand : public RenderCommand {
public:
    DrawQuadBatchCommand(const glm::vec2& position, const glm::vec2& size, const Color& color)
        : m_Position(position), m_Size(size), m_Color(color) {}

    void execute() override;

private:
    glm::vec2 m_Position;
    glm::vec2 m_Size;
    Color m_Color;
};

class DrawTexturedQuadBatchCommand : public RenderCommand {
public:
    DrawTexturedQuadBatchCommand(const glm::vec2& position, const glm::vec2& size,
                                const std::shared_ptr<Texture2D>& texture,
                                const glm::vec4& textureCoords, const Color& tint)
        : m_Position(position), m_Size(size), m_Texture(texture),
          m_TextureCoords(textureCoords), m_Tint(tint) {}

    void execute() override;

private:
    glm::vec2 m_Position;
    glm::vec2 m_Size;
    std::shared_ptr<Texture2D> m_Texture;
    glm::vec4 m_TextureCoords;
    Color m_Tint;
};

class BatchRenderer2D {
public:
    static constexpr uint32_t MAX_QUADS = 20000;
    static constexpr uint32_t MAX_VERTICES = MAX_QUADS * 4;
    static constexpr uint32_t MAX_INDICES = MAX_QUADS * 6;
    static constexpr uint32_t MAX_TEXTURE_SLOTS = 16;
    
    struct QuadVertex {
        glm::vec3 Position;
        glm::vec2 TexCoord;
        uint32_t Color;
    };

    struct InstanceData {
        glm::vec4 Transform;    // x, y, scale_x, scale_y
        glm::vec4 Color;       // rgba color
        glm::vec4 TextureData; // UV coordinates
        float TextureIndex;    // texture slot
        float Padding[3];      // alignment padding
    };

    BatchRenderer2D();
    ~BatchRenderer2D();

    BatchRenderer2D(const BatchRenderer2D&) = delete;
    BatchRenderer2D& operator=(const BatchRenderer2D&) = delete;

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

    void drawTexturedQuads(const std::vector<InstanceData>& instances,
                          const std::vector<std::shared_ptr<Texture2D>>& textures);

private:
    void createBuffers();
    void flushColorBatch();    // 刷新纯色批次
    void flushTextureBatch();  // 刷新纹理批次
    void flushColorBatchInternal();  // 内部刷新纯色批次(无锁)
    void flushTextureBatchInternal();  // 内部刷新纹理批次(无锁)

    friend class DrawQuadBatchCommand;
    friend class DrawTexturedQuadBatchCommand;

private:
    bgfx::ProgramHandle m_Shader = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_VertexBuffer = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_IndexBuffer = BGFX_INVALID_HANDLE;
    
    bgfx::DynamicVertexBufferHandle m_ColorInstanceBuffer = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle m_TexturedInstanceBuffer = BGFX_INVALID_HANDLE;
    
    bgfx::UniformHandle m_TextureSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_UseTexture = BGFX_INVALID_HANDLE;

    static bgfx::VertexLayout s_VertexLayout;
    static bgfx::VertexLayout s_InstanceLayout;
    
    std::vector<InstanceData> m_ColorInstances;
    std::vector<InstanceData> m_TexturedInstances;
    std::vector<std::shared_ptr<Texture2D>> m_Textures;
    
    uint32_t m_ColorQuadCount = 0;
    uint32_t m_TexturedQuadCount = 0;
    bool m_Initialized = false;

    std::mutex m_Mutex; // 用于线程同步
};

// 便捷函数
inline void submitDrawQuad(const glm::vec2& position, const glm::vec2& size, const Color& color) {
    submitRenderCommand<DrawQuadBatchCommand>(position, size, color);
}

inline void submitDrawTexturedQuad(const glm::vec2& position, const glm::vec2& size,
                                 const std::shared_ptr<Texture2D>& texture,
                                 const glm::vec4& textureCoords = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                                 const Color& tint = Color::White) {
    submitRenderCommand<DrawTexturedQuadBatchCommand>(position, size, texture, textureCoords, tint);
}

} // namespace Tina 