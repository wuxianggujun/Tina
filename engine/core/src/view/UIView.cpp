#include "tina/view/UIView.hpp"
#include "tina/core/Engine.hpp"
#include "tina/log/Logger.hpp"

namespace Tina {

UIView::UIView() : View("UIView") {
    TINA_LOG_INFO("UIView created");
}

void UIView::onAttach() {
    if (!m_initialized) {
        try {
            TINA_LOG_INFO("Initializing UIView");

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
                uint32_t bgColor = 
                    (static_cast<uint32_t>(m_backgroundColor.r * 255) << 24) |
                    (static_cast<uint32_t>(m_backgroundColor.g * 255) << 16) |
                    (static_cast<uint32_t>(m_backgroundColor.b * 255) << 8) |
                    static_cast<uint32_t>(m_backgroundColor.a * 255);
                
                batchRenderer->setViewClear(
                    BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                    bgColor, 1.0f, 0
                );
                
                TINA_LOG_DEBUG("View parameters set - ViewId: {}, Size: {}x{}", 
                    renderState.viewId, width, height);
            }

            m_initialized = true;
            TINA_LOG_INFO("UIView initialized successfully");
        }
        catch (const std::exception& e) {
            TINA_LOG_ERROR("Failed to initialize UIView: {}", e.what());
            if (bgfx::isValid(m_shaderProgram)) {
                m_shaderProgram = BGFX_INVALID_HANDLE;
            }
            throw;
        }
    }
}

void UIView::onDetach() {
    TINA_LOG_INFO("Shutting down UIView");

    try {
        if (m_renderer) {
            m_renderer->shutdown();
        }

        if (bgfx::isValid(m_shaderProgram)) {
            TINA_LOG_DEBUG("Destroying shader program");
            Core::Engine::get().getShaderManager().destroyProgram(m_shaderProgram);
            m_shaderProgram = BGFX_INVALID_HANDLE;
        }

        m_initialized = false;
        TINA_LOG_INFO("UIView shutdown completed");
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Error during UIView shutdown: {}", e.what());
        if (bgfx::isValid(m_shaderProgram)) {
            Core::Engine::get().getShaderManager().destroyProgram(m_shaderProgram);
            m_shaderProgram = BGFX_INVALID_HANDLE;
        }
    }
}

void UIView::onUpdate(float deltaTime) {
    if (m_camera) {
        m_camera->onUpdate(deltaTime);
        
        renderState.viewMatrix = m_camera->getViewMatrix();
        renderState.projMatrix = m_camera->getProjectionMatrix();
        updateRenderState();
    }
    
    View::onUpdate(deltaTime);
}

void UIView::beginRender() {
    if (!isVisible() || !m_initialized || !m_renderer) return;

    View::beginRender();
    
    if (m_camera) {
        m_renderer->beginScene(m_camera);
    }
    
    // 清空绘制命令列表
    m_drawCommands.clear();
}

void UIView::endRender() {
    if (m_initialized && m_renderer) {
        // 执行所有绘制命令
        for (const auto& cmd : m_drawCommands) {
            cmd();
        }
        m_renderer->endScene();
    }
    View::endRender();
}

void UIView::render(Scene* scene) {
    if (!isVisible() || !m_initialized || !m_renderer) return;

    beginRender();
    endRender();
}

bool UIView::onEvent(Event& event) {
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

        setViewPort(Rect(0, 0, width, height));
        
        if (m_renderer && m_renderer->getBatchRenderer()) {
            m_renderer->getBatchRenderer()->setViewRect(0, 0, width, height);
        }
        
        handled = true;
    }

    if (!handled) {
        handled = View::onEvent(event);
    }

    return handled;
}

void UIView::setBackgroundColor(const UIColor& color) {
    m_backgroundColor = color;
    
    if (m_renderer && m_renderer->getBatchRenderer()) {
        uint32_t bgColor = 
            (static_cast<uint32_t>(color.r * 255) << 24) |
            (static_cast<uint32_t>(color.g * 255) << 16) |
            (static_cast<uint32_t>(color.b * 255) << 8) |
            static_cast<uint32_t>(color.a * 255);
            
        m_renderer->getBatchRenderer()->setViewClear(
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            bgColor, 1.0f, 0
        );
    }
}

void UIView::drawLine(const glm::vec2& start, const glm::vec2& end, const LineStyle& style) {
    if (!m_renderer) return;
    
    m_drawCommands.push_back([this, start, end, style]() {
        // 计算线段的方向和长度
        glm::vec2 direction = end - start;
        float length = glm::length(direction);
        
        if (length < 0.0001f) return; // 防止除零
        
        // 归一化方向
        direction /= length;
        
        // 计算垂直于线段的向量，用于生成线段的宽度
        glm::vec2 normal(-direction.y, direction.x);
        normal *= style.thickness * 0.5f;
        
        // 生成线段的四个顶点位置
        glm::vec2 v0 = start + normal;
        glm::vec2 v1 = end + normal;
        glm::vec2 v2 = end - normal;
        glm::vec2 v3 = start - normal;
        
        // 计算线段的大小
        glm::vec2 size = end - start;
        
        // 使用单个drawQuad绘制线段
        m_renderer->drawQuad(
            start,
            {glm::length(size), style.thickness},
            Color(style.color.r, style.color.g, style.color.b, style.color.a)
        );
    });
}

void UIView::drawRect(const glm::vec2& position, const glm::vec2& size, const RectStyle& style) {
    if (!m_renderer) return;
    
    m_drawCommands.push_back([this, position, size, style]() {
        // 绘制填充
        if (style.filled) {
            m_renderer->drawQuad(
                position,
                size,
                Color(style.fillColor.r, style.fillColor.g, style.fillColor.b, style.fillColor.a)
            );
        }
        
        // 绘制边框
        if (style.borderThickness > 0.0f) {
            // 上边
            drawLine(
                position,
                {position.x + size.x, position.y},
                {style.borderThickness, style.borderColor}
            );
            
            // 右边
            drawLine(
                {position.x + size.x, position.y},
                {position.x + size.x, position.y + size.y},
                {style.borderThickness, style.borderColor}
            );
            
            // 下边
            drawLine(
                {position.x + size.x, position.y + size.y},
                {position.x, position.y + size.y},
                {style.borderThickness, style.borderColor}
            );
            
            // 左边
            drawLine(
                {position.x, position.y + size.y},
                position,
                {style.borderThickness, style.borderColor}
            );
        }
    });
}

void UIView::drawCircle(const glm::vec2& center, float radius, const RectStyle& style) {
    if (!m_renderer) return;
    
    m_drawCommands.push_back([this, center, radius, style]() {
        constexpr int segments = 32; // 圆形的段数
        constexpr float PI = 3.14159265359f;
        
        std::vector<glm::vec2> points;
        points.reserve(segments);
        
        // 生成圆周上的点
        for (int i = 0; i < segments; ++i) {
            float angle = (float)i / (float)segments * 2.0f * PI;
            points.emplace_back(
                center.x + radius * cos(angle),
                center.y + radius * sin(angle)
            );
        }
        
        // 绘制填充
        if (style.filled) {
            // 使用三角扇形绘制圆形填充
            for (int i = 0; i < segments; ++i) {
                int nextIndex = (i + 1) % segments;
                
                // 计算三角形的大小和位置
                glm::vec2 triangleSize = points[nextIndex] - points[i];
                glm::vec2 trianglePos = points[i];
                
                m_renderer->drawQuad(
                    trianglePos,
                    triangleSize,
                    Color(style.fillColor.r, style.fillColor.g, style.fillColor.b, style.fillColor.a)
                );
            }
        }
        
        // 绘制边框
        if (style.borderThickness > 0.0f) {
            for (int i = 0; i < segments; ++i) {
                int nextIndex = (i + 1) % segments;
                drawLine(
                    points[i],
                    points[nextIndex],
                    {style.borderThickness, style.borderColor}
                );
            }
        }
    });
}

void UIView::drawTexture(const std::shared_ptr<Texture2D>& texture, const glm::vec2& position, const glm::vec2& size, const UIColor& tint) {
    if (!m_renderer || !texture) return;
    
    m_drawCommands.push_back([this, texture, position, size, tint]() {
        m_renderer->drawSprite(
            position,
            size,
            texture,
            glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
            Color(tint.r, tint.g, tint.b, tint.a)
        );
    });
}

void UIView::initShaders() {
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

void UIView::initRenderer() {
    m_renderer = MakeUnique<Renderer2D>();
    m_renderer->init(m_shaderProgram);
    m_renderer->setViewId(renderState.viewId);
}

void UIView::initCamera() {
    uint32_t width, height;
    Core::Engine::get().getWindowSize(width, height);
    m_camera = MakeShared<OrthographicCamera>(
        0.0f, static_cast<float>(width),
        static_cast<float>(height), 0.0f
    );
    m_camera->setProjectionType(OrthographicCamera::ProjectionType::ScreenSpace);

    renderState.viewMatrix = m_camera->getViewMatrix();
    renderState.projMatrix = m_camera->getProjectionMatrix();

    TINA_LOG_INFO("Camera initialized with screen space projection {}x{}", width, height);
}

} // namespace Tina 