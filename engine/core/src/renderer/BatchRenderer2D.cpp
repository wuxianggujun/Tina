#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/log/Logger.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace Tina {

bgfx::VertexLayout BatchRenderer2D::s_VertexLayout;
bgfx::VertexLayout BatchRenderer2D::s_InstanceLayout;

BatchRenderer2D::BatchRenderer2D() {
    // 为两个实例数组预分配空间
    m_ColorInstances.reserve(MAX_QUADS);
    m_TexturedInstances.reserve(MAX_QUADS);
}

BatchRenderer2D::~BatchRenderer2D() {
    shutdown();
}

void BatchRenderer2D::init(bgfx::ProgramHandle shader) {
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

        // 初始化实例数据布局
        s_InstanceLayout
            .begin()
            .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)     // Transform (i_data0)
            .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)     // Color (i_data1)
            .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)     // TextureData (i_data2)
            .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)     // TextureIndex + Padding (i_data3)
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
    
    // 预分配实例数组
    m_ColorInstances.reserve(MAX_QUADS);
    m_TexturedInstances.reserve(MAX_QUADS);
}

void BatchRenderer2D::shutdown() {
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
    m_ColorInstances.clear();
    m_TexturedInstances.clear();
    m_Textures.clear();
    m_ColorQuadCount = 0;
    m_TexturedQuadCount = 0;
}

void BatchRenderer2D::end() {
    // 先渲染纯色批次
    if (m_ColorQuadCount > 0) {
        flushColorBatch();
    }
    
    // 再渲染纹理批次
    if (m_TexturedQuadCount > 0) {
        flushTextureBatch();
    }
}

void BatchRenderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, const Color& color) {
    if (m_ColorQuadCount >= MAX_QUADS) {
        flushColorBatch();
    }

    InstanceData instance;
    instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
    instance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
    instance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    instance.TextureIndex = -1.0f;  // 标记为非纹理quad
    
    m_ColorInstances.push_back(instance);
    m_ColorQuadCount++;
}

void BatchRenderer2D::drawQuads(const std::vector<InstanceData>& instances) {
    for (const auto& instance : instances) {
        if (m_ColorQuadCount >= MAX_QUADS) {
            flushColorBatch();
        }
        
        // 创建新的实例，确保是非纹理quad
        InstanceData newInstance = instance;
        newInstance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        newInstance.TextureIndex = -1.0f;  // 标记为非纹理quad
        
        m_ColorInstances.push_back(newInstance);
        m_ColorQuadCount++;
    }
}

void BatchRenderer2D::drawTexturedQuad(const glm::vec2& position, const glm::vec2& size,
                                    const std::shared_ptr<Texture2D>& texture,
                                    const glm::vec4& textureCoords,
                                    const Color& tint) {
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
    instance.TextureIndex = textureIndex;

    TINA_LOG_DEBUG("Creating textured quad: pos({}, {}), size({}, {}), texCoords({}, {}, {}, {}), texIndex({})",
        position.x, position.y, size.x, size.y,
        textureCoords.x, textureCoords.y, textureCoords.z, textureCoords.w,
        textureIndex);
    
    m_TexturedInstances.push_back(instance);
    m_TexturedQuadCount++;
}

void BatchRenderer2D::drawTexturedQuads(const std::vector<InstanceData>& instances,
                                      const std::vector<std::shared_ptr<Texture2D>>& textures) {
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

        // 只处理有效的纹理索引
        if (instance.TextureIndex >= 0 && instance.TextureIndex < textures.size()) {
            m_TexturedInstances.push_back(instance);
            m_TexturedQuadCount++;
        }
    }
}

void BatchRenderer2D::flushColorBatch() {
    if (m_ColorQuadCount == 0) {
        return;
    }

    TINA_LOG_DEBUG("Flushing color batch with {} quads", m_ColorQuadCount);

    // 更新实例数据
    const bgfx::Memory* mem = bgfx::copy(m_ColorInstances.data(), 
        m_ColorQuadCount * sizeof(InstanceData));
    bgfx::update(m_ColorInstanceBuffer, 0, mem);

    // 设置uniform
    float noTexture[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(m_UseTexture, noTexture);

    // 设置渲染状态
    uint64_t state = 0
        | BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_A
        | BGFX_STATE_DEPTH_TEST_ALWAYS
        | BGFX_STATE_MSAA
        | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    bgfx::setState(state);

    // 设置缓冲
    bgfx::setVertexBuffer(0, m_VertexBuffer);
    bgfx::setInstanceDataBuffer(m_ColorInstanceBuffer, 0, m_ColorQuadCount);
    bgfx::setIndexBuffer(m_IndexBuffer);

    // 提交渲染
    bgfx::submit(0, m_Shader);

    // 重置状态
    m_ColorInstances.clear();
    m_ColorQuadCount = 0;
}

void BatchRenderer2D::flushTextureBatch() {
    if (m_TexturedQuadCount == 0 || m_Textures.empty()) {
        return;
    }

    TINA_LOG_DEBUG("Flushing texture batch with {} quads and {} textures", 
        m_TexturedQuadCount, m_Textures.size());

    // 更新实例数据
    const bgfx::Memory* mem = bgfx::copy(m_TexturedInstances.data(), 
        m_TexturedQuadCount * sizeof(InstanceData));
    bgfx::update(m_TexturedInstanceBuffer, 0, mem);

    // 设置纹理和uniform
    for (size_t i = 0; i < m_Textures.size(); ++i) {
        const auto& texture = m_Textures[i];
        if (texture && texture->isValid()) {
            // 设置纹理采样器状态
            uint32_t samplerFlags = 0
                | BGFX_SAMPLER_U_CLAMP
                | BGFX_SAMPLER_V_CLAMP
                | BGFX_SAMPLER_MIN_POINT
                | BGFX_SAMPLER_MAG_POINT;
            
            // 设置纹理和采样器标志
            bgfx::setTexture(0, m_TextureSampler, texture->getNativeHandle(), samplerFlags);
            
            // 启用纹理并传递纹理索引
            float useTexture[4] = { 1.0f, static_cast<float>(i), 0.0f, 0.0f };
            bgfx::setUniform(m_UseTexture, useTexture);
            
            TINA_LOG_DEBUG("Setting texture[{}] handle: {}, size: {}x{}, sampler flags: {:#x}, useTexture: [{}, {}, {}, {}]", 
                i,
                texture->getNativeHandle().idx,
                texture->getWidth(),
                texture->getHeight(),
                samplerFlags,
                useTexture[0], useTexture[1], useTexture[2], useTexture[3]);
                
            break; // 目前只使用第一个纹理
        }
    }

    // 设置渲染状态
    uint64_t state = 0
        | BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_A
        | BGFX_STATE_DEPTH_TEST_ALWAYS
        | BGFX_STATE_MSAA
        | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    bgfx::setState(state);

    // 设置缓冲
    bgfx::setVertexBuffer(0, m_VertexBuffer);
    bgfx::setInstanceDataBuffer(m_TexturedInstanceBuffer, 0, m_TexturedQuadCount);
    bgfx::setIndexBuffer(m_IndexBuffer);

    // 提交渲染
    bgfx::submit(0, m_Shader);

    // 重置状态
    m_TexturedInstances.clear();
    m_Textures.clear();
    m_TexturedQuadCount = 0;
}

} // namespace Tina 