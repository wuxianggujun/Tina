//
// Created by wuxianggujun on 2025/2/16.
//

#pragma once
#include "tina/core/Engine.hpp"
#include "tina/core/OrthographicCamera.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/renderer/Scene2DRenderer.hpp"
#include "tina/view/View.hpp"

namespace Tina
{
    class Scene;

    class GameView : public View
    {
    public:
        GameView() : View("GameView")
        {
        }

        // 加载纹理
        std::shared_ptr<Texture2D> loadTexture(const std::string& name, const std::string& path)
        {
            return Core::Engine::get().getTextureManager().loadTexture(name, path);
        }

        // 创建矩形
        void createRectangle(const glm::vec2& position, const glm::vec2& size, const Color& color)
        {
            BatchRenderer2D::InstanceData instance;
            instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
            instance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
            instance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            instance.TextureInfo = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f); // 非纹理quad

            // 预分配一定大小以减少重新分配
            // if (m_rectangles.empty())
            // {
            //     m_rectangles.reserve(100);
            // }
            //
            // m_rectangles.push_back(instance);
        }

        // 创建带纹理的矩形
        void createTexturedRectangle(const glm::vec2& position, const glm::vec2& size,
                                     const std::string& textureName,
                                     const glm::vec4& textureCoords = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                                     const Color& tint = Color::White)
        {
            auto texture = Core::Engine::get().getTextureManager().getTexture(textureName);
            if (!texture)
            {
                TINA_LOG_WARN("Texture '{}' not found", textureName);
                return;
            }

            // BatchRenderer2D::InstanceData instance;
            // instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
            // instance.Color = glm::vec4(tint.getR(), tint.getG(), tint.getB(), tint.getA());
            // instance.TextureData = textureCoords;
            // instance.TextureInfo = glm::vec4(static_cast<float>(m_texturedRectangles.size()), 0.0f, 0.0f, 0.0f);
            //
            // if (m_texturedRectangles.empty())
            // {
            //     m_texturedRectangles.reserve(100);
            // }
            //
            // m_texturedRectangles.push_back(TexturedRectangle{instance, texture});
        }

        // 渲染场景实体
        void render(Scene* scene)
        {
            // if (!scene || !m_renderer) return;

            // 渲染场景中的实体
            // m_sceneRenderer->render(scene, *m_renderer);
        }

    protected:
        void onAttach() override
        {
            View::initializeView();

            // initShaders();
            // initRenderer();
            initCamera();
        }

        void onUpdate(float deltaTime) override
        {
            // 更新相机
            if (m_camera)
            {
                m_camera->onUpdate(deltaTime);
                updateTransform(
                    m_camera->getViewMatrix(),
                    m_camera->getProjectionMatrix()
                );
            }

            View::onUpdate(deltaTime);
        }

        void beginRender() override
        {
            if (!isVisible()) return;

            TINA_LOG_DEBUG("GameView: Begin render with viewport {}x{}",
                renderState.viewport.getWidth(),
                renderState.viewport.getHeight());

            // 确保使用最新的相机矩阵
            if (m_camera)
            {
                updateTransform(
                    m_camera->getViewMatrix(),
                    m_camera->getProjectionMatrix()
                );
            }

            // 调用父类的渲染
            View::beginRender();
        }

        void endRender() override
        {
            // if (m_renderer)
            // {
            //     m_renderer->endScene();
            // }
            View::endRender();
        }

        bool onEvent(Event& event) override
        {
            if (event.type == Event::WindowResize)
            {
                TINA_LOG_DEBUG("GameView: Handling WindowResize {}x{}",
                    event.windowResize.width,
                    event.windowResize.height);

                uint32_t width = event.windowResize.width;
                uint32_t height = event.windowResize.height;

                // 更新相机投影
                if (m_camera)
                {
                    m_camera->setProjection(
                        0.0f, static_cast<float>(width),
                        0.0f, static_cast<float>(height)
                    );

                    // 更新视图的变换矩阵
                    updateTransform(
                        m_camera->getViewMatrix(),
                        m_camera->getProjectionMatrix()
                    );
                    
                    TINA_LOG_DEBUG("GameView: Updated camera projection to {}x{}", width, height);
                }

                return true;
            }

            return View::onEvent(event);
        }

    private:
        // void initShaders()
        // {
        //     m_shaderProgram = Core::Engine::get().getShaderManager().createProgram("2d");
        //     if (!bgfx::isValid(m_shaderProgram))
        //     {
        //         throw std::runtime_error("Failed to create 2D shader program");
        //     }
        // }

        // void initRenderer()
        // {
        //     m_renderer = MakeUnique<Renderer2D>();
        //     m_renderer->init(m_shaderProgram);
        //     m_renderer->setViewId(renderState.viewId);
        //     // m_sceneRenderer = MakeUnique<Scene2DRenderer>();
        // }

        void initCamera()
        {
            uint32_t width, height;
            Core::Engine::get().getWindowSize(width, height);

            m_camera = MakeShared<OrthographicCamera>(
                0.0f, static_cast<float>(width),
                0.0f, static_cast<float>(height)
            );
            m_camera->setProjectionType(OrthographicCamera::ProjectionType::ScreenSpace);

            TINA_LOG_DEBUG("GameView: Camera initialized with {}x{}", width, height);
        }

        // UniquePtr<Renderer2D> m_renderer;
        SharedPtr<OrthographicCamera> m_camera;
        // UniquePtr<Scene2DRenderer> m_sceneRenderer;
        // bgfx::ProgramHandle m_shaderProgram = BGFX_INVALID_HANDLE;
        //
        // // 存储实例数据
        // std::vector<BatchRenderer2D::InstanceData> m_rectangles;
        //
        // struct TexturedRectangle
        // {
        //     BatchRenderer2D::InstanceData instance;
        //     std::shared_ptr<Texture2D> texture;
        // };
        //
        // std::vector<TexturedRectangle> m_texturedRectangles;
    };
} // Tina
