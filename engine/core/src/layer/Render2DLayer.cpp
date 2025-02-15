//
// Created by wuxianggujun on 2025/2/14.
//

#include "tina/layer/Render2DLayer.hpp"
#include "tina/renderer/TextureManager.hpp"
#include "tina/renderer/ShaderManager.hpp"

namespace Tina
{
    Render2DLayer::Render2DLayer() : Layer("Render2DLayer")
    {
        TINA_LOG_INFO("Render2DLayer created");
    }

    Render2DLayer::~Render2DLayer()
    {
        m_rectangles.clear();
        m_texturedRectangles.clear();
    }

    void Render2DLayer::onAttach()
    {
        if (!m_initialized)
        {
            try
            {
                TINA_LOG_INFO("Initializing Render2DLayer");
                initShaders();
                initRenderer();
                initCamera();

                // 清除之前的数据
                m_rectangles.clear();
                m_texturedRectangles.clear();

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
    }

    void Render2DLayer::onDetach()
    {
        TINA_LOG_INFO("Shutting down Render2DLayer");

        try
        {
            // 先清理所有纹理相关的资源
            m_texturedRectangles.clear();
            m_rectangles.clear();

            if (m_renderer)
            {
                m_renderer->shutdown();
            }

            // 使用ShaderManager销毁shader program
            if (bgfx::isValid(m_shaderProgram))
            {
                TINA_LOG_DEBUG("Destroying shader program");
                ShaderManager::getInstance().destroyProgram(m_shaderProgram);
                m_shaderProgram = BGFX_INVALID_HANDLE;
            }

            m_initialized = false;
            TINA_LOG_INFO("Render2DLayer shutdown completed");
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error during Render2DLayer shutdown: {}", e.what());
            // 即使发生错误也要尝试清理shader
            if (bgfx::isValid(m_shaderProgram))
            {
                ShaderManager::getInstance().destroyProgram(m_shaderProgram);
                m_shaderProgram = BGFX_INVALID_HANDLE;
            }
        }
    }

    void Render2DLayer::onUpdate(float deltaTime)
    {
        if (m_camera)
        {
            m_camera->onUpdate(deltaTime);
        }
    }

    void Render2DLayer::onRender()
    {
        if (!m_initialized || !m_renderer) return;

        static bool rendering = false;
        if (rendering) return; // 防止重入

        try
        {
            rendering = true;
            m_renderer->beginScene(m_camera);

            // 渲染场景中的实体
            if (Scene* scene = getScene())
            {
                m_sceneRenderer->render(scene, *m_renderer);
            }
            m_renderer->endScene();
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error during 2D rendering: {}", e.what());
        }
        rendering = false;
    }

    void Render2DLayer::onEvent(Event& event)
    {
        if (event.type == Event::WindowResize)
        {
            uint32_t width = event.windowResize.width;
            uint32_t height = event.windowResize.height;

            if (m_camera)
            {
                m_camera->setProjection(
                    0.0f, static_cast<float>(width),
                    static_cast<float>(height), 0.0f
                );
                TINA_LOG_DEBUG("Camera projection updated for window resize: {}x{}", width, height);
            }

            if (bgfx::getInternalData() && bgfx::getInternalData()->context)
            {
                bgfx::reset(width, height, BGFX_RESET_VSYNC);
                bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));
                TINA_LOG_DEBUG("View rect updated: {}x{}", width, height);
            }
            event.handled = true;
        }
        // 让相机处理其他事件
        if (m_camera && !event.handled)
        {
            m_camera->onEvent(event);
        }
    }

    std::shared_ptr<Texture2D> Render2DLayer::loadTexture(const std::string& name, const std::string& path)
    {
        return TextureManager::getInstance().loadTexture(name, path);
    }

    void Render2DLayer::createRectangle(const glm::vec2& position, const glm::vec2& size, const Color& color)
    {
        BatchRenderer2D::InstanceData instance;
        instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
        instance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
        instance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        instance.TextureInfo = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);  // 非纹理quad
        m_rectangles.push_back(instance);
    }

    void Render2DLayer::createTexturedRectangle(const glm::vec2& position, const glm::vec2& size,
                                                const std::string& textureName, const glm::vec4& textureCoords,
                                                const Color& tint)
    {
        auto texture = TextureManager::getInstance().getTexture(textureName);
        if (!texture)
        {
            TINA_LOG_WARN("Texture '{}' not found", textureName);
            return;
        }

        BatchRenderer2D::InstanceData instance;
        instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
        instance.Color = glm::vec4(tint.getR(), tint.getG(), tint.getB(), tint.getA());
        instance.TextureData = textureCoords;
        instance.TextureInfo = glm::vec4(static_cast<float>(m_texturedRectangles.size()), 0.0f, 0.0f, 0.0f);

        m_texturedRectangles.push_back({instance, texture});
    }

    void Render2DLayer::initShaders()
    {
        try {
            m_shaderProgram = ShaderManager::getInstance().createProgram("2d");
            if (!bgfx::isValid(m_shaderProgram))
            {
                throw std::runtime_error("Failed to create 2D shader program");
            }
            TINA_LOG_INFO("Successfully loaded 2D shaders");
        } catch (const std::exception& e) {
            TINA_LOG_ERROR("Failed to initialize shaders: {}", e.what());
            if (bgfx::isValid(m_shaderProgram))
            {
                ShaderManager::getInstance().destroyProgram(m_shaderProgram);
                m_shaderProgram = BGFX_INVALID_HANDLE;
            }
            throw;
        }
    }

    void Render2DLayer::initRenderer()
    {
        m_renderer = MakeUnique<Renderer2D>();
        m_renderer->init(m_shaderProgram);
        m_sceneRenderer = MakeUnique<Scene2DRenderer>();
    }

    void Render2DLayer::initCamera()
    {
        uint32_t width, height;
        Core::Engine::get().getWindowSize(width, height);
        m_camera = MakeShared<OrthographicCamera>(
            0.0f, static_cast<float>(width),
            static_cast<float>(height), 0.0f
        );
        m_camera->setProjectionType(OrthographicCamera::ProjectionType::ScreenSpace);

        TINA_LOG_INFO("Camera initialized with screen space projection {}x{}", width, height);
    }
}
