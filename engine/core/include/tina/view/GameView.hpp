#pragma once

#include "tina/core/Engine.hpp"
#include "tina/core/OrthographicCamera.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/view/View.hpp"
#include "tina/resources/ResourceManager.hpp"
#include "tina/resources/TextureLoader.hpp"
#include "tina/renderer/Texture2D.hpp"
#include "tina/renderer/Renderer2D.hpp"
#include "tina/renderer/GameViewRenderer.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Tina {
    class Scene;

    class GameView : public View {
    public:
        GameView() : View("GameView") {
            TINA_LOG_INFO("GameView created");
        }

        ~GameView() override = default;

        // 加载纹理
        std::shared_ptr<Texture2D> loadTexture(const std::string& name, const std::string& path) {
            return Core::Engine::get().getTextureManager().loadTexture(name, path);
        }

        void onAttach() override {
            if (!m_initialized) {
                try {
                    TINA_LOG_INFO("Initializing GameView");

                    initShaders();
                    initRenderer();
                    initCamera();

                    // 设置view参数
                    if (m_renderer && m_renderer->getBatchRenderer()) {
                        auto* batchRenderer = m_renderer->getBatchRenderer();
                        batchRenderer->setViewId(renderState.viewId);

                        uint32_t width, height;
                        Core::Engine::get().getWindowSize(width, height);

                        // 更新视口
                        setViewPort(Rect(0, 0, width, height));
                        
                        // 设置BatchRenderer的视口
                        batchRenderer->setViewRect(0, 0, width, height);
                        
                        // 设置清除标志和颜色
                        batchRenderer->setViewClear(
                            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                            0x303030ff, 1.0f, 0
                        );
                        
                        TINA_LOG_DEBUG("View parameters set - ViewId: {}, Size: {}x{}", 
                            renderState.viewId, width, height);
                    }

                    m_initialized = true;
                    TINA_LOG_INFO("GameView initialized successfully");
                }
                catch (const std::exception& e) {
                    TINA_LOG_ERROR("Failed to initialize GameView: {}", e.what());
                    if (bgfx::isValid(m_shaderProgram)) {
                        m_shaderProgram = BGFX_INVALID_HANDLE;
                    }
                    throw;
                }
            }
        }

        void onDetach() override {
            TINA_LOG_INFO("Shutting down GameView");

            try {
                if (m_renderer) {
                    m_renderer->shutdown();
                }

                // 使用ShaderManager销毁shader program
                if (bgfx::isValid(m_shaderProgram)) {
                    TINA_LOG_DEBUG("Destroying shader program");
                    Core::Engine::get().getShaderManager().destroyProgram(m_shaderProgram);
                    m_shaderProgram = BGFX_INVALID_HANDLE;
                }

                m_initialized = false;
                TINA_LOG_INFO("GameView shutdown completed");
            }
            catch (const std::exception& e) {
                TINA_LOG_ERROR("Error during GameView shutdown: {}", e.what());
                // 即使发生错误也要尝试清理shader
                if (bgfx::isValid(m_shaderProgram)) {
                    Core::Engine::get().getShaderManager().destroyProgram(m_shaderProgram);
                    m_shaderProgram = BGFX_INVALID_HANDLE;
                }
            }
        }

        void onUpdate(float deltaTime) override {
            if (m_camera) {
                m_camera->onUpdate(deltaTime);
                
                // 更新渲染状态中的视图和投影矩阵
                renderState.viewMatrix = m_camera->getViewMatrix();
                renderState.projMatrix = m_camera->getProjectionMatrix();
                updateRenderState();
            }
            
            // 调用基类更新
            View::onUpdate(deltaTime);
        }

        void beginRender() override {
            if (!isVisible() || !m_initialized || !m_renderer) return;

            View::beginRender();
            
            // 开始场景渲染
            if (m_camera) {
                m_renderer->beginScene(m_camera);
            }
        }

        void endRender() override {
            if (m_initialized && m_renderer) {
                // 结束场景渲染
                m_renderer->endScene();
            }
            View::endRender();
        }

        void render(Scene* scene) override {
            if (!isVisible() || !m_initialized || !m_renderer) return;

            // 开始渲染
            beginRender();

            // 使用 GameViewRenderer 渲染场景
            m_viewRenderer.render(scene, *m_renderer);

            // 结束渲染
            endRender();
        }

        bool onEvent(Event& event) override {
            bool handled = false;

            if (event.type == Event::WindowResize) {
                uint32_t width = event.windowResize.width;
                uint32_t height = event.windowResize.height;

                if (m_camera) {
                    m_camera->setProjection(
                        0.0f, static_cast<float>(width),
                        static_cast<float>(height), 0.0f
                    );
                }

                // 更新视口
                setViewPort(Rect(0, 0, width, height));
                
                // 更新BatchRenderer的视口
                if (m_renderer && m_renderer->getBatchRenderer()) {
                    m_renderer->getBatchRenderer()->setViewRect(0, 0, width, height);
                }
                
                handled = true;
            }

            // 如果当前视图没有处理事件，让基类处理
            if (!handled) {
                handled = View::onEvent(event);
            }

            return handled;
        }

    private:
        void initShaders() {
            try {
                m_shaderProgram = Core::Engine::get().getShaderManager().createProgram("2d");
                if (!bgfx::isValid(m_shaderProgram)) {
                    throw std::runtime_error("Failed to create 2D shader program");
                }
                TINA_LOG_INFO("Successfully loaded 2D shaders");
            }
            catch (const std::exception& e) {
                TINA_LOG_ERROR("Failed to initialize shaders: {}", e.what());
                throw;
            }
        }

        void initRenderer() {
            m_renderer = MakeUnique<Renderer2D>();
            m_renderer->init(m_shaderProgram);
            m_renderer->setViewId(renderState.viewId);
        }

        void initCamera() {
            uint32_t width, height;
            Core::Engine::get().getWindowSize(width, height);
            m_camera = MakeShared<OrthographicCamera>(
                0.0f, static_cast<float>(width),
                static_cast<float>(height), 0.0f
            );
            m_camera->setProjectionType(OrthographicCamera::ProjectionType::ScreenSpace);

            // 初始化渲染状态的视图和投影矩阵
            renderState.viewMatrix = m_camera->getViewMatrix();
            renderState.projMatrix = m_camera->getProjectionMatrix();

            TINA_LOG_INFO("Camera initialized with screen space projection {}x{}", width, height);
        }

    private:
        UniquePtr<Renderer2D> m_renderer;
        SharedPtr<OrthographicCamera> m_camera;
        bgfx::ProgramHandle m_shaderProgram = BGFX_INVALID_HANDLE;
        bool m_initialized = false;
        
        // 组件渲染器
        GameViewRenderer m_viewRenderer;
    };

} // namespace Tina
