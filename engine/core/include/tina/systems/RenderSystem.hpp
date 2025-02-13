#pragma once

#include <entt/entt.hpp>
#include "tina/components/RenderableComponent.hpp"
#include "tina/components/Transform2DComponent.hpp"
#include "tina/components/SpriteComponent.hpp"
#include "tina/core/OrthographicCamera.hpp"
#include <vector>
#include <memory>

namespace Tina {

class RenderSystem {
public:
    static constexpr uint32_t MAX_QUADS = 10000;
    static constexpr uint32_t MAX_VERTICES = MAX_QUADS * 4;
    static constexpr uint32_t MAX_INDICES = MAX_QUADS * 6;

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