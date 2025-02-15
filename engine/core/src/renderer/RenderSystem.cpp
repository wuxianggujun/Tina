/*#include "tina/renderer/RenderSystem.hpp"

#include <entt/entity/registry.hpp>

#include "tina/log/Logger.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace Tina {

RenderSystem::RenderSystem() {
    TINA_LOG_INFO("Creating RenderSystem");
    
    try {
        // 预分配缓冲区
        m_VertexBuffer.resize(VERTEX_BUFFER_SIZE);
        m_IndexBuffer.resize(MAX_INDICES);
        
        // 预先创建一个批次
        m_Batches.push_back(RenderBatch());
        m_CurrentBatch = &m_Batches.back();
        
        TINA_LOG_INFO("RenderSystem created successfully");
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to create RenderSystem: {}", e.what());
        throw;
    }
}

RenderSystem::~RenderSystem() {
    shutdown();
}

void RenderSystem::init(bgfx::ProgramHandle shader) {
    if (m_Initialized) {
        TINA_LOG_WARN("RenderSystem already initialized");
        return;
    }

    if (!bgfx::isValid(shader)) {
        TINA_LOG_ERROR("Invalid shader handle");
        return;
    }

    try {
        TINA_LOG_INFO("Initializing RenderSystem");
        
        m_Shader = shader;
        
        // 创建Uniforms
        m_TextureSampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        m_UseTexture = bgfx::createUniform("u_useTexture", bgfx::UniformType::Vec4);

        if (!bgfx::isValid(m_TextureSampler) || !bgfx::isValid(m_UseTexture)) {
            throw std::runtime_error("Failed to create uniforms");
        }

        // 初始化顶点布局
        RectangleComponent::initVertexLayout();

        // 预先填充索引缓冲区
        uint32_t offset = 0;
        for (uint32_t i = 0; i < MAX_INDICES; i += 6) {
            m_IndexBuffer[i + 0] = offset + 0;
            m_IndexBuffer[i + 1] = offset + 1;
            m_IndexBuffer[i + 2] = offset + 2;
            m_IndexBuffer[i + 3] = offset + 2;
            m_IndexBuffer[i + 4] = offset + 3;
            m_IndexBuffer[i + 5] = offset + 0;
            offset += 4;
        }

        // 初始化第一个批次
        if (!m_CurrentBatch) {
            TINA_LOG_ERROR("Current batch is null");
            throw std::runtime_error("Current batch is null");
        }
        
        initBatch();

        m_Initialized = true;
        TINA_LOG_INFO("RenderSystem initialized successfully");
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to initialize RenderSystem: {}", e.what());
        shutdown();
        throw;
    }
}

void RenderSystem::shutdown() {
    if (!m_Initialized) {
        return;
    }

    TINA_LOG_INFO("Shutting down RenderSystem");

    try {
        // 销毁所有批次的资源
        for (auto& batch : m_Batches) {
            if (bgfx::isValid(batch.vertexBuffer)) {
                bgfx::destroy(batch.vertexBuffer);
                batch.vertexBuffer = BGFX_INVALID_HANDLE;
            }
            if (bgfx::isValid(batch.indexBuffer)) {
                bgfx::destroy(batch.indexBuffer);
                batch.indexBuffer = BGFX_INVALID_HANDLE;
            }
        }
        m_Batches.clear();
        m_CurrentBatch = nullptr;

        // 销毁Uniforms
        if (bgfx::isValid(m_TextureSampler)) {
            bgfx::destroy(m_TextureSampler);
            m_TextureSampler = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_UseTexture)) {
            bgfx::destroy(m_UseTexture);
            m_UseTexture = BGFX_INVALID_HANDLE;
        }

        m_VertexBuffer.clear();
        m_IndexBuffer.clear();
        m_Initialized = false;
        TINA_LOG_INFO("RenderSystem shutdown complete");
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Error during RenderSystem shutdown: {}", e.what());
    }
}

void RenderSystem::beginScene(const OrthographicCamera& camera) {
    if (!m_Initialized) {
        TINA_LOG_WARN("RenderSystem not initialized");
        return;
    }

    TINA_LOG_DEBUG("Beginning scene");

    // 设置视图变换
    bgfx::setViewTransform(0, nullptr, glm::value_ptr(camera.getProjectionMatrix()));

    // 重置所有批次
    for (auto& batch : m_Batches) {
        batch.quadCount = 0;
        batch.vertexCount = 0;
        batch.indexCount = 0;
        batch.texture = BGFX_INVALID_HANDLE;
        }

    if (m_Batches.empty()) {
    m_Batches.push_back(RenderBatch());
    }
    m_CurrentBatch = &m_Batches[0];
}

void RenderSystem::endScene() {
    if (!m_Initialized) {
        return;
    }

    TINA_LOG_DEBUG("Ending scene");

    // Flush所有非空批次
    for (auto& batch : m_Batches) {
        if (batch.quadCount > 0 && isValidBatch(batch)) {
            flushBatch(batch);
        }
    }
}

void RenderSystem::render(entt::registry& registry) {
    if (!m_Initialized || !m_CurrentBatch) {
        return;
    }

    TINA_LOG_DEBUG("Rendering scene");

    // 先渲染普通矩形
    auto rectangleView = registry.view<RectangleComponent, Transform2DComponent>();
    for (auto entity : rectangleView) {
        auto [renderable, transform] = rectangleView.get<RectangleComponent, Transform2DComponent>(entity);
        
        if (!renderable.isVisible()) {
            continue;
        }

        // 检查是否可以添加到当前批次
        if (!canAddToCurrentBatch(1)) {
            flushBatch(*m_CurrentBatch);
            if (m_Batches.size() >= MAX_QUADS) {
                TINA_LOG_ERROR("Too many batches");
                return;
            }
            m_CurrentBatch = &m_Batches.emplace_back();
            initBatch();
        }

        // 获取变换矩阵
        glm::mat4 transformMatrix = transform.getTransform();

        // 更新顶点数据时应用变换
        size_t vertexOffset = m_CurrentBatch->vertexCount * sizeof(RectangleComponent::Vertex);
        if (vertexOffset + sizeof(RectangleComponent::Vertex) * 4 > m_VertexBuffer.size()) {
            TINA_LOG_ERROR("Vertex buffer overflow");
            return;
        }

        auto* vertices = reinterpret_cast<RectangleComponent::Vertex*>(m_VertexBuffer.data() + vertexOffset);

        // 先获取原始顶点数据
        renderable.updateVertexBuffer(vertices,
            sizeof(RectangleComponent::Vertex) * 4);

        // 对每个顶点应用变换
        for (int i = 0; i < 4; i++) {
            glm::vec4 pos(vertices[i].x, vertices[i].y, vertices[i].z, 1.0f);
            pos = transformMatrix * pos;
            vertices[i].x = pos.x;
            vertices[i].y = pos.y;
            vertices[i].z = pos.z;
        }

        // 更新索引数据
        size_t indexOffset = m_CurrentBatch->indexCount;
        if (indexOffset + 6 > m_IndexBuffer.size()) {
            TINA_LOG_ERROR("Index buffer overflow");
            return;
        }

        renderable.updateIndexBuffer(m_IndexBuffer.data() + indexOffset, 
            sizeof(uint16_t) * 6);

        // 更新计数
        m_CurrentBatch->quadCount++;
        m_CurrentBatch->vertexCount += 4;
        m_CurrentBatch->indexCount += 6;
    }

    // 然后渲染精灵
    auto spriteView = registry.view<SpriteComponent, Transform2DComponent>();
    for (auto entity : spriteView) {
        auto [sprite, transform] = spriteView.get<SpriteComponent, Transform2DComponent>(entity);
        
        if (!sprite.isVisible()) {
            continue;
        }

        // 检查是否需要开始新的批次
        if (shouldStartNewBatch(*m_CurrentBatch, &sprite)) {
            flushBatch(*m_CurrentBatch);
            if (m_Batches.size() >= MAX_QUADS) {
                TINA_LOG_ERROR("Too many batches");
                return;
            }
            m_CurrentBatch = &m_Batches.emplace_back();
            initBatch();
        }

        // 设置当前批次的纹理
        if (sprite.getTexture() && sprite.getTexture()->isValid()) {
            m_CurrentBatch->texture = sprite.getTexture()->getNativeHandle();
        }

        // 更新顶点数据
        size_t vertexOffset = m_CurrentBatch->vertexCount * sizeof(RectangleComponent::Vertex);
        if (vertexOffset + sizeof(RectangleComponent::Vertex) * 4 > m_VertexBuffer.size()) {
            TINA_LOG_ERROR("Vertex buffer overflow");
            return;
        }

        sprite.updateVertexBuffer(m_VertexBuffer.data() + vertexOffset, 
            sizeof(RectangleComponent::Vertex) * 4);

        // 更新索引数据
        size_t indexOffset = m_CurrentBatch->indexCount;
        if (indexOffset + 6 > m_IndexBuffer.size()) {
            TINA_LOG_ERROR("Index buffer overflow");
            return;
        }

        sprite.updateIndexBuffer(m_IndexBuffer.data() + indexOffset, 
            sizeof(uint16_t) * 6);

        // 更新计数
        m_CurrentBatch->quadCount++;
        m_CurrentBatch->vertexCount += 4;
        m_CurrentBatch->indexCount += 6;
    }
}

void RenderSystem::initBatch() {
    if (!m_CurrentBatch) {
        TINA_LOG_ERROR("Current batch is null in initBatch");
        throw std::runtime_error("Current batch is null in initBatch");
    }

    RenderBatch& batch = *m_CurrentBatch;
    TINA_LOG_DEBUG("Initializing new batch");

    try {
        // 创建顶点缓冲
        const uint32_t flags = BGFX_BUFFER_ALLOW_RESIZE;
        batch.vertexBuffer = bgfx::createDynamicVertexBuffer(
            MAX_VERTICES,
            RectangleComponent::s_VertexLayout,
            flags
        );

        if (!bgfx::isValid(batch.vertexBuffer)) {
            throw std::runtime_error("Failed to create vertex buffer");
        }

        // 创建索引缓冲
        const bgfx::Memory* mem = bgfx::copy(m_IndexBuffer.data(), 
            MAX_INDICES * sizeof(uint16_t));
        
        if (!mem) {
            throw std::runtime_error("Failed to copy index data");
        }

        batch.indexBuffer = bgfx::createIndexBuffer(mem);

        if (!bgfx::isValid(batch.indexBuffer)) {
            throw std::runtime_error("Failed to create index buffer");
        }

        batch.quadCount = 0;
        batch.vertexCount = 0;
        batch.indexCount = 0;
        batch.texture = BGFX_INVALID_HANDLE;

        TINA_LOG_DEBUG("Batch initialized successfully");
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to initialize batch: {}", e.what());
        
        if (bgfx::isValid(batch.vertexBuffer)) {
            bgfx::destroy(batch.vertexBuffer);
            batch.vertexBuffer = BGFX_INVALID_HANDLE;
        }
        
        if (bgfx::isValid(batch.indexBuffer)) {
            bgfx::destroy(batch.indexBuffer);
            batch.indexBuffer = BGFX_INVALID_HANDLE;
        }
        
        throw;
    }
}

void RenderSystem::flushBatch(RenderBatch& batch) {
    if (batch.quadCount == 0 || !isValidBatch(batch)) {
        return;
    }

    TINA_LOG_DEBUG("Flushing batch with {} quads", batch.quadCount);

    try {
        // 更新顶点缓冲
        const bgfx::Memory* mem = bgfx::copy(m_VertexBuffer.data(),
            batch.vertexCount * sizeof(RectangleComponent::Vertex));
            
        if (!mem) {
            throw std::runtime_error("Failed to copy vertex data");
        }
        
        bgfx::update(batch.vertexBuffer, 0, mem);

        // 设置渲染状态
        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_DEPTH_TEST_LESS
            | BGFX_STATE_WRITE_Z
            | BGFX_STATE_MSAA
            | BGFX_STATE_BLEND_ALPHA;

        bgfx::setState(state);

        // 设置纹理状态
        if (bgfx::isValid(batch.texture)) {
            TINA_LOG_DEBUG("Setting texture for batch");
            bgfx::setTexture(0, m_TextureSampler, batch.texture);
            float useTexture[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(m_UseTexture, useTexture);
        }
        else {
            TINA_LOG_DEBUG("No texture for batch");
            float noTexture[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(m_UseTexture, noTexture);
        }

        // 设置缓冲
        bgfx::setVertexBuffer(0, batch.vertexBuffer, 0, batch.vertexCount);
        bgfx::setIndexBuffer(batch.indexBuffer, 0, batch.indexCount);

        // 提交渲染
        bgfx::submit(0, m_Shader);

        // 重置状态
        batch.quadCount = 0;
        batch.vertexCount = 0;
        batch.indexCount = 0;
        batch.texture = BGFX_INVALID_HANDLE;
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to flush batch: {}", e.what());
        throw;
    }
}

bool RenderSystem::shouldStartNewBatch(const RenderBatch& batch, const SpriteComponent* sprite) const {
    // 如果批次已满,需要新批次
    if (batch.quadCount >= MAX_QUADS) {
        return true;
    }

    // 如果当前批次没有纹理,而sprite有纹理,需要新批次
    if (!bgfx::isValid(batch.texture) && 
        sprite->getTexture() && 
        sprite->getTexture()->isValid()) {
        return true;
    }

    // 如果当前批次有纹理,而sprite没有纹理,需要新批次
    if (bgfx::isValid(batch.texture) && 
        (!sprite->getTexture() || 
         !sprite->getTexture()->isValid())) {
        return true;
    }

    // 如果两者都有纹理,但纹理不同,需要新批次
    if (bgfx::isValid(batch.texture) && 
        sprite->getTexture() && 
        sprite->getTexture()->isValid() && 
        batch.texture.idx != sprite->getTexture()->getNativeHandle().idx) {
        return true;
    }

    return false;
}

} // namespace Tina */