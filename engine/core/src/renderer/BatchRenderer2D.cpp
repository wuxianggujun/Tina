#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/log/Logger.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace Tina {

bgfx::VertexLayout BatchRenderer2D::s_VertexLayout;
bgfx::VertexLayout BatchRenderer2D::s_InstanceLayout;

BatchRenderer2D::BatchRenderer2D() {
    m_Instances.reserve(MAX_QUADS);
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
    
    // 左上
    vertices[0].Position = {0.0f, 0.0f, 0.0f};
    vertices[0].TexCoord = {0.0f, 0.0f};
    vertices[0].Color = 0xFFFFFFFF;

    // 右上
    vertices[1].Position = {1.0f, 0.0f, 0.0f};
    vertices[1].TexCoord = {1.0f, 0.0f};
    vertices[1].Color = 0xFFFFFFFF;

    // 右下
    vertices[2].Position = {1.0f, 1.0f, 0.0f};
    vertices[2].TexCoord = {1.0f, 1.0f};
    vertices[2].Color = 0xFFFFFFFF;

    // 左下
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
    m_InstanceBuffer = bgfx::createDynamicVertexBuffer(
        MAX_QUADS,
        s_InstanceLayout,
        flags
    );
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
    
    if (bgfx::isValid(m_InstanceBuffer)) {
        bgfx::destroy(m_InstanceBuffer);
    }

    m_Instances.clear();
    m_QuadCount = 0;
    m_Initialized = false;
}

void BatchRenderer2D::begin() {
    m_Instances.clear();
    m_QuadCount = 0;
}

void BatchRenderer2D::end() {
    flushBatch();
}

void BatchRenderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, const Color& color) {
    if (m_QuadCount >= MAX_QUADS) {
        flushBatch();
    }

    InstanceData instance;
    instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
    instance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
    
    m_Instances.push_back(instance);
    m_QuadCount++;
}

void BatchRenderer2D::drawQuads(const std::vector<InstanceData>& instances) {
    for (const auto& instance : instances) {
        if (m_QuadCount >= MAX_QUADS) {
            flushBatch();
        }
        m_Instances.push_back(instance);
        m_QuadCount++;
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

    if (m_QuadCount >= MAX_QUADS) {
        flushBatch();
    }

    // 查找或添加纹理
    auto it = std::find(m_Textures.begin(), m_Textures.end(), texture);
    float textureIndex = 0.0f;
    
    if (it == m_Textures.end()) {
        textureIndex = static_cast<float>(m_Textures.size());
        m_Textures.push_back(texture);
    } else {
        textureIndex = static_cast<float>(std::distance(m_Textures.begin(), it));
    }

    InstanceData instance;
    instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
    instance.Color = glm::vec4(tint.getR(), tint.getG(), tint.getB(), tint.getA());
    instance.TextureData = textureCoords;
    instance.TextureIndex = textureIndex;
    
    m_Instances.push_back(instance);
    m_QuadCount++;
}

void BatchRenderer2D::drawTexturedQuads(const std::vector<InstanceData>& instances,
                                      const std::vector<std::shared_ptr<Texture2D>>& textures) {
    // 添加新的纹理
    for (const auto& texture : textures) {
        if (std::find(m_Textures.begin(), m_Textures.end(), texture) == m_Textures.end()) {
            m_Textures.push_back(texture);
        }
    }

    // 添加实例数据
    for (const auto& instance : instances) {
        if (m_QuadCount >= MAX_QUADS) {
            flushBatch();
        }
        m_Instances.push_back(instance);
        m_QuadCount++;
    }
}

void BatchRenderer2D::flushBatch() {
    if (m_QuadCount == 0) {
        return;
    }

    // 更新实例数据
    const bgfx::Memory* mem = bgfx::copy(m_Instances.data(), 
        m_QuadCount * sizeof(InstanceData));
    bgfx::update(m_InstanceBuffer, 0, mem);

    // 设置渲染状态
    uint64_t state = 0
        | BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_A
        | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_WRITE_Z
        | BGFX_STATE_MSAA
        | BGFX_STATE_BLEND_ALPHA;

    bgfx::setState(state);

    // 设置纹理
    if (!m_Textures.empty()) {
        bgfx::setTexture(0, m_TextureSampler, m_Textures[0]->getNativeHandle());
        float useTexture[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(m_UseTexture, useTexture);
    } else {
        float noTexture[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        bgfx::setUniform(m_UseTexture, noTexture);
    }

    // 设置缓冲
    bgfx::setVertexBuffer(0, m_VertexBuffer);
    bgfx::setInstanceDataBuffer(m_InstanceBuffer, 0, m_QuadCount);
    bgfx::setIndexBuffer(m_IndexBuffer);

    // 提交渲染
    bgfx::submit(0, m_Shader);

    // 重置状态
    m_Instances.clear();
    m_Textures.clear();
    m_QuadCount = 0;
}

} // namespace Tina 