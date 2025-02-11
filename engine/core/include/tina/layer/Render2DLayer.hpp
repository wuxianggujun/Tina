#pragma once

#include "tina/layer/Layer.hpp"
#include "tina/renderer/Renderer2D.hpp"
#include "tina/renderer/ShaderManager.hpp"
#include "tina/log/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Tina {

class Render2DLayer : public Layer {
public:
    Render2DLayer() : Layer("Render2DLayer") {
        TINA_LOG_INFO("Render2DLayer created");
    }

    void onAttach() override {
        if (m_initialized) {
            TINA_LOG_WARN("Render2DLayer already initialized");
            return;
        }

        TINA_LOG_INFO("Initializing Render2DLayer");

        try {
            // 加载2D渲染所需的着色器
            auto& shaderManager = ShaderManager::getInstance();
            
            TINA_LOG_DEBUG("Creating shader program");
            m_shaderProgram = shaderManager.createProgram("2d");
            if (!bgfx::isValid(m_shaderProgram)) {
                TINA_LOG_ERROR("Failed to create 2D shader program");
                return;
            }
            TINA_LOG_INFO("Successfully loaded 2D shaders");

            // 初始化 Renderer2D
            if (!Renderer2D::isInitialized()) {
                TINA_LOG_DEBUG("Initializing Renderer2D with shader program");
                try {
                    Renderer2D::init(m_shaderProgram);
                    
                    if (!Renderer2D::isInitialized()) {
                        TINA_LOG_ERROR("Failed to initialize Renderer2D");
                        if (bgfx::isValid(m_shaderProgram)) {
                            bgfx::destroy(m_shaderProgram);
                            m_shaderProgram = BGFX_INVALID_HANDLE;
                        }
                        return;
                    }
                } catch (const std::exception& e) {
                    TINA_LOG_ERROR("Exception during Renderer2D initialization: {}", e.what());
                    if (bgfx::isValid(m_shaderProgram)) {
                        bgfx::destroy(m_shaderProgram);
                        m_shaderProgram = BGFX_INVALID_HANDLE;
                    }
                    return;
                }
            }

            m_initialized = true;
            TINA_LOG_INFO("Render2DLayer initialized successfully");
        } catch (const std::exception& e) {
            TINA_LOG_ERROR("Failed to initialize Render2DLayer: {}", e.what());
            if (bgfx::isValid(m_shaderProgram)) {
                bgfx::destroy(m_shaderProgram);
                m_shaderProgram = BGFX_INVALID_HANDLE;
            }
        }
    }

    void onDetach() override {
        TINA_LOG_INFO("Shutting down Render2DLayer");
        
        // 关闭 Renderer2D
        if (Renderer2D::isInitialized()) {
            Renderer2D::shutdown();
        }

        // 销毁着色器程序
        if (bgfx::isValid(m_shaderProgram)) {
            bgfx::destroy(m_shaderProgram);
            m_shaderProgram = BGFX_INVALID_HANDLE;
        }

        m_initialized = false;
    }

    void onUpdate(float deltaTime) override {
        // 在这里更新2D对象
    }

    void onRender() override {
        if (!Renderer2D::isInitialized()) {
            TINA_LOG_ERROR("Renderer2D not initialized");
            return;
        }

        if (!bgfx::isValid(m_shaderProgram)) {
            TINA_LOG_ERROR("Invalid shader program");
            return;
        }

        try {
            // 获取当前窗口尺寸
            uint32_t width, height;
            Core::Engine::get().getWindowSize(width, height);

            // 创建投影矩阵
            glm::mat4 proj = glm::ortho(0.0f, static_cast<float>(width),
                                       static_cast<float>(height), 0.0f,
                                       -1.0f, 1.0f);

            // 开始场景渲染
            Renderer2D::beginScene(proj);

            // 绘制测试图形 - 使用更大的尺寸和更亮的颜色
            Renderer2D::drawQuad({100.0f, 100.0f}, {200.0f, 200.0f}, {1.0f, 0.0f, 0.0f, 1.0f});  // 大红色方块
            Renderer2D::drawQuad({400.0f, 200.0f}, {200.0f, 200.0f}, {0.0f, 1.0f, 0.0f, 1.0f});  // 大绿色方块
            Renderer2D::drawQuad({700.0f, 300.0f}, {200.0f, 200.0f}, {0.0f, 0.0f, 1.0f, 1.0f});  // 大蓝色方块
            Renderer2D::drawQuad({300.0f, 400.0f}, {200.0f, 200.0f}, {1.0f, 1.0f, 0.0f, 1.0f});  // 大黄色方块

            Renderer2D::endScene();

        } catch (const std::exception& e) {
            TINA_LOG_ERROR("Error during 2D rendering: {}", e.what());
        }
    }

private:
    bgfx::ProgramHandle m_shaderProgram = BGFX_INVALID_HANDLE;
    bool m_initialized = false;
};

} // namespace Tina
