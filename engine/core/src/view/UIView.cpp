#include "tina/view/UIView.hpp"
#include "tina/core/Engine.hpp"
#include "tina/log/Logger.hpp"
#include "tina/renderer/Color.hpp"
#include "tina/scene/Scene.hpp"
#include "tina/event/Event.hpp"
#include "tina/core/OrthographicCamera.hpp"
#include <bgfx/bgfx.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace Tina {

UIView::UIView(const std::string& name) : View(name) {
    // 设置较高的zOrder确保UI显示在最上层
    setZOrder(100);
    
    // 设置默认背景色为透明
    m_backgroundColor = {0.0f, 0.0f, 0.0f, 0.0f};
    
    TINA_CORE_LOG_INFO("Created UIView '{}' with zOrder {}", name, getZOrder());

    initShaders();
    initRenderer();
    initCamera();
    TINA_CORE_LOG_DEBUG("Created UIView '{}'", name);
}

UIView::~UIView() {
    if (bgfx::isValid(m_shaderProgram)) {
        bgfx::destroy(m_shaderProgram);
    }
    m_renderer.reset();
    TINA_CORE_LOG_DEBUG("Destroyed UIView '{}'", getName());
}

void UIView::onAttach() {
    if (!m_initialized) {
        try {
            TINA_CORE_LOG_INFO("Initializing UIView");

            initShaders();
            initRenderer();
            initCamera();

            // 设置view参数
            if (m_renderer) {
                m_renderer->setViewId(renderState.viewId);

                uint32_t width, height;
                Core::Engine::get().getWindowSize(width, height);

                // 更新视口
                setViewPort(Rect(0, 0, width, height));
                
                // 设置BatchRenderer的视口
                m_renderer->setViewRect(0, 0, width, height);
                
                // 设置清除标志和颜色 - 对UI使用NONE,避免清除下层内容
                m_renderer->setViewClear(BGFX_CLEAR_NONE);
                
                TINA_CORE_LOG_DEBUG("View parameters set - ViewId: {}, Size: {}x{}, ZOrder: {}",
                    renderState.viewId, width, height, getZOrder());
            }

            m_initialized = true;
            TINA_CORE_LOG_INFO("UIView initialized successfully");
        }
        catch (const std::exception& e) {
            TINA_CORE_LOG_ERROR("Failed to initialize UIView: {}", e.what());
            if (bgfx::isValid(m_shaderProgram)) {
                m_shaderProgram = BGFX_INVALID_HANDLE;
            }
            throw;
        }
    }
    TINA_CORE_LOG_DEBUG("UIView '{}' attached", getName());
}

void UIView::onDetach() {
    TINA_CORE_LOG_INFO("Shutting down UIView");

    try {
        if (m_renderer) {
            m_renderer->shutdown();
        }

        if (bgfx::isValid(m_shaderProgram)) {
            TINA_CORE_LOG_DEBUG("Destroying shader program");
            Core::Engine::get().getShaderManager().destroyProgram(m_shaderProgram);
            m_shaderProgram = BGFX_INVALID_HANDLE;
        }

        m_initialized = false;
        TINA_CORE_LOG_INFO("UIView shutdown completed");
    }
    catch (const std::exception& e) {
        TINA_CORE_LOG_ERROR("Error during UIView shutdown: {}", e.what());
        if (bgfx::isValid(m_shaderProgram)) {
            Core::Engine::get().getShaderManager().destroyProgram(m_shaderProgram);
            m_shaderProgram = BGFX_INVALID_HANDLE;
        }
    }
    TINA_CORE_LOG_DEBUG("UIView '{}' detached", getName());
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
        m_renderer->begin();
        m_renderer->setViewTransform(m_camera->getViewMatrix(), m_camera->getProjectionMatrix());
    }
}

void UIView::endRender() {
    if (m_initialized && m_renderer) {
        m_renderer->end();
    }
    View::endRender();
}

void UIView::render(Scene* scene) {
    if (!isVisible() || !m_initialized || !m_renderer) return;

    TINA_CORE_LOG_DEBUG("UIView starting render with {} draw commands", m_drawCommands.size());

    // 开始渲染
    beginRender();

    // 设置渲染状态
    if (m_renderer) {
        // 设置渲染状态,确保UI正确显示
        uint32_t state = BGFX_STATE_WRITE_RGB 
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_MSAA
            | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
            
        bgfx::setState(state);
        
        // 禁用深度测试,确保UI总是显示在最上层
        m_renderer->setViewClear(BGFX_CLEAR_NONE);
    }

    // 绘制背景
    drawBackground();

    // 执行所有绘制命令
    for (const auto& cmd : m_drawCommands) {
        cmd();
    }

    // 结束渲染
    endRender();

    TINA_CORE_LOG_DEBUG("UIView finished rendering {} draw commands", m_drawCommands.size());
}

bool UIView::onEvent(Event& event) {
    bool handled = false;

    if (event.type == Event::Type::WindowResize) {
        uint32_t width = event.windowResize.width;
        uint32_t height = event.windowResize.height;

        if (m_camera) {
            m_camera->setProjection(
                0.0f, static_cast<float>(width),
                static_cast<float>(height), 0.0f
            );
        }

        setViewPort(Rect(0, 0, width, height));
        
        if (m_renderer) {
            m_renderer->setViewRect(0, 0, width, height);
        }
        
        // 清除现有的绘制命令
        m_drawCommands.clear();
        
        // 重新创建UI十字架
        float centerX = width * 0.5f;
        float centerY = height * 0.5f;
        
        LineStyle style;
        style.thickness = 5.0f;
        style.color = {1.0f, 0.0f, 0.0f, 1.0f};
        
        // 重新绘制垂直线和水平线
        drawLine({centerX, 0.0f}, {centerX, (float)height}, style);
        drawLine({0.0f, centerY}, {(float)width, centerY}, style);
        
        TINA_CORE_LOG_INFO("UI cross updated for window size {}x{}", width, height);
        
        handled = true;
    }

    if (!handled) {
        handled = View::onEvent(event);
    }

    return handled;
}

void UIView::setBackgroundColor(const UIColor& color) {
    m_backgroundColor = color;
    
    if (m_renderer) {
        uint32_t bgColor = 
            (static_cast<uint32_t>(color.r * 255) << 24) |
            (static_cast<uint32_t>(color.g * 255) << 16) |
            (static_cast<uint32_t>(color.b * 255) << 8) |
            static_cast<uint32_t>(color.a * 255);
            
        m_renderer->setViewClear(
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
            bgColor, 1.0f, 0
        );
    }
}

void UIView::drawBackground() {
    if (m_backgroundColor.a > 0.0f) {
        RectStyle style;
        style.fillColor = m_backgroundColor;
        style.filled = true;
        style.borderThickness = 0.0f;
        drawRect(getBounds(), style);
    }
}

void UIView::drawLine(const glm::vec2& start, const glm::vec2& end, const LineStyle& style) {
    if (!m_renderer) return;
    
    TINA_CORE_LOG_DEBUG("Adding line draw command from ({}, {}) to ({}, {}) with thickness {}",
        start.x, start.y, end.x, end.y, style.thickness);
    
    m_drawCommands.push_back([this, start, end, style]() {
        // 计算线段的方向和长度
        glm::vec2 direction = end - start;
        float length = glm::length(direction);
        
        if (length < 0.0001f) return; // 防止除零
        
        // 归一化方向
        direction /= length;
        
        // 计算垂直于线段的向量
        glm::vec2 normal(-direction.y, direction.x);
        
        // 计算线条的实际尺寸
        glm::vec2 size;
        if (std::abs(direction.x) > std::abs(direction.y)) {
            // 水平线
            size = glm::vec2(length, style.thickness);
        } else {
            // 垂直线
            size = glm::vec2(style.thickness, length);
        }
        
        // 使用正确的位置和大小绘制quad
        m_renderer->drawQuad(
            start,
            size,
            Color(style.color.r, style.color.g, style.color.b, style.color.a)
        );
        
        TINA_CORE_LOG_DEBUG("Drawing line as quad at ({}, {}) with size ({}, {})",
            start.x, start.y, size.x, size.y);
    });
}

void UIView::drawRect(const Rect& rect, const RectStyle& style) {
    if (!m_renderer) return;
    
    m_drawCommands.push_back([this, rect, style]() {
        // 绘制填充
        if (style.filled) {
            m_renderer->drawQuad(
                rect.getPosition(),
                rect.getSize(),
                Color(style.fillColor.r, style.fillColor.g, style.fillColor.b, style.fillColor.a)
            );
        }
        
        // 绘制边框
        if (style.borderThickness > 0.0f) {
            LineStyle borderStyle{style.borderColor, style.borderThickness};
            
            // 上边
            drawLine(
                rect.getPosition(),
                glm::vec2(rect.getRight(), rect.getTop()),
                borderStyle
            );
            
            // 右边
            drawLine(
                glm::vec2(rect.getRight(), rect.getTop()),
                glm::vec2(rect.getRight(), rect.getBottom()),
                borderStyle
            );
            
            // 下边
            drawLine(
                glm::vec2(rect.getRight(), rect.getBottom()),
                glm::vec2(rect.getLeft(), rect.getBottom()),
                borderStyle
            );
            
            // 左边
            drawLine(
                glm::vec2(rect.getLeft(), rect.getBottom()),
                rect.getPosition(),
                borderStyle
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
            LineStyle borderStyle{style.borderColor, style.borderThickness};
            
            for (int i = 0; i < segments; ++i) {
                int nextIndex = (i + 1) % segments;
                drawLine(points[i], points[nextIndex], borderStyle);
            }
        }
    });
}

void UIView::drawTexture(const Rect& rect, const std::shared_ptr<Texture2D>& texture, const Color& tint) {
    if (!m_renderer || !texture) return;
    
    m_drawCommands.push_back([this, rect, texture, tint]() {
        m_renderer->drawTexturedQuad(
            rect.getPosition(),
            rect.getSize(),
            texture,
            glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
            tint
        );
    });
}

void UIView::initShaders() {
    try {
        m_shaderProgram = Core::Engine::get().getShaderManager().createProgram("2d");
        if (!bgfx::isValid(m_shaderProgram)) {
            throw std::runtime_error("Failed to create 2D shader program");
        }
        TINA_CORE_LOG_INFO("Successfully loaded 2D shaders");
    }
    catch (const std::exception& e) {
        TINA_CORE_LOG_ERROR("Failed to initialize shaders: {}", e.what());
        throw;
    }
}

void UIView::initRenderer() {
    m_renderer = std::make_unique<BatchRenderer2D>();
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

    TINA_CORE_LOG_INFO("Camera initialized with screen space projection {}x{}", width, height);
}

bool UIView::handleMouseMove(Event& event) {
    if (event.type != Event::MouseMove) return false;
    
    // 转换到本地坐标
    glm::vec2 localPos = globalToLocal({
        static_cast<float>(event.mousePos.x), 
        static_cast<float>(event.mousePos.y)
    });
    
    // 检查鼠标是否在视图范围内
    if (bounds.contains(localPos)) {
        TINA_CORE_LOG_DEBUG("Mouse moved in UIView '{}' at local position ({}, {})",
            getName(), localPos.x, localPos.y);
        return true;
    }
    return false;
}

bool UIView::handleMouseButton(Event& event) {
    if (event.type != Event::MouseButtonEvent) return false;
    
    // 转换到本地坐标
    glm::vec2 localPos = globalToLocal({
        static_cast<float>(event.mouseButton.x),
        static_cast<float>(event.mouseButton.y)
    });
    
    // 检查鼠标是否在视图范围内
    if (bounds.contains(localPos)) {
        TINA_CORE_LOG_DEBUG("Mouse button {} {} in UIView '{}' at local position ({}, {})",
            static_cast<int>(event.mouseButton.button),
            event.mouseButton.action == GLFW_PRESS ? "pressed" : "released",
            getName(),
            localPos.x, localPos.y);
        return true;
    }
    return false;
}

bool UIView::handleWindowResize(Event& event) {
    if (event.type != Event::WindowResize) return false;
    
    // 更新视图边界
    bounds.setWidth(static_cast<float>(event.windowResize.width));
    bounds.setHeight(static_cast<float>(event.windowResize.height));
    
    TINA_CORE_LOG_DEBUG("UIView '{}' resized to {}x{}", 
        getName(),
        event.windowResize.width,
        event.windowResize.height);
    
    return true;
}

} // namespace Tina 