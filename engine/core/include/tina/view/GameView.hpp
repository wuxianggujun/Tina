#pragma once

#include "tina/core/Engine.hpp"
#include "tina/core/OrthographicCamera.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/view/View.hpp"
#include "tina/resources/ResourceManager.hpp"
#include "tina/resources/TextureLoader.hpp"
#include "tina/renderer/Texture2D.hpp"
#include "tina/renderer/Renderer2D.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Tina {
    class Scene;

    class GameView : public View {
    public:
        GameView() : View("GameView") {
            TINA_LOG_INFO("GameView created");
        }

        ~GameView() {
            m_rectangles.clear();
            m_texturedRectangles.clear();
        }

        // 加载纹理
        std::shared_ptr<Texture2D> loadTexture(const std::string& name, const std::string& path) {
            return Core::Engine::get().getTextureManager().loadTexture(name, path);
        }

        // 创建矩形
        void createRectangle(const glm::vec2& position, const glm::vec2& size, const Color& color) {
            BatchRenderer2D::InstanceData instance;
            instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
            instance.Color = glm::vec4(color.getR(), color.getG(), color.getB(), color.getA());
            instance.TextureData = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            instance.TextureInfo = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f); // 非纹理quad

            if (m_rectangles.empty()) {
                m_rectangles.reserve(100);
            }
            m_rectangles.push_back(instance);
        }

        // 创建带纹理的矩形
        void createTexturedRectangle(const glm::vec2& position, const glm::vec2& size,
                                   const std::string& textureName,
                                   const glm::vec4& textureCoords = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                                   const Color& tint = Color::White) {
            auto texture = Core::Engine::get().getTextureManager().getTexture(textureName);
            if (!texture) {
                TINA_LOG_WARN("Texture '{}' not found", textureName);
                return;
            }

            BatchRenderer2D::InstanceData instance;
            instance.Transform = glm::vec4(position.x, position.y, size.x, size.y);
            instance.Color = glm::vec4(tint.getR(), tint.getG(), tint.getB(), tint.getA());
            instance.TextureData = textureCoords;
            instance.TextureInfo = glm::vec4(static_cast<float>(m_texturedRectangles.size()), 0.0f, 0.0f, 0.0f);

            if (m_texturedRectangles.empty()) {
                m_texturedRectangles.reserve(100);
            }
            m_texturedRectangles.push_back({instance, texture});
        }

    protected:
        void onAttach() override {
            if (!m_initialized) {
                try {
                    TINA_LOG_INFO("Initializing GameView");

                    initShaders();
                    initRenderer();
                    initCamera();

                    // 设置view参数
                    if (m_renderer && m_renderer->getBatchRenderer()) {
                        m_renderer->getBatchRenderer()->setViewId(renderState.viewId);

                        uint32_t width, height;
                        Core::Engine::get().getWindowSize(width, height);

                        // 更新视口
                        setViewPort(Rect(0, 0, width, height));
                        
                        // 设置清除标志和颜色
                        setClearFlag(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH);
                        setClearColor(0x303030ff);
                    }

                    // 清除之前的数据
                    m_rectangles.clear();
                    m_texturedRectangles.clear();

                    m_initialized = true;
                    TINA_LOG_INFO("GameView initialized successfully");
                }
                catch (const std::exception& e) {
                    TINA_LOG_ERROR("Failed to initialize GameView: {}", e.what());
                    if (bgfx::isValid(m_shaderProgram)) {
                        bgfx::destroy(m_shaderProgram);
                        m_shaderProgram = BGFX_INVALID_HANDLE;
                    }
                    throw;
                }
            }
        }

        void onDetach() override {
            TINA_LOG_INFO("Shutting down GameView");

            try {
                // 先清理所有纹理相关的资源
                m_texturedRectangles.clear();
                m_rectangles.clear();

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

            // 开始批处理渲染
            m_renderer->getBatchRenderer()->begin();

            // 渲染普通矩形
            if (!m_rectangles.empty()) {
                m_renderer->getBatchRenderer()->drawQuads(m_rectangles);
            }

            // 渲染带纹理的矩形
            if (!m_texturedRectangles.empty()) {
                std::vector<BatchRenderer2D::InstanceData> instances;
                std::vector<std::shared_ptr<Texture2D>> textures;
                
                for (const auto& texturedRect : m_texturedRectangles) {
                    instances.push_back(texturedRect.instance);
                    textures.push_back(texturedRect.texture);
                }

                m_renderer->getBatchRenderer()->drawTexturedQuads(instances, textures);
            }

            // 结束批处理渲染
            m_renderer->getBatchRenderer()->end();
        }

        void endRender() override {
            if (m_initialized && m_renderer) {
                // 清除本帧的渲染数据
                m_rectangles.clear();
                m_texturedRectangles.clear();
            }
            View::endRender();
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
        struct TexturedRectangle {
            BatchRenderer2D::InstanceData instance;
            std::shared_ptr<Texture2D> texture;
        };

        std::vector<BatchRenderer2D::InstanceData> m_rectangles;
        std::vector<TexturedRectangle> m_texturedRectangles;
        UniquePtr<Renderer2D> m_renderer;
        SharedPtr<OrthographicCamera> m_camera;
        bgfx::ProgramHandle m_shaderProgram = BGFX_INVALID_HANDLE;
        bool m_initialized = false;
    };

} // namespace Tina
