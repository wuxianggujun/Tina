//
// Created by wuxianggujun on 2025/2/14.
//

#include "tina/core/Engine.hpp"
#include "tina/layer/Render2DLayer.hpp"
#include "tina/log/Logger.hpp"

/*
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

                m_ViewId = Core::Engine::get().allocateViewId();
                TINA_LOG_INFO("Allocated viewId: {}", m_ViewId);

                initShaders();
                initRenderer();
                initCamera();

                // 设置view参数
                if (m_renderer && m_renderer->getBatchRenderer())
                {
                    m_renderer->getBatchRenderer()->setViewId(m_ViewId);

                    uint32_t width, height;
                    Core::Engine::get().getWindowSize(width, height);

                    m_renderer->getBatchRenderer()->setViewRect(0, 0, width, height);
                    m_renderer->getBatchRenderer()->setViewClear(
                        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
                    );
                }

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
                Core::Engine::get().getShaderManager().destroyProgram(m_shaderProgram);
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
                Core::Engine::get().getShaderManager().destroyProgram(m_shaderProgram);
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


            // 每100帧收缩一次vectors
            static int frameCount = 0;
            if (++frameCount >= 100)
            {
                frameCount = 0;
                m_rectangles.shrink_to_fit();
                m_texturedRectangles.shrink_to_fit();
                TINA_LOG_DEBUG("Shrunk instance vectors - Rectangles capacity: {}, Textured rectangles capacity: {}",
                               m_rectangles.capacity(), m_texturedRectangles.capacity());
            }

            m_renderer->beginScene(m_camera);

            // 渲染场景中的实体
            if (Scene* scene = getScene())
            {
                m_sceneRenderer->render(scene, *m_renderer);
            }

            // 输出当前内存使用情况
            size_t totalMemoryUsage =
                m_rectangles.capacity() * sizeof(BatchRenderer2D::InstanceData) +
                m_texturedRectangles.capacity() * (sizeof(BatchRenderer2D::InstanceData) + sizeof(std::shared_ptr<
                    Texture2D>));

            TINA_LOG_DEBUG("Total rendering memory usage: {} bytes", totalMemoryUsage);

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
                bgfx::setViewRect(m_ViewId, 0, 0, uint16_t(width), uint16_t(height));
            }

            if (m_renderer && m_renderer->getBatchRenderer())
            {
                m_renderer->getBatchRenderer()->setViewRect(0, 0, width, height);
            }

            event.handled = true;
        }
        // 让相机处理其他事件
        if (m_camera && !event.handled)
        {
            m_camera->onEvent(event);
        }
    }

    void Render2DLayer::setViewId(uint16_t viewId)
    {
        m_ViewId = viewId;

        if (m_renderer)
        {
            m_renderer->setViewId(viewId);  // 设置到Renderer2D
            if (m_renderer->getBatchRenderer())
            {
                m_renderer->getBatchRenderer()->setViewId(viewId);

                uint32_t width, height;
                Core::Engine::get().getWindowSize(width, height);

                m_renderer->getBatchRenderer()->setViewRect(viewId, 0, width, height);
                m_renderer->getBatchRenderer()->setViewClear(
                    BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                    0x303030ff, 1.0f, 0
                );
            }
        }
    }

    std::shared_ptr<Texture2D> Render2DLayer::loadTexture(const std::string& name, const std::string& path)
    {
        return Core::Engine::get().getTextureManager().loadTexture(name, path);
    }

    void Render2DLayer::createRectangle(const glm::vec2& position, const glm::vec2& size, const Color& color)
    {
        BatchRenderer2D::InstanceData instance;
        instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
        instance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
        instance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        instance.TextureInfo = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f); // 非纹理quad

        // 预分配一定大小以减少重新分配
        if (m_rectangles.empty())
        {
            m_rectangles.reserve(100);
        }

        TINA_LOG_DEBUG("Rectangle instances count: {}, Memory usage: {} bytes",
                       m_rectangles.size(),
                       m_rectangles.size() * sizeof(BatchRenderer2D::InstanceData));

        m_rectangles.push_back(instance);
    }

    void Render2DLayer::createTexturedRectangle(const glm::vec2& position, const glm::vec2& size,
                                                const std::string& textureName, const glm::vec4& textureCoords,
                                                const Color& tint)
    {
        auto texture = Core::Engine::get().getTextureManager().getTexture(textureName);
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

        // 预分配一定大小以减少重新分配
        if (m_texturedRectangles.empty())
        {
            m_texturedRectangles.reserve(100);
        }

        TINA_LOG_DEBUG("Textured rectangle instances count: {}, Memory usage: {} bytes",
                       m_texturedRectangles.size(),
                       m_texturedRectangles.size() * (sizeof(BatchRenderer2D::InstanceData) + sizeof(std::shared_ptr<
                           Texture2D>)));

        m_texturedRectangles.push_back({instance, texture});
    }

    void Render2DLayer::initShaders()
    {
        try
        {
            m_shaderProgram = Core::Engine::get().getShaderManager().createProgram("2d");
            if (!bgfx::isValid(m_shaderProgram))
            {
                throw std::runtime_error("Failed to create 2D shader program");
            }
            TINA_LOG_INFO("Successfully loaded 2D shaders");
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Failed to initialize shaders: {}", e.what());
            if (bgfx::isValid(m_shaderProgram))
            {
                Core::Engine::get().getShaderManager().destroyProgram(m_shaderProgram);
                m_shaderProgram = BGFX_INVALID_HANDLE;
            }
            throw;
        }
    }

    void Render2DLayer::initRenderer()
    {
        m_renderer = MakeUnique<Renderer2D>();
        m_renderer->init(m_shaderProgram);
        m_renderer->setViewId(m_ViewId);
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
*/
