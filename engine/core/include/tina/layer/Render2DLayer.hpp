#pragma once

#include "tina/layer/Layer.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/renderer/ShaderManager.hpp"
#include "tina/log/Logger.hpp"
#include "tina/core/Engine.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include "bx/math.h"
#include "tina/core/OrthographicCamera.hpp"
#include "tina/event/Event.hpp"
#include "tina/renderer/Texture2D.hpp"
#include "tina/components/SpriteComponent.hpp"
#include "tina/components/Transform2DComponent.hpp"
#include "tina/scene/Scene.hpp"
#include <unordered_map>
#include "tina/utils/BgfxUtils.hpp"
#include <vector>
#include <algorithm>

#include "bgfx/platform.h"

namespace Tina
{
    class Render2DLayer : public Layer
    {
    public:
        Render2DLayer() : Layer("Render2DLayer")
        {
            TINA_LOG_INFO("Render2DLayer created");
        }

        ~Render2DLayer()
        {
            m_rectangles.clear();
            m_textures.clear();
            m_texturedRectangles.clear();
        }

        // 加载纹理
        std::shared_ptr<Texture2D> loadTexture(const std::string& name, const std::string& path)
        {
            auto it = m_textures.find(name);
            if (it != m_textures.end())
            {
                return it->second;
            }

            // 使用BgfxUtils加载纹理
            auto result = Utils::BgfxUtils::loadTexture(path);
            if (!bgfx::isValid(result.handle))
            {
                TINA_LOG_ERROR("Failed to load texture: {}", path);
                return nullptr;
            }

            auto texture = Texture2D::create(name);
            texture->setNativeHandle(
                result.handle,
                result.width,
                result.height,
                result.depth,
                result.hasMips,
                result.layers,
                result.format
            );
            
            if (texture->isValid())
            {
                m_textures[name] = texture;
                TINA_LOG_INFO("Texture '{}' loaded successfully", name);
                return texture;
            }
            
            TINA_LOG_ERROR("Failed to create texture '{}'", name);
            return nullptr;
        }

        // 创建矩形
        void createRectangle(const glm::vec2& position, const glm::vec2& size, const Color& color)
        {
            BatchRenderer2D::InstanceData instance;
            instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
            instance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
            m_rectangles.push_back(instance);
        }

        // 创建带纹理的矩形
        void createTexturedRectangle(const glm::vec2& position, const glm::vec2& size, 
                                   const std::string& textureName,
                                   const glm::vec4& textureCoords = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                                   const Color& tint = Color::White)
        {
            auto it = m_textures.find(textureName);
            if (it == m_textures.end())
            {
                TINA_LOG_WARN("Texture '{}' not found", textureName);
                return;
            }

            BatchRenderer2D::InstanceData instance;
            instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
            instance.Color = glm::vec4(tint.getR(), tint.getG(), tint.getB(), tint.getA());
            instance.TextureData = textureCoords;
            instance.TextureIndex = static_cast<float>(m_texturedRectangles.size());

            TINA_LOG_DEBUG("Creating textured rectangle: pos({}, {}), size({}, {}), texCoords({}, {}, {}, {}), index({})",
                position.x, position.y, size.x, size.y,
                textureCoords.x, textureCoords.y, textureCoords.z, textureCoords.w,
                instance.TextureIndex);

            m_texturedRectangles.push_back({instance, it->second});
        }

        void createRandomRectangles(int count)
        {
            m_rectangles.clear();
            m_rectangles.reserve(count);

            for (int i = 0; i < count; i++)
            {
                float x = static_cast<float>(rand() % 800);
                float y = static_cast<float>(rand() % 600);

                float width = 50.0f + static_cast<float>(rand() % 150);
                float height = 50.0f + static_cast<float>(rand() % 150);

                Color randomColor(
                    static_cast<float>(rand()) / RAND_MAX,
                    static_cast<float>(rand()) / RAND_MAX,
                    static_cast<float>(rand()) / RAND_MAX,
                    1.0f
                );

                createRectangle({x, y}, {width, height}, randomColor);
            }
        }

    private:
        void initShaders()
        {
            auto& shaderManager = ShaderManager::getInstance();
            m_shaderProgram = shaderManager.createProgram("2d");
            if (!bgfx::isValid(m_shaderProgram))
            {
                throw std::runtime_error("Failed to create 2D shader program");
            }
            TINA_LOG_INFO("Successfully loaded 2D shaders");
        }

        void initRenderer()
        {
            m_batchRenderer = std::make_unique<BatchRenderer2D>();
            m_batchRenderer->init(m_shaderProgram);
            
            // 清除之前的数据
            m_rectangles.clear();
            m_texturedRectangles.clear();
            m_textures.clear();
        }

        void initCamera()
        {
            uint32_t width, height;
            Core::Engine::get().getWindowSize(width, height);
            m_camera = std::make_unique<OrthographicCamera>(
                0.0f, static_cast<float>(width),
                static_cast<float>(height), 0.0f
            );
        }

    public:
        void onAttach() override
        {
            if (m_initialized)
            {
                TINA_LOG_WARN("Render2DLayer already initialized");
                return;
            }

            TINA_LOG_INFO("Initializing Render2DLayer");

            try
            {
                initShaders();
                initRenderer();
                initCamera();
                
                // 清除之前的数据
                m_rectangles.clear();
                m_texturedRectangles.clear();
                m_textures.clear();

                m_initialized = true;
                TINA_LOG_INFO("Render2DLayer initialized successfully");
            }
            catch (const std::exception& e)
            {
                TINA_LOG_ERROR("Failed to initialize Render2DLayer: {}", e.what());
                if (bgfx::isValid(m_shaderProgram))
                {
                    bgfx::destroy(m_shaderProgram);
                    m_shaderProgram = BGFX_INVALID_HANDLE;
                }
            }
        }

        void onEvent(Event& event) override
        {
            if (event.type == Event::WindowResize)
            {
                uint32_t width = event.windowResize.width;
                uint32_t height = event.windowResize.height;

                if (bgfx::getInternalData() && bgfx::getInternalData()->context)
                {
                    m_camera->setProjection(
                        0.0f, static_cast<float>(width),
                        static_cast<float>(height), 0.0f
                    );

                    bgfx::reset(width, height, BGFX_RESET_VSYNC);
                    bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));

                    event.handled = true;
                }

                TINA_LOG_INFO("Render2DLayer: Window resized to {}x{}", width, height);
                event.handled = true;
            }
        }

        void onDetach() override
        {
            TINA_LOG_INFO("Shutting down Render2DLayer");

            try
            {
                if (bgfx::getInternalData()->context)
                {
                    bgfx::frame();
                }

                if (m_batchRenderer)
                {
                    m_batchRenderer->shutdown();
                }

                if (bgfx::isValid(m_shaderProgram))
                {
                    TINA_LOG_DEBUG("Destroying shader program");
                    bgfx::destroy(m_shaderProgram);
                    m_shaderProgram = BGFX_INVALID_HANDLE;
                }

                m_initialized = false;
                TINA_LOG_INFO("Render2DLayer shutdown completed");
            }
            catch (const std::exception& e)
            {
                TINA_LOG_ERROR("Error during Render2DLayer shutdown: {}", e.what());
                if (bgfx::isValid(m_shaderProgram))
                {
                    bgfx::destroy(m_shaderProgram);
                    m_shaderProgram = BGFX_INVALID_HANDLE;
                }
            }
        }

        void onUpdate(float deltaTime) override
        {
            // 在这里更新2D对象的位置、大小等属性
            // 而不是重新创建对象
        }

        void onRender() override
        {
            if (!m_initialized || !m_batchRenderer || !bgfx::isValid(m_shaderProgram))
            {
                return;
            }

            try
            {
                bgfx::setViewTransform(0, nullptr, glm::value_ptr(m_camera->getProjectionMatrix()));
                
                m_batchRenderer->begin();

                Scene* scene = getScene();
                if (scene)
                {
                    // 获取场景中所有具有Transform2D和Sprite组件的实体
                    auto& registry = scene->getRegistry();
                    auto view = registry.view<Transform2DComponent, SpriteComponent>();
                    
                    // 收集并排序所有要渲染的实体
                    std::vector<std::pair<int, entt::entity>> sortedEntities;
                    view.each([&sortedEntities](entt::entity entity, const Transform2DComponent& transform, const SpriteComponent& sprite) {
                        if (sprite.isVisible()) {
                            sortedEntities.emplace_back(sprite.getLayer(), entity);
                        }
                    });
                    
                    // 按层级排序
                    std::sort(sortedEntities.begin(), sortedEntities.end());
                    
                    // 渲染所有精灵
                    for (const auto& pair : sortedEntities)
                    {
                        auto entity = pair.second;
                        const auto& transform = registry.get<Transform2DComponent>(entity);
                        const auto& sprite = registry.get<SpriteComponent>(entity);
                        
                        if (sprite.getTexture() && sprite.getTexture()->isValid())
                        {
                            // 渲染纹理精灵
                            m_batchRenderer->drawTexturedQuad(
                                transform.getPosition(),
                                sprite.getSize() * transform.getScale(),
                                sprite.getTexture(),
                                sprite.getTextureRect(),
                                sprite.getColor()
                            );
                        }
                        else
                        {
                            // 渲染纯色矩形
                            m_batchRenderer->drawQuad(
                                transform.getPosition(),
                                sprite.getSize() * transform.getScale(),
                                sprite.getColor()
                            );
                        }
                    }
                }
                
                // 渲染其他矩形和纹理矩形(如果有的话)
                if (!m_rectangles.empty())
                {
                    m_batchRenderer->drawQuads(m_rectangles);
                }
                
                if (!m_texturedRectangles.empty())
                {
                    std::vector<BatchRenderer2D::InstanceData> instances;
                    std::vector<std::shared_ptr<Texture2D>> textures;
                    
                    for (const auto& [instance, texture] : m_texturedRectangles)
                    {
                        if (texture && texture->isValid())
                        {
                            instances.push_back(instance);
                            textures.push_back(texture);
                        }
                    }
                    
                    if (!instances.empty())
                    {
                        m_batchRenderer->drawTexturedQuads(instances, textures);
                    }
                }
                
                m_batchRenderer->end();
            }
            catch (const std::exception& e)
            {
                TINA_LOG_ERROR("Error during 2D rendering: {}", e.what());
            }
        }

    private:
        std::unique_ptr<OrthographicCamera> m_camera;
        bgfx::ProgramHandle m_shaderProgram = BGFX_INVALID_HANDLE;
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_textures;
        bool m_initialized = false;
        
        std::unique_ptr<BatchRenderer2D> m_batchRenderer;
        std::vector<BatchRenderer2D::InstanceData> m_rectangles;
        
        // 存储带纹理的矩形数据
        struct TexturedRectangle {
            BatchRenderer2D::InstanceData instance;
            std::shared_ptr<Texture2D> texture;
        };
        std::vector<TexturedRectangle> m_texturedRectangles;
    };
} // namespace Tina
