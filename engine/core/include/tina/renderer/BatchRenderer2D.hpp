#pragma once

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
    // 减少最大quad数量,因为大多数2D场景不需要这么多
    static constexpr uint32_t MAX_QUADS = 1000;
    static constexpr uint32_t MAX_VERTICES = MAX_QUADS * 4;
    static constexpr uint32_t MAX_INDICES = MAX_QUADS * 6;
    static constexpr uint32_t MAX_TEXTURE_SLOTS = 16;
    
    struct QuadVertex {
        glm::vec3 Position;
        glm::vec2 TexCoord;
        uint32_t Color;
    };

    // 实例数据结构 - 保持与shader一致的布局
    struct InstanceData {
        glm::vec4 Transform;    // x, y, width, height (i_data0)
        glm::vec4 Color;       // rgba color (i_data1)
        glm::vec4 TextureData; // UV coordinates (i_data2)
        glm::vec4 TextureInfo; // x: texture index, yzw: padding (i_data3)
    };

    BatchRenderer2D();
    ~BatchRenderer2D();

    BatchRenderer2D(const BatchRenderer2D&) = delete;
    BatchRenderer2D& operator=(const BatchRenderer2D&) = delete;

    uint16_t m_ViewId = 0;
    void setViewId(uint16_t viewId) { m_ViewId = viewId; }
    uint16_t getViewId() const { return m_ViewId; }

    void init(bgfx::ProgramHandle shader);
    void shutdown();

    void begin();
    void end();

    void flushBatch(uint16_t viewId);

    void setViewTransform(const glm::mat4& view,const glm::mat4& proj);
    void setViewRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    void setViewClear(uint16_t flags, uint32_t rgba = 0x303030ff, float depth = 1.0f, uint8_t stencil = 0);


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

    bgfx::ProgramHandle m_Shader = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_VertexBuffer = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_IndexBuffer = BGFX_INVALID_HANDLE;

    bgfx::DynamicVertexBufferHandle m_InstanceBuffer = BGFX_INVALID_HANDLE;

    const std::string SAMPLER_UNIFORM_NAME = "s_texColor";
    const std::string USE_TEXTURE_UNIFORM_NAME = "u_useTexture";

    static bgfx::VertexLayout s_VertexLayout;
    static bgfx::VertexLayout s_InstanceLayout;

    std::vector<InstanceData> m_Instances;
    std::vector<std::shared_ptr<Texture2D>> m_Textures;
    uint32_t m_QuadCount = 0;

    bool m_Initialized = false;
    std::mutex m_Mutex;
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