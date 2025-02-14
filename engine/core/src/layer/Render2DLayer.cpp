//
// Created by wuxianggujun on 2025/2/14.
//

#include "tina/layer/Render2DLayer.hpp"

#include "tina/renderer/TextureManager.hpp"

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

    std::shared_ptr<Texture2D> Render2DLayer::loadTexture(const std::string& name, const std::string& path)
    {
       return TextureManager::getInstance().loadTexture(name,path);
    }

    void Render2DLayer::createRectangle(const glm::vec2& position, const glm::vec2& size, const Color& color)
    {
        BatchRenderer2D::InstanceData instance;
        instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
        instance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
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
        instance.TextureIndex = static_cast<float>(m_texturedRectangles.size());

        m_texturedRectangles.push_back({instance, texture});
    }

    void Render2DLayer::initShaders()
    {
        auto& shaderManager = ShaderManager::getInstance();
        m_shaderProgram = shaderManager.createProgram("2d");
        if (!bgfx::isValid(m_shaderProgram))
        {
            throw std::runtime_error("Failed to create 2D shader program");
        }
        TINA_LOG_INFO("Successfully loaded 2D shaders");
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
    }

    void Render2DLayer::onAttach()
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

            if (bgfx::getInternalData() && bgfx::getInternalData()->context) {
                if (bgfx::isValid(m_shaderProgram)) {
                    TINA_LOG_DEBUG("Destroying shader program");
                    bgfx::destroy(m_shaderProgram);
                    m_shaderProgram = BGFX_INVALID_HANDLE;
                }
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
            }

            if (bgfx::getInternalData() && bgfx::getInternalData()->context)
            {
                bgfx::reset(width, height, BGFX_RESET_VSYNC);
                bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));
            }

            TINA_LOG_INFO("Render2DLayer: Window resized to {}x{}", width, height);
            event.handled = true;
        }
    }

    void Render2DLayer::onUpdate(float deltaTime)
    {
    }

    void Render2DLayer::onRender()
    {
        if (!m_initialized || !m_renderer) return;

        try
        {
            m_renderer->beginScene(m_camera);

            // 渲染场景中的实体
            if (Scene* scene = getScene())
            {
                m_sceneRenderer->render(scene, *m_renderer);
            }

            // 渲染独立的矩形
            for (const auto& rect : m_rectangles)
            {
                m_renderer->drawQuad(
                    glm::vec2(rect.Transform.x, rect.Transform.y),
                    glm::vec2(rect.Transform.z, rect.Transform.w),
                    Color(rect.Color.r, rect.Color.g, rect.Color.b, rect.Color.a)
                );
            }

            // 渲染带纹理的矩形
            for (const auto& texRect : m_texturedRectangles)
            {
                if (texRect.texture && texRect.texture->isValid())
                {
                    m_renderer->drawSprite(
                        glm::vec2(texRect.instance.Transform.x, texRect.instance.Transform.y),
                        glm::vec2(texRect.instance.Transform.z, texRect.instance.Transform.w),
                        texRect.texture,
                        texRect.instance.TextureData,
                        Color(texRect.instance.Color.r, texRect.instance.Color.g,
                              texRect.instance.Color.b, texRect.instance.Color.a)
                    );
                }
            }

            m_renderer->endScene();
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error during 2D rendering: {}", e.what());
        }
    }
}
