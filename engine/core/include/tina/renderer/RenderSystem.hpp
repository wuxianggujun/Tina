#pragma once

#include <entt/entt.hpp>
#include "tina/components/RenderableComponent.hpp"
#include "tina/components/Transform2DComponent.hpp"
#include "tina/components/SpriteComponent.hpp"
#include "tina/core/OrthographicCamera.hpp"
#include <vector>
#include <memory>

#include "tina/components/RectangleComponent.hpp"

namespace Tina {

class RenderSystem {
public:
    static constexpr uint32_t MAX_QUADS = 20000;
    static constexpr uint32_t MAX_VERTICES = MAX_QUADS * 4;
    static constexpr uint32_t MAX_INDICES = MAX_QUADS * 6;
    static constexpr uint32_t MAX_TEXTURE_SLOTS = 16;
    static constexpr uint32_t VERTEX_BUFFER_SIZE = MAX_VERTICES * sizeof(RectangleComponent::Vertex);
    static constexpr uint32_t INDEX_BUFFER_SIZE = MAX_INDICES * sizeof(uint16_t);

    RenderSystem();
    ~RenderSystem();

    void init(bgfx::ProgramHandle shader);
    void shutdown();

    void beginScene(const OrthographicCamera& camera);
    void endScene();

    void render(entt::registry& registry);

private:
    struct RenderBatch {
        bgfx::DynamicVertexBufferHandle vertexBuffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle indexBuffer = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        uint32_t quadCount = 0;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
    };

    void initBatch();
    void flushBatch(RenderBatch& batch);
    bool shouldStartNewBatch(const RenderBatch& batch, const SpriteComponent* sprite) const;
    
    bool isValidBatch(const RenderBatch& batch) const {
        return bgfx::isValid(batch.vertexBuffer) && bgfx::isValid(batch.indexBuffer);
    }

    bool canAddToCurrentBatch(uint32_t additionalQuads) const {
        if (!m_CurrentBatch) return false;
        return (m_CurrentBatch->quadCount + additionalQuads) <= MAX_QUADS;
    }

private:
    bgfx::ProgramHandle m_Shader = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_TextureSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_UseTexture = BGFX_INVALID_HANDLE;

    std::vector<RenderBatch> m_Batches;
    RenderBatch* m_CurrentBatch = nullptr;

    std::vector<uint8_t> m_VertexBuffer;
    std::vector<uint16_t> m_IndexBuffer;

    bool m_Initialized = false;
};

} // namespace Tina 