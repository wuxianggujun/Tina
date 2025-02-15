#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/core/Engine.hpp"
#include "tina/log/Logger.hpp"
#include "tina/utils/Profiler.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace Tina
{
    void DrawQuadBatchCommand::execute()
    {
        TINA_PROFILE_FUNCTION();
        if (auto* renderer = RenderCommandQueue::getInstance().getBatchRenderer())
        {
            renderer->drawQuad(m_Position, m_Size, m_Color);
        }
    }

    void DrawTexturedQuadBatchCommand::execute()
    {
        TINA_PROFILE_FUNCTION();
        if (auto* renderer = RenderCommandQueue::getInstance().getBatchRenderer())
        {
            renderer->drawTexturedQuad(m_Position, m_Size, m_Texture, m_TextureCoords, m_Tint);
        }
    }

    bgfx::VertexLayout BatchRenderer2D::s_VertexLayout;
    bgfx::VertexLayout BatchRenderer2D::s_InstanceLayout;

    BatchRenderer2D::BatchRenderer2D()
    {
        TINA_PROFILE_FUNCTION();
        // 为两个实例数组预分配空间
        m_Instances.reserve(MAX_QUADS);
    }

    BatchRenderer2D::~BatchRenderer2D()
    {
        TINA_PROFILE_FUNCTION();
        shutdown();
    }

    void BatchRenderer2D::init(bgfx::ProgramHandle shader)
    {
        TINA_PROFILE_FUNCTION();

        if (m_Initialized)
        {
            TINA_LOG_WARN("BatchRenderer2D already initialized");
            return;
        }

        try
        {
            TINA_LOG_INFO("Initializing BatchRenderer2D");

            m_Shader = shader;

            auto& uniformManager = Core::Engine::get().getUniformManager();

            // 创建Uniforms
            uniformManager.createUniform(SAMPLER_UNIFORM_NAME, bgfx::UniformType::Sampler);
            uniformManager.createUniform(USE_TEXTURE_UNIFORM_NAME, bgfx::UniformType::Vec4);

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
                .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float) // Transform (i_data0)
                .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float) // Color (i_data1)
                .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float) // TextureData (i_data2)
                .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float) // TextureInfo (i_data3)
                .end();

            createBuffers();

            m_Initialized = true;
            TINA_LOG_INFO("BatchRenderer2D initialized successfully");
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Failed to initialize BatchRenderer2D: {}", e.what());
            shutdown();
            throw;
        }
    }

    void BatchRenderer2D::createBuffers()
    {
        TINA_PROFILE_FUNCTION();

        // 创建顶点数据
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

        // 创建实例缓冲 - 使用较小的初始容量
        uint32_t initialCapacity = 100; // 减小初始容量
        uint32_t flags = BGFX_BUFFER_ALLOW_RESIZE;

        m_InstanceBuffer = bgfx::createDynamicVertexBuffer(initialCapacity, s_InstanceLayout, flags);

        m_Instances.reserve(initialCapacity);
        m_Textures.reserve(MAX_TEXTURE_SLOTS);

        TINA_LOG_DEBUG("Buffers created - Initial capacity: {}, Instance size: {} bytes",
                       initialCapacity,
                       sizeof(InstanceData));
    }

    void BatchRenderer2D::flushBatch(uint16_t viewId)
    {
        if (m_QuadCount == 0) return;

        auto& uniformManager = Core::Engine::get().getUniformManager();

        // 更新实例缓冲
        const bgfx::Memory* mem = bgfx::copy(m_Instances.data(), m_QuadCount * sizeof(InstanceData));
        bgfx::update(m_InstanceBuffer, 0, mem);

        // 绑定纹理
        for (size_t i = 0; i < m_Textures.size(); i++)
        {
            bgfx::setTexture(static_cast<uint8_t>(i),  uniformManager.getUniformHandle(SAMPLER_UNIFORM_NAME), m_Textures[i]->getHandle());
        }

        // 设置是否使用纹理的uniform
        float useTexture = !m_Textures.empty() ? 1.0f : 0.0f;
        uniformManager.setUniform(USE_TEXTURE_UNIFORM_NAME, glm::vec4(useTexture, 0.0f, 0.0f, 0.0f));

        // 设置渲染状态
        uint8_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS
            | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

        // 提交绘制命令
        bgfx::setVertexBuffer(0, m_VertexBuffer);
        bgfx::setInstanceDataBuffer(m_InstanceBuffer, 0, m_QuadCount);
        bgfx::setIndexBuffer(m_IndexBuffer);
        bgfx::setState(state);
        bgfx::submit(viewId, m_Shader);

        // 重置
        m_QuadCount = 0;
        m_Instances.clear();
        m_Textures.clear();
    }

    void BatchRenderer2D::setViewTransform(const glm::mat4& view, const glm::mat4& proj)
    {
        bgfx::setViewTransform(m_ViewId,glm::value_ptr(view), glm::value_ptr(proj));
    }

    void BatchRenderer2D::setViewRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
    {
        bgfx::setViewRect(m_ViewId, x, y, width, height);
    }

    void BatchRenderer2D::setViewClear(uint16_t flags, uint32_t rgba, float depth, uint8_t stencil)
    {
        bgfx::setViewClear(m_ViewId, flags, rgba, depth, stencil);
    }

    void BatchRenderer2D::shutdown()
    {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!m_Initialized)
        {
            return;
        }

        TINA_LOG_INFO("Shutting down BatchRenderer2D");

        if (bgfx::isValid(m_VertexBuffer))
        {
            bgfx::destroy(m_VertexBuffer);
        }

        if (bgfx::isValid(m_IndexBuffer))
        {
            bgfx::destroy(m_IndexBuffer);
        }

        if (bgfx::isValid(m_InstanceBuffer))
        {
            bgfx::destroy(m_InstanceBuffer);
        }

        m_Instances.clear();
        m_Textures.clear();
        m_QuadCount = 0;
        m_Initialized = false;
    }

    void BatchRenderer2D::begin()
    {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);

        // 输出内存使用情况
        size_t instanceMemory = m_Instances.capacity() * sizeof(InstanceData);
        size_t textureMemory = 0;
        for (const auto& texture : m_Textures)
        {
            if (texture)
            {
                textureMemory += texture->getWidth() * texture->getHeight() * 4;
            }
        }
        TINA_LOG_DEBUG(
            "Begin frame - Memory Usage:\n\tInstances: {}/{} ({}KB)\n\tActive Textures: {} ({}KB)\n\tTotal: {}KB",
            m_QuadCount, m_Instances.capacity(), instanceMemory/1024,
            m_Textures.size(), textureMemory/1024,
            (instanceMemory + textureMemory)/1024);

        m_Instances.clear();
        m_Textures.clear();
        m_QuadCount = 0;
    }

    void BatchRenderer2D::end()
    {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);

        // 输出本帧的渲染统计
        TINA_LOG_DEBUG("End frame - Rendering stats:\n\tTotal quads: {}\n\tTexture bindings: {}",
                       m_QuadCount, m_Textures.size());

        if (m_QuadCount > 0)
        {
            TINA_PROFILE_SCOPE("Flush Batch");
            flushBatch(m_ViewId);
        }

        // 主动释放不再需要的GPU资源
        if (m_Instances.capacity() > MAX_QUADS)
        {
            bgfx::destroy(m_InstanceBuffer);
            m_InstanceBuffer = bgfx::createDynamicVertexBuffer(
                MAX_QUADS,
                s_InstanceLayout,
                BGFX_BUFFER_ALLOW_RESIZE
            );
        }
    }

    void BatchRenderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, const Color& color)
    {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (m_QuadCount >= MAX_QUADS)
        {
            flushBatch(m_ViewId);
        }

        // 创建实例数据
        InstanceData newInstance;
        newInstance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
        newInstance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
        newInstance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        newInstance.TextureInfo = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f); // 非纹理quad

        m_Instances.push_back(newInstance);
        m_QuadCount++;
    }

    void BatchRenderer2D::drawQuads(const std::vector<InstanceData>& instances)
    {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);

        for (const auto& instance : instances)
        {
            if (m_QuadCount >= MAX_QUADS)
            {
                flushBatch(m_ViewId);
            }

            // 创建新的实例，确保是非纹理quad
            InstanceData newInstance = instance;
            newInstance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            newInstance.TextureInfo = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f); // 标记为非纹理quad

            m_Instances.push_back(newInstance);
            m_QuadCount++;
        }
    }

    void BatchRenderer2D::drawTexturedQuad(const glm::vec2& position, const glm::vec2& size,
                                           const std::shared_ptr<Texture2D>& texture,
                                           const glm::vec4& textureCoords,
                                           const Color& tint)
    {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);

        if (!texture || !texture->isValid())
        {
            TINA_LOG_WARN("Attempting to draw with invalid texture");
            return;
        }

        if (m_QuadCount >= MAX_QUADS)
        {
            flushBatch(m_ViewId);
        }

        // 查找或添加纹理
        auto it = std::find(m_Textures.begin(), m_Textures.end(), texture);
        float textureIndex = 0.0f;

        if (it == m_Textures.end())
        {
            if (m_Textures.size() >= MAX_TEXTURE_SLOTS)
            {
                flushBatch(m_ViewId);
                m_Textures.clear();
            }
            textureIndex = static_cast<float>(m_Textures.size());
            m_Textures.push_back(texture);
        }
        else
        {
            textureIndex = static_cast<float>(std::distance(m_Textures.begin(), it));
        }

        // 创建实例数据
        InstanceData instance;
        instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
        instance.Color = glm::vec4(tint.getR(), tint.getG(), tint.getB(), tint.getA());
        instance.TextureData = textureCoords;
        instance.TextureInfo = glm::vec4(textureIndex, 1.0f, 0.0f, 0.0f);

        m_Instances.push_back(instance);
        m_QuadCount++;
    }

    void BatchRenderer2D::drawTexturedQuads(const std::vector<InstanceData>& instances,
                                            const std::vector<std::shared_ptr<Texture2D>>& textures)
    {
        TINA_PROFILE_FUNCTION();
        std::lock_guard<std::mutex> lock(m_Mutex);

        // 添加新的纹理
        for (const auto& texture : textures)
        {
            if (!texture || !texture->isValid())
            {
                TINA_LOG_WARN("Invalid texture in drawTexturedQuads");
                continue;
            }
            if (std::find(m_Textures.begin(), m_Textures.end(), texture) == m_Textures.end())
            {
                if (m_Textures.size() >= MAX_TEXTURE_SLOTS)
                {
                    flushBatch(m_ViewId);
                    m_Textures.clear();
                }
                m_Textures.push_back(texture);
            }
        }

        // 添加实例数据
        for (const auto& instance : instances)
        {
            if (m_QuadCount >= MAX_QUADS)
            {
                flushBatch(m_ViewId);
            }
            m_Instances.push_back(instance);
            m_QuadCount++;
        }
    }
} // namespace Tina
