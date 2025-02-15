#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/log/Logger.hpp"
#include "tina/utils/Profiler.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace Tina {

void DrawQuadBatchCommand::execute() {
    TINA_PROFILE_FUNCTION();
    if (auto* renderer = RenderCommandQueue::getInstance().getBatchRenderer()) {
        renderer->drawQuad(m_Position, m_Size, m_Color);
    }
}

void DrawTexturedQuadBatchCommand::execute() {
    TINA_PROFILE_FUNCTION();
    if (auto* renderer = RenderCommandQueue::getInstance().getBatchRenderer()) {
        renderer->drawTexturedQuad(m_Position, m_Size, m_Texture, m_TextureCoords, m_Tint);
    }
}

bgfx::VertexLayout BatchRenderer2D::s_VertexLayout;
bgfx::VertexLayout BatchRenderer2D::s_InstanceLayout;

BatchRenderer2D::BatchRenderer2D() {
    TINA_PROFILE_FUNCTION();
    // 为两个实例数组预分配空间
    m_ColorInstances.reserve(MAX_QUADS);
    m_TexturedInstances.reserve(MAX_QUADS);
}

BatchRenderer2D::~BatchRenderer2D() {
    TINA_PROFILE_FUNCTION();
    shutdown();
}

void BatchRenderer2D::init(bgfx::ProgramHandle shader) {
    TINA_PROFILE_FUNCTION();
    
    if (m_Initialized) {
        TINA_LOG_WARN("BatchRenderer2D already initialized");
        return;
    }

    try {
        TINA_LOG_INFO("Initializing BatchRenderer2D");
        
        m_Shader = shader;

        // 创建Uniforms
        m_TextureSampler = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        m_UseTexture = bgfx::createUniform("u_useTexture", bgfx::UniformType::Vec4);

        // 初始化顶点布局
        s_VertexLayout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();

        // 初始化实例数据布局 - 保持与shader一致
        s_InstanceLayout
            .begin()
            .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)     // Transform (i_data0)
            .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)     // Color (i_data1)
            .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)     // TextureData (i_data2)
            .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)     // TextureInfo (i_data3)
            .end();

        createBuffers();

        m_Initialized = true;
        TINA_LOG_INFO("BatchRenderer2D initialized successfully");
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to initialize BatchRenderer2D: {}", e.what());
        shutdown();
        throw;
    }
}

void BatchRenderer2D::createBuffers() {
    TINA_PROFILE_FUNCTION();
    
    // 创建单个四边形的顶点数据
    std::vector<QuadVertex> vertices(4);
    
    // 左上 (0.0, 0.0)
    vertices[0].Position = {0.0f, 0.0f, 0.0f};
    vertices[0].TexCoord = {0.0f, 0.0f};
    vertices[0].Color = 0xFFFFFFFF;

    // 右上 (1.0, 0.0)
    vertices[1].Position = {1.0f, 0.0f, 0.0f};
    vertices[1].TexCoord = {1.0f, 0.0f};
    vertices[1].Color = 0xFFFFFFFF;

    // 右下 (1.0, 1.0)
    vertices[2].Position = {1.0f, 1.0f, 0.0f};
    vertices[2].TexCoord = {1.0f, 1.0f};
    vertices[2].Color = 0xFFFFFFFF;

    // 左下 (0.0, 1.0)
    vertices[3].Position = {0.0f, 1.0f, 0.0f};
    vertices[3].TexCoord = {0.0f, 1.0f};
    vertices[3].Color = 0xFFFFFFFF;

    // 创建顶点缓冲
    const bgfx::Memory* vbMem = bgfx::copy(vertices.data(), vertices.size() * sizeof(QuadVertex));
    m_VertexBuffer = bgfx::createVertexBuffer(vbMem, s_VertexLayout);

    // 创建索引数据
    std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};
    const bgfx::Memory* ibMem = bgfx::copy(indices.data(), indices.size() * sizeof(uint16_t));
    m_IndexBuffer = bgfx::createIndexBuffer(ibMem);

    // 创建实例缓冲
    uint32_t flags = BGFX_BUFFER_ALLOW_RESIZE;
    m_ColorInstanceBuffer = bgfx::createDynamicVertexBuffer(
        MAX_QUADS,
        s_InstanceLayout,
        flags
    );
    
    m_TexturedInstanceBuffer = bgfx::createDynamicVertexBuffer(
        MAX_QUADS,
        s_InstanceLayout,
        flags
    );
    
    // 预分配实例数组(使用较小的初始容量)
    m_ColorInstances.reserve(100);
    m_TexturedInstances.reserve(100);
}

void BatchRenderer2D::shutdown() {
    TINA_PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    if (!m_Initialized) {
        return;
    }

    TINA_LOG_INFO("Shutting down BatchRenderer2D");

    if (bgfx::isValid(m_VertexBuffer)) {
        bgfx::destroy(m_VertexBuffer);
    }
    
    if (bgfx::isValid(m_IndexBuffer)) {
        bgfx::destroy(m_IndexBuffer);
    }
    
    if (bgfx::isValid(m_ColorInstanceBuffer)) {
        bgfx::destroy(m_ColorInstanceBuffer);
    }
    
    if (bgfx::isValid(m_TexturedInstanceBuffer)) {
        bgfx::destroy(m_TexturedInstanceBuffer);
    }

    m_ColorInstances.clear();
    m_TexturedInstances.clear();
    m_Textures.clear();
    m_ColorQuadCount = 0;
    m_TexturedQuadCount = 0;
    m_Initialized = false;
}

void BatchRenderer2D::begin() {
    TINA_PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    m_ColorInstances.clear();
    m_TexturedInstances.clear();
    m_Textures.clear();
    m_ColorQuadCount = 0;
    m_TexturedQuadCount = 0;
}

void BatchRenderer2D::flushColorBatchInternal() {
    if (m_ColorQuadCount == 0) return;

    // 更新实例缓冲
    const bgfx::Memory* mem = bgfx::copy(m_ColorInstances.data(), 
                                        m_ColorQuadCount * sizeof(InstanceData));
    bgfx::update(m_ColorInstanceBuffer, 0, mem);

    // 设置着色器参数
    bgfx::setUniform(m_UseTexture, glm::value_ptr(glm::vec4(0.0f)));

    // 设置渲染状态
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                     BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    
    // 提交绘制命令
    bgfx::setVertexBuffer(0, m_VertexBuffer);
    bgfx::setInstanceDataBuffer(m_ColorInstanceBuffer, 0, m_ColorQuadCount);
    bgfx::setIndexBuffer(m_IndexBuffer);
    bgfx::setState(state);
    bgfx::submit(0, m_Shader);

    // 重置计数
    m_ColorQuadCount = 0;
    m_ColorInstances.clear();
}

void BatchRenderer2D::flushTextureBatchInternal() {
    if (m_TexturedQuadCount == 0) return;

    // 更新实例缓冲
    const bgfx::Memory* mem = bgfx::copy(m_TexturedInstances.data(), 
                                        m_TexturedQuadCount * sizeof(InstanceData));
    bgfx::update(m_TexturedInstanceBuffer, 0, mem);

    // 设置着色器参数
    bgfx::setUniform(m_UseTexture, glm::value_ptr(glm::vec4(1.0f)));

    // 绑定纹理
    for (size_t i = 0; i < m_Textures.size(); ++i) {
        bgfx::setTexture(static_cast<uint8_t>(i), m_TextureSampler, m_Textures[i]->getHandle());
    }

    // 设置渲染状态
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                     BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    // 提交绘制命令
    bgfx::setVertexBuffer(0, m_VertexBuffer);
    bgfx::setInstanceDataBuffer(m_TexturedInstanceBuffer, 0, m_TexturedQuadCount);
    bgfx::setIndexBuffer(m_IndexBuffer);
    bgfx::setState(state);
    bgfx::submit(0, m_Shader);

    // 重置计数
    m_TexturedQuadCount = 0;
    m_TexturedInstances.clear();
    m_Textures.clear();
}

void BatchRenderer2D::flushColorBatch() {
    TINA_PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(m_Mutex);
    flushColorBatchInternal();
}

void BatchRenderer2D::flushTextureBatch() {
    TINA_PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(m_Mutex);
    flushTextureBatchInternal();
}

void BatchRenderer2D::end() {
    TINA_PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    // 先渲染纯色批次
    if (m_ColorQuadCount > 0) {
        TINA_PROFILE_SCOPE("Flush Color Batch");
        flushColorBatchInternal();
    }
    
    // 再渲染纹理批次
    if (m_TexturedQuadCount > 0) {
        TINA_PROFILE_SCOPE("Flush Texture Batch");
        flushTextureBatchInternal();
    }
}

void BatchRenderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, const Color& color) {
    TINA_PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (m_ColorQuadCount >= MAX_QUADS) {
        flushColorBatch();
    }

    // 创建实例数据
    InstanceData newInstance;
    newInstance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
    newInstance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
    newInstance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    newInstance.TextureInfo = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);  // 非纹理quad
    
    m_ColorInstances.push_back(newInstance);
    m_ColorQuadCount++;
}

void BatchRenderer2D::drawQuads(const std::vector<InstanceData>& instances) {
    TINA_PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    for (const auto& instance : instances) {
        if (m_ColorQuadCount >= MAX_QUADS) {
            flushColorBatch();
        }
        
        // 创建新的实例，确保是非纹理quad
        InstanceData newInstance = instance;
        newInstance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        newInstance.TextureInfo = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);  // 标记为非纹理quad
        
        m_ColorInstances.push_back(newInstance);
        m_ColorQuadCount++;
    }
}

void BatchRenderer2D::drawTexturedQuad(const glm::vec2& position, const glm::vec2& size,
                                    const std::shared_ptr<Texture2D>& texture,
                                    const glm::vec4& textureCoords,
                                    const Color& tint) {
    TINA_PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(m_Mutex);

    if (!texture || !texture->isValid()) {
        TINA_LOG_WARN("Attempting to draw with invalid texture");
        return;
    }

    if (m_TexturedQuadCount >= MAX_QUADS) {
        flushTextureBatch();
    }

    // 查找或添加纹理
    auto it = std::find(m_Textures.begin(), m_Textures.end(), texture);
    float textureIndex = 0.0f;
    
    if (it == m_Textures.end()) {
        if (m_Textures.size() >= MAX_TEXTURE_SLOTS) {
            flushTextureBatch();
            m_Textures.clear();
        }
        textureIndex = static_cast<float>(m_Textures.size());
        m_Textures.push_back(texture);
    } else {
        textureIndex = static_cast<float>(std::distance(m_Textures.begin(), it));
    }

    // 创建实例数据
    InstanceData instance;
    instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
    instance.Color = glm::vec4(tint.getR(), tint.getG(), tint.getB(), tint.getA());
    instance.TextureData = textureCoords;
    instance.TextureInfo = glm::vec4(textureIndex, 0.0f, 0.0f, 0.0f);

    TINA_LOG_DEBUG("Creating textured quad: pos({}, {}), size({}, {}), texCoords({}, {}, {}, {}), texIndex({})",
        position.x, position.y, size.x, size.y,
        textureCoords.x, textureCoords.y, textureCoords.z, textureCoords.w,
        textureIndex);
    
    m_TexturedInstances.push_back(instance);
    m_TexturedQuadCount++;
}

void BatchRenderer2D::drawTexturedQuads(const std::vector<InstanceData>& instances,
                                      const std::vector<std::shared_ptr<Texture2D>>& textures) {
    TINA_PROFILE_FUNCTION();
    std::lock_guard<std::mutex> lock(m_Mutex);
    
    // 添加新的纹理
    for (const auto& texture : textures) {
        if (!texture || !texture->isValid()) {
            TINA_LOG_WARN("Invalid texture in drawTexturedQuads");
            continue;
        }
        if (std::find(m_Textures.begin(), m_Textures.end(), texture) == m_Textures.end()) {
            if (m_Textures.size() >= MAX_TEXTURE_SLOTS) {
                flushTextureBatch();
                m_Textures.clear();
            }
            m_Textures.push_back(texture);
        }
    }

    // 添加实例数据
    for (const auto& instance : instances) {
        if (m_TexturedQuadCount >= MAX_QUADS) {
            flushTextureBatch();
        }
        m_TexturedInstances.push_back(instance);
        m_TexturedQuadCount++;
    }
}

} // namespace Tina 