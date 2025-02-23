// #include "tina/renderer/BatchRenderer2D.hpp"
// #include "tina/core/Engine.hpp"
// #include "tina/log/Logger.hpp"
// #include "tina/utils/Profiler.hpp"
// #include "tina/renderer/ShaderManager.hpp"
// #include <glm/gtc/type_ptr.hpp>
//
// namespace Tina
// {
//     void DrawQuadBatchCommand::execute()
//     {
//         TINA_PROFILE_FUNCTION();
//         if (auto* renderer = RenderCommandQueue::getInstance().getBatchRenderer())
//         {
//             renderer->drawQuad(m_Position, m_Size, m_Color);
//         }
//     }
//
//     void DrawTexturedQuadBatchCommand::execute()
//     {
//         TINA_PROFILE_FUNCTION();
//         if (auto* renderer = RenderCommandQueue::getInstance().getBatchRenderer())
//         {
//             renderer->drawTexturedQuad(m_Position, m_Size, m_Texture, m_TextureCoords, m_Tint);
//         }
//     }
//
//     bgfx::VertexLayout BatchRenderer2D::s_VertexLayout;
//     bgfx::VertexLayout BatchRenderer2D::s_InstanceLayout;
//
//     BatchRenderer2D::BatchRenderer2D()
//     {
//         TINA_PROFILE_FUNCTION();
//         // 为实例数组预分配空间
//         m_Instances.reserve(MAX_QUADS);
//         m_TextureSlots.reserve(MAX_TEXTURE_SLOTS);
//     }
//
//     BatchRenderer2D::~BatchRenderer2D()
//     {
//         TINA_PROFILE_FUNCTION();
//         shutdown();
//     }
//
//     void BatchRenderer2D::loadShader()
//     {
//         auto& shaderManager = Core::Engine::get().getShaderManager();
//         
//         // 创建着色器程序
//         m_Shader = shaderManager.createProgram("2d");
//         
//         if (!bgfx::isValid(m_Shader))
//         {
//             TINA_CORE_LOG_ERROR("Failed to create BatchRenderer2D shader program");
//             throw std::runtime_error("Failed to create BatchRenderer2D shader program");
//         }
//         
//         TINA_CORE_LOG_INFO("BatchRenderer2D shader loaded successfully");
//     }
//
//     void BatchRenderer2D::init()
//     {
//         TINA_PROFILE_FUNCTION();
//         std::lock_guard<std::mutex> lock(m_Mutex);
//
//         if (m_Initialized)
//         {
//             TINA_CORE_LOG_WARN("BatchRenderer2D already initialized");
//             return;
//         }
//
//         try
//         {
//             TINA_CORE_LOG_INFO("Initializing BatchRenderer2D");
//
//             auto& uniformManager = Core::Engine::get().getUniformManager();
//
//             // 创建Uniforms
//             uniformManager.createUniform(SAMPLER_UNIFORM_NAME, bgfx::UniformType::Sampler);
//             uniformManager.createUniform(USE_TEXTURE_UNIFORM_NAME, bgfx::UniformType::Vec4);
//
//             // 加载默认shader
//             loadShader();
//             
//             // 初始化顶点布局
//             s_VertexLayout
//                 .begin()
//                 .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
//                 .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
//                 .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
//                 .end();
//
//             // 初始化实例数据布局
//             s_InstanceLayout
//                 .begin()
//                 .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float) // Transform
//                 .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float) // Color
//                 .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float) // TextureData
//                 .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float) // TextureInfo
//                 .end();
//
//             createBuffers();
//
//             m_Initialized = true;
//             TINA_CORE_LOG_INFO("BatchRenderer2D initialized successfully");
//         }
//         catch (const std::exception& e)
//         {
//             TINA_CORE_LOG_ERROR("Failed to initialize BatchRenderer2D: {}", e.what());
//             shutdown();
//             throw;
//         }
//     }
//
//     void BatchRenderer2D::shutdown()
//     {
//         std::lock_guard<std::mutex> lock(m_Mutex);
//         if (!m_Initialized) return;
//
//         destroyBuffers();
//
//         auto& shaderManager = Core::Engine::get().getShaderManager();
//
//         // 销毁着色器程序
//         if (bgfx::isValid(m_Shader))
//         {
//             shaderManager.destroyProgram(m_Shader);
//             m_Shader = BGFX_INVALID_HANDLE;
//         }
//
//         m_Instances.clear();
//         m_TextureSlots.clear();
//         m_TextureSlotIndex = 1;
//
//         m_Initialized = false;
//         TINA_CORE_LOG_INFO("BatchRenderer2D shutdown");
//     }
//
//     void BatchRenderer2D::begin()
//     {
//         std::lock_guard<std::mutex> lock(m_Mutex);
//         if (!m_Initialized)
//         {
//             TINA_CORE_LOG_ERROR("BatchRenderer2D not initialized");
//             return;
//         }
//
//         // 重置状态
//         m_Instances.clear();
//         m_TextureSlots.clear();
//         m_TextureSlotIndex = 1;
//     }
//
//     void BatchRenderer2D::end()
//     {
//         flush();
//     }
//
//     void BatchRenderer2D::flush()
//     {
//         std::lock_guard<std::mutex> lock(m_Mutex);
//         if (m_Instances.empty()) return;
//
//         auto& uniformManager = Core::Engine::get().getUniformManager();
//
//         // 更新实例缓冲
//         bgfx::update(m_InstanceBuffer, 0, 
//             bgfx::copy(m_Instances.data(), m_Instances.size() * sizeof(InstanceData)));
//
//         // 设置纹理
//         for (uint32_t i = 0; i < m_TextureSlots.size(); i++)
//         {
//             bgfx::setTexture(i, uniformManager.getUniformHandle(SAMPLER_UNIFORM_NAME), 
//                             m_TextureSlots[i]->getHandle());
//         }
//
//         // 设置是否使用纹理
//         float useTexture = !m_TextureSlots.empty() ? 1.0f : 0.0f;
//         uniformManager.setUniform(USE_TEXTURE_UNIFORM_NAME, glm::vec4(useTexture, 0.0f, 0.0f, 0.0f));
//
//         // 设置渲染状态
//         uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
//                          BGFX_STATE_MSAA | 
//                          BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
//         bgfx::setState(state);
//
//         // 设置顶点和索引缓冲
//         bgfx::setVertexBuffer(0, m_VertexBuffer);
//         bgfx::setInstanceDataBuffer(m_InstanceBuffer, 0, m_Instances.size());
//         bgfx::setIndexBuffer(m_IndexBuffer);
//
//         // 提交绘制
//         bgfx::submit(m_ViewId, m_Shader);
//
//         // 清理状态
//         m_Instances.clear();
//         m_TextureSlots.clear();
//         m_TextureSlotIndex = 1;
//     }
//
//     void BatchRenderer2D::setViewTransform(const glm::mat4& view, const glm::mat4& proj)
//     {
//         std::lock_guard<std::mutex> lock(m_Mutex);
//         m_ViewMatrix = view;
//         m_ProjectionMatrix = proj;
//         
//         if (m_Initialized)
//         {
//             bgfx::setViewTransform(m_ViewId, glm::value_ptr(m_ViewMatrix), glm::value_ptr(m_ProjectionMatrix));
//         }
//     }
//
//     void BatchRenderer2D::setViewRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
//     {
//         if (m_Initialized)
//         {
//             bgfx::setViewRect(m_ViewId, x, y, width, height);
//         }
//     }
//
//     void BatchRenderer2D::setViewClear(uint16_t viewId, uint16_t flags, uint32_t rgba,
//                                      float depth, uint8_t stencil)
//     {
//         if (m_Initialized)
//         {
//             bgfx::setViewClear(viewId, flags, rgba, depth, stencil);
//         }
//     }
//
//     void BatchRenderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, const Color& color)
//     {
//         std::lock_guard<std::mutex> lock(m_Mutex);
//         if (!m_Initialized) return;
//
//         if (m_Instances.size() >= MAX_QUADS)
//         {
//             flush();
//         }
//
//         // 计算中心位置
//         glm::vec2 center = position + size * 0.5f;
//
//         InstanceData instance;
//         instance.Transform = glm::vec4(center.x, center.y, size.x, size.y);
//         instance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
//         instance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
//         instance.TextureInfo = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // 使用白色纹理
//
//         m_Instances.push_back(instance);
//     }
//
//     void BatchRenderer2D::drawQuads(const std::vector<InstanceData>& instances)
//     {
//         std::lock_guard<std::mutex> lock(m_Mutex);
//         if (!m_Initialized) return;
//
//         for (const auto& instance : instances)
//         {
//             if (m_Instances.size() >= MAX_QUADS)
//             {
//                 flush();
//             }
//             m_Instances.push_back(instance);
//         }
//     }
//
//     void BatchRenderer2D::drawTexturedQuad(const glm::vec2& position, const glm::vec2& size,
//         const std::shared_ptr<Texture2D>& texture, const glm::vec4& texCoords, const Color& tint)
//     {
//         std::lock_guard<std::mutex> lock(m_Mutex);
//         if (!m_Initialized || !texture) return;
//
//         if (m_Instances.size() >= MAX_QUADS)
//         {
//             flush();
//         }
//
//         float textureIndex = 0.0f;
//         
//         // 查找纹理是否已在槽中
//         for (uint32_t i = 0; i < m_TextureSlots.size(); i++)
//         {
//             if (m_TextureSlots[i] == texture)
//             {
//                 textureIndex = static_cast<float>(i + 1);
//                 break;
//             }
//         }
//
//         // 如果纹理不在槽中且还有空槽，添加到新槽
//         if (textureIndex == 0.0f)
//         {
//             if (m_TextureSlotIndex >= MAX_TEXTURE_SLOTS)
//             {
//                 flush();
//                 m_TextureSlotIndex = 1;
//             }
//             
//             textureIndex = static_cast<float>(m_TextureSlotIndex);
//             m_TextureSlots.push_back(texture);
//             m_TextureSlotIndex++;
//         }
//
//         // 计算中心位置
//         glm::vec2 center = position + size * 0.5f;
//
//         InstanceData instance;
//         instance.Transform = glm::vec4(center.x, center.y, size.x, size.y);
//         instance.Color = glm::vec4(tint.getR(), tint.getG(), tint.getB(), tint.getA());
//         instance.TextureData = texCoords;
//         instance.TextureInfo = glm::vec4(textureIndex, 0.0f, 0.0f, 0.0f);
//
//         m_Instances.push_back(instance);
//     }
//
//     void BatchRenderer2D::drawTexturedQuads(const std::vector<InstanceData>& instances,
//                                             const std::vector<std::shared_ptr<Texture2D>>& textures)
//     {
//         std::lock_guard<std::mutex> lock(m_Mutex);
//         if (!m_Initialized) return;
//
//         // 添加纹理
//         for (const auto& texture : textures)
//         {
//             if (!texture || !texture->isValid()) continue;
//             if (m_TextureSlotIndex >= MAX_TEXTURE_SLOTS)
//             {
//                 flush();
//                 m_TextureSlotIndex = 1;
//             }
//             m_TextureSlots.push_back(texture);
//             m_TextureSlotIndex++;
//         }
//
//         // 添加实例
//         for (const auto& instance : instances)
//         {
//             if (m_Instances.size() >= MAX_QUADS)
//             {
//                 flush();
//             }
//             m_Instances.push_back(instance);
//         }
//     }
//
//     void BatchRenderer2D::createBuffers()
//     {
//         // 创建顶点缓冲
//         float vertices[] = {
//             -0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
//              0.5f, -0.5f, 0.0f, 1.0f, 1.0f,
//              0.5f,  0.5f, 0.0f, 1.0f, 0.0f,
//             -0.5f,  0.5f, 0.0f, 0.0f, 0.0f
//         };
//         
//         m_VertexBuffer = bgfx::createVertexBuffer(
//             bgfx::copy(vertices, sizeof(vertices)),
//             s_VertexLayout
//         );
//
//         // 创建索引缓冲
//         uint16_t indices[] = {
//             0, 1, 2,
//             2, 3, 0
//         };
//         
//         m_IndexBuffer = bgfx::createIndexBuffer(
//             bgfx::copy(indices, sizeof(indices))
//         );
//
//         // 创建实例缓冲
//         m_InstanceBuffer = bgfx::createDynamicVertexBuffer(
//             MAX_QUADS,
//             s_InstanceLayout,
//             BGFX_BUFFER_ALLOW_RESIZE
//         );
//
//         if (!bgfx::isValid(m_VertexBuffer) || !bgfx::isValid(m_IndexBuffer) || !bgfx::isValid(m_InstanceBuffer))
//         {
//             TINA_CORE_LOG_ERROR("Failed to create BatchRenderer2D buffers");
//             throw std::runtime_error("Failed to create BatchRenderer2D buffers");
//         }
//     }
//
//     void BatchRenderer2D::destroyBuffers()
//     {
//         if (bgfx::isValid(m_VertexBuffer))
//         {
//             bgfx::destroy(m_VertexBuffer);
//             m_VertexBuffer = BGFX_INVALID_HANDLE;
//         }
//
//         if (bgfx::isValid(m_IndexBuffer))
//         {
//             bgfx::destroy(m_IndexBuffer);
//             m_IndexBuffer = BGFX_INVALID_HANDLE;
//         }
//
//         if (bgfx::isValid(m_InstanceBuffer))
//         {
//             bgfx::destroy(m_InstanceBuffer);
//             m_InstanceBuffer = BGFX_INVALID_HANDLE;
//         }
//     }
// } // namespace Tina
