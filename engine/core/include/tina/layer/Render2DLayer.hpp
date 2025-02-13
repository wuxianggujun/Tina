#pragma once

#include "tina/layer/Layer.hpp"
#include "tina/systems/RenderSystem.hpp"
#include "tina/renderer/ShaderManager.hpp"
#include "tina/log/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include "bx/math.h"
#include "tina/core/OrthographicCamera.hpp"
#include "tina/event/Event.hpp"
#include "tina/renderer/Texture2D.hpp"
#include <unordered_map>
#include "tina/utils/BgfxUtils.hpp"
#include <entt/entt.hpp>

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
            // 清理前确保所有实体都被销毁
            m_registry.clear();
            m_textures.clear();
        }

        // 创建矩形
        entt::entity createRectangle(const glm::vec2& position, const glm::vec2& size, const Color& color)
        {
            auto entity = m_registry.create();
            
            auto& transform = m_registry.emplace<Transform2DComponent>(entity, position);
            auto& rectangle = m_registry.emplace<RectangleComponent>(entity, size);
            rectangle.setColor(color);
            
            return entity;
        }

        // 创建精灵
        entt::entity createSprite(const glm::vec2& position, const std::string& textureName)
        {
            auto entity = m_registry.create();
            
            auto& transform = m_registry.emplace<Transform2DComponent>(entity, position);
            auto& sprite = m_registry.emplace<SpriteComponent>(entity);
            
            auto it = m_textures.find(textureName);
            if (it != m_textures.end())
            {
                sprite.setTexture(it->second);
            }
            
            return entity;
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
            m_renderSystem = std::make_unique<RenderSystem>();
            m_renderSystem->init(m_shaderProgram);
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

        void loadTextures()
        {
            // 加载测试纹理
            const std::string texturePath = "resources/textures/test.png";
            TINA_LOG_INFO("Loading texture: {}", texturePath);
            
            auto result = Utils::BgfxUtils::loadTexture(texturePath);
            if (!bgfx::isValid(result.handle))
            {
                TINA_LOG_ERROR("Failed to load texture: {}", texturePath);
                return;
            }

            TINA_LOG_INFO("Texture loaded successfully. Size: {}x{}, Format: {}, Layers: {}, Depth: {}, HasMips: {}",
                result.width, result.height, static_cast<int>(result.format),
                result.layers, result.depth, result.hasMips);

            auto texture = Texture2D::create("test");
            texture->setNativeHandle(result.handle, result.width, result.height, 
                result.depth, result.hasMips, result.layers, result.format);

            if (texture->isValid())
            {
                m_textures["test"] = texture;
                TINA_LOG_INFO("Successfully created texture object: test ({}x{})", 
                    texture->getWidth(), texture->getHeight());
            }
        }

        void createTestObjects()
        {
            // 创建测试矩形
            createRectangle({100.0f, 100.0f}, {200.0f, 200.0f}, Color::Red);
            createRectangle({400.0f, 200.0f}, {200.0f, 200.0f}, Color::Blue);
            createRectangle({700.0f, 300.0f}, {200.0f, 200.0f}, Color::Green);
            createRectangle({300.0f, 400.0f}, {200.0f, 200.0f}, Color::Orange);

            // 创建测试精灵
            auto spriteEntity = createSprite({500.0f, 300.0f}, "test");
            if (auto* sprite = m_registry.try_get<SpriteComponent>(spriteEntity))
            {
                if (sprite->getTexture())
                {
                    float scale = 0.25f;
                    glm::vec2 size = {
                        static_cast<float>(sprite->getTexture()->getWidth()) * scale,
                        static_cast<float>(sprite->getTexture()->getHeight()) * scale
                    };
                    sprite->setSize(size);
                }
            }
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
                loadTextures();
                createTestObjects();

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
                    // 更新相机投影
                    m_camera->setProjection(
                        0.0f, static_cast<float>(width),
                        static_cast<float>(height), 0.0f
                    );

                    // 更新视口
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
                // 确保完成所有渲染操作
                if (bgfx::getInternalData()->context)
                {
                    bgfx::frame();
                }

                // 关闭渲染系统
                if (m_renderSystem)
                {
                    m_renderSystem->shutdown();
                }

                // 销毁着色器程序
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
            // 在这里更新2D对象
        }

        void onRender() override
        {
            if (!m_initialized || !m_renderSystem || !bgfx::isValid(m_shaderProgram))
            {
                return;
            }

            try
            {
                // 开始场景渲染
                m_renderSystem->beginScene(*m_camera);
                
                // 渲染所有实体
                m_renderSystem->render(m_registry);
                
                // 结束场景渲染
                m_renderSystem->endScene();
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
        
        // 新增的ECS相关成员
        entt::registry m_registry;
        std::unique_ptr<RenderSystem> m_renderSystem;
    };
} // namespace Tina
