#pragma once

#include "tina/layer/Layer.hpp"
#include "tina/renderer/Renderer2D.hpp"
#include "tina/renderer/ShaderManager.hpp"
#include "tina/log/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include "bx/math.h"
#include "tina/core/OrthographicCamera.hpp"
#include "tina/event/Event.hpp"
#include "tina/renderer/Texture2D.hpp"
#include <unordered_map>
#include "tina/utils/BgfxUtils.hpp"

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
            for (auto& [name, texture] : m_textures)
            {
                delete texture;
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

        void initRenderer2D()
        {
            if (!Renderer2D::isInitialized())
            {
                Renderer2D::init(m_shaderProgram);
                if (!Renderer2D::isInitialized())
                {
                    throw std::runtime_error("Failed to initialize Renderer2D");
                }
            }
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

            auto texture = new Texture2D("test");
            texture->setNativeHandle(result.handle, result.width, result.height, 
                result.depth, result.hasMips, result.layers, result.format);

            if (texture->isValid())
            {
                m_textures["test"] = texture;
                TINA_LOG_INFO("Successfully created texture object: test ({}x{})", 
                    texture->getWidth(), texture->getHeight());
            }
            else
            {
                TINA_LOG_ERROR("Failed to create texture object: test");
                delete texture;
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
                initRenderer2D();
                initCamera();
                loadTextures();

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

                    // 设置新的视图变换
                    bgfx::setViewTransform(0, m_camera->getView(), m_camera->getProjection());

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

                // 先关闭 Renderer2D
                if (Renderer2D::isInitialized())
                {
                    TINA_LOG_DEBUG("Shutting down Renderer2D");
                    Renderer2D::shutdown();
                }

                // 然后销毁着色器程序
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
                // 确保着色器程序被销毁
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
            if (!m_initialized || !Renderer2D::isInitialized() || !bgfx::isValid(m_shaderProgram))
            {
                return;
            }

            try
            {
                // 开始场景渲染
                Renderer2D::beginScene(m_camera->getProjectionMatrix());

                // 绘制测试图形
                Renderer2D::drawRect({100.0f, 100.0f}, {200.0f, 200.0f}, Color::Red);
                Renderer2D::drawRect({400.0f, 200.0f}, {200.0f, 200.0f}, Color::Blue);
                Renderer2D::drawRect({700.0f, 300.0f}, {200.0f, 200.0f}, Color::Green);
                Renderer2D::drawRect({300.0f, 400.0f}, {200.0f, 200.0f}, Color::Orange);

                // 绘制纹理
                auto it = m_textures.find("test");
                if (it != m_textures.end() && it->second->isValid())
                {
                    float scale = 0.25f;  // 缩小缩放因子
                    float width = static_cast<float>(it->second->getWidth()) * scale;
                    float height = static_cast<float>(it->second->getHeight()) * scale;
                    
                    TINA_LOG_DEBUG("Drawing texture: {}x{} at position (500, 300)", width, height);
                    
                    Renderer2D::drawTexturedRect(
                        {500.0f, 300.0f},    // 位置
                        {width, height},      // 使用实际纹理尺寸（经过缩放）
                        it->second->getNativeHandle(),
                        Color::White
                    );
                }
                else
                {
                    TINA_LOG_WARN("Test texture not found or invalid");
                }

                Renderer2D::endScene();
            }
            catch (const std::exception& e)
            {
                TINA_LOG_ERROR("Error during 2D rendering: {}", e.what());
            }
        }

    private:
        std::unique_ptr<OrthographicCamera> m_camera;
        bgfx::ProgramHandle m_shaderProgram = BGFX_INVALID_HANDLE;
        std::unordered_map<std::string, Texture2D*> m_textures;
        bool m_initialized = false;
    };
} // namespace Tina
