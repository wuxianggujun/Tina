#include "tina/renderer/Renderer2D.hpp"
#include "tina/renderer/ShaderManager.hpp"
#include "tina/log/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "bx/math.h"
#include "tina/core/Engine.hpp"

namespace Tina {

bgfx::VertexLayout Vertex2D::layout;
Renderer2D::Renderer2DData Renderer2D::s_Data;

void Renderer2D::init(bgfx::ProgramHandle shaderProgram) {
    TINA_LOG_INFO("Initializing Renderer2D");

    if (s_Data.isInitialized) {
        TINA_LOG_WARN("Renderer2D already initialized");
        return;
    }

    try {
        // 初始化顶点布局
        TINA_LOG_DEBUG("Initializing vertex layout");
        Vertex2D::init();

        // 创建顶点缓冲
        TINA_LOG_DEBUG("Creating vertex buffer");
        s_Data.vertexBufferBase = new Vertex2D[s_Data.MaxVertices];
        s_Data.vertexBufferPtr = s_Data.vertexBufferBase;

        // 创建索引缓冲
        TINA_LOG_DEBUG("Creating index buffer");
        s_Data.indices = new uint16_t[s_Data.MaxIndices];

        uint32_t offset = 0;
        for (uint32_t i = 0; i < s_Data.MaxIndices; i += 6) {
            s_Data.indices[i + 0] = offset + 0;
            s_Data.indices[i + 1] = offset + 1;
            s_Data.indices[i + 2] = offset + 2;
            s_Data.indices[i + 3] = offset + 2;
            s_Data.indices[i + 4] = offset + 3;
            s_Data.indices[i + 5] = offset + 0;

            offset += 4;
        }

        // 创建BGFX索引缓冲
        TINA_LOG_DEBUG("Creating BGFX index buffer");
        s_Data.indexBuffer = bgfx::createIndexBuffer(
            bgfx::makeRef(s_Data.indices, s_Data.MaxIndices * sizeof(uint16_t))
        );

        if (!bgfx::isValid(s_Data.indexBuffer)) {
            throw std::runtime_error("Failed to create index buffer");
        }

        // 创建BGFX顶点缓冲
        TINA_LOG_DEBUG("Creating BGFX vertex buffer");
        s_Data.vertexBuffer = bgfx::createDynamicVertexBuffer(
            s_Data.MaxVertices,
            Vertex2D::layout,
            BGFX_BUFFER_ALLOW_RESIZE
        );

        if (!bgfx::isValid(s_Data.vertexBuffer)) {
            throw std::runtime_error("Failed to create vertex buffer");
        }

        // 创建uniforms
        TINA_LOG_DEBUG("Creating uniforms");
        s_Data.shader = shaderProgram;

        TINA_LOG_DEBUG("Creating texture sampler uniform");
        s_Data.s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

        if (!bgfx::isValid(s_Data.s_texColor)) {
            throw std::runtime_error("Failed to create texture sampler uniform");
        }

        s_Data.isInitialized = true;
        TINA_LOG_INFO("Renderer2D initialized successfully");

    } catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to initialize Renderer2D: {}", e.what());
        shutdown();
        throw;
    }
}

void Renderer2D::shutdown() {
    if (!s_Data.isInitialized) {
        return;
    }

    TINA_LOG_INFO("Shutting down Renderer2D");

    try {
        // 确保所有渲染命令都已经提交
        flush();

        // 销毁BGFX资源
        if (bgfx::isValid(s_Data.vertexBuffer)) {
            TINA_LOG_DEBUG("Destroying vertex buffer");
            bgfx::destroy(s_Data.vertexBuffer);
            s_Data.vertexBuffer = BGFX_INVALID_HANDLE;
        }

        if (bgfx::isValid(s_Data.indexBuffer)) {
            TINA_LOG_DEBUG("Destroying index buffer");
            bgfx::destroy(s_Data.indexBuffer);
            s_Data.indexBuffer = BGFX_INVALID_HANDLE;
        }

        if (bgfx::isValid(s_Data.s_texColor)) {
            TINA_LOG_DEBUG("Destroying texture sampler uniform");
            bgfx::destroy(s_Data.s_texColor);
            s_Data.s_texColor = BGFX_INVALID_HANDLE;
        }

        // 释放内存
        if (s_Data.vertexBufferBase) {
            TINA_LOG_DEBUG("Freeing vertex buffer memory");
            delete[] s_Data.vertexBufferBase;
            s_Data.vertexBufferBase = nullptr;
        }

        if (s_Data.indices) {
            TINA_LOG_DEBUG("Freeing index buffer memory");
            delete[] s_Data.indices;
            s_Data.indices = nullptr;
        }

        s_Data.vertexBufferPtr = nullptr;
        s_Data.quadCount = 0;
        s_Data.isInitialized = false;

        TINA_LOG_INFO("Renderer2D shutdown complete");
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Error during Renderer2D shutdown: {}", e.what());
        // 即使发生错误也标记为未初始化，防止重复调用
        s_Data.isInitialized = false;
        throw;
    }
}

void Renderer2D::beginScene(const glm::mat4& viewProj) {
    if (!s_Data.isInitialized) {
        TINA_LOG_ERROR("Renderer2D not initialized");
        return;
    }

    TINA_LOG_DEBUG("Beginning scene with view matrix");

    // 获取当前窗口尺寸
    uint32_t width, height;
    Core::Engine::get().getWindowSize(width, height);

    // 设置视图矩阵和视口
    bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));

    // 设置正交投影矩阵
    float orthoProjection[16];
    bx::mtxOrtho(orthoProjection, 
        0.0f, float(width),    // left, right
        float(height), 0.0f,   // bottom, top (反转Y轴)
        0.0f, 1000.0f,        // near, far
        0.0f,                 // offset
        bgfx::getCaps()->homogeneousDepth);

    // 设置视图矩阵（默认为单位矩阵）
    float view[16];
    bx::mtxIdentity(view);

    // 设置变换矩阵
    bgfx::setViewTransform(0, view, orthoProjection);

    // 设置视图清除状态
    bgfx::setViewClear(0,
        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
        0x303030ff, // 深灰色背景
        1.0f,
        0);

    // 确保视图被更新
    bgfx::touch(0);

    // 重置渲染状态
    s_Data.quadCount = 0;
    s_Data.vertexBufferPtr = s_Data.vertexBufferBase;
    
    TINA_LOG_DEBUG("Scene setup complete");
}

void Renderer2D::endScene() {
    if (!s_Data.isInitialized) {
        return;
    }

    flush();
}

void Renderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, uint32_t color) {
    if (!s_Data.isInitialized) {
        TINA_LOG_ERROR("Renderer2D not initialized");
        return;
    }

    if (s_Data.quadCount >= s_Data.MaxQuads) {
        flush();
    }

    // 直接使用屏幕坐标
    float x = position.x;
    float y = position.y;
    float width = size.x;
    float height = size.y;
    float z = 0.0f;  // 使用z坐标进行深度排序

    // 添加四个顶点，按照逆时针顺序
    s_Data.vertexBufferPtr->x = x;
    s_Data.vertexBufferPtr->y = y;
    s_Data.vertexBufferPtr->z = z;
    s_Data.vertexBufferPtr->u = 0.0f;
    s_Data.vertexBufferPtr->v = 0.0f;
    s_Data.vertexBufferPtr->color = color;
    s_Data.vertexBufferPtr++;

    s_Data.vertexBufferPtr->x = x + width;
    s_Data.vertexBufferPtr->y = y;
    s_Data.vertexBufferPtr->z = z;
    s_Data.vertexBufferPtr->u = 1.0f;
    s_Data.vertexBufferPtr->v = 0.0f;
    s_Data.vertexBufferPtr->color = color;
    s_Data.vertexBufferPtr++;

    s_Data.vertexBufferPtr->x = x + width;
    s_Data.vertexBufferPtr->y = y + height;
    s_Data.vertexBufferPtr->z = z;
    s_Data.vertexBufferPtr->u = 1.0f;
    s_Data.vertexBufferPtr->v = 1.0f;
    s_Data.vertexBufferPtr->color = color;
    s_Data.vertexBufferPtr++;

    s_Data.vertexBufferPtr->x = x;
    s_Data.vertexBufferPtr->y = y + height;
    s_Data.vertexBufferPtr->z = z;
    s_Data.vertexBufferPtr->u = 0.0f;
    s_Data.vertexBufferPtr->v = 1.0f;
    s_Data.vertexBufferPtr->color = color;
    s_Data.vertexBufferPtr++;

    s_Data.quadCount++;

    TINA_LOG_DEBUG("Drew quad at ({}, {}) with size ({}, {}) and color 0x{:08x}", 
        x, y, width, height, color);
}

void Renderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color) {
    uint32_t packed = ((uint8_t)(color.a * 255.0f) << 24) |
                     ((uint8_t)(color.b * 255.0f) << 16) |
                     ((uint8_t)(color.g * 255.0f) << 8) |
                     ((uint8_t)(color.r * 255.0f));
    drawQuad(position, size, packed);
}

void Renderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, bgfx::TextureHandle texture, uint32_t color) {
    if (!s_Data.isInitialized) {
        TINA_LOG_ERROR("Renderer2D not initialized");
        return;
    }

    drawQuad(position, size, color);
    bgfx::setTexture(0, s_Data.s_texColor, texture);
}

void Renderer2D::flush() {
    if (!s_Data.isInitialized || s_Data.quadCount == 0) {
        return;
    }

    try {
        // 计算实际使用的顶点数
        uint32_t vertexCount = s_Data.quadCount * 4;
        uint32_t indexCount = s_Data.quadCount * 6;
        
        // 更新顶点缓冲
        uint32_t dataSize = vertexCount * sizeof(Vertex2D);
        if (dataSize > 0) {
            TINA_LOG_DEBUG("Flushing {} vertices", vertexCount);
            
            // 更新顶点缓冲区
            bgfx::update(s_Data.vertexBuffer, 0, bgfx::makeRef(s_Data.vertexBufferBase, dataSize));

            // 设置渲染状态
            uint64_t state = 0
                | BGFX_STATE_WRITE_RGB
                | BGFX_STATE_WRITE_A
                | BGFX_STATE_DEPTH_TEST_LESS
                | BGFX_STATE_WRITE_Z
                | BGFX_STATE_MSAA
                | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
            
            bgfx::setState(state);

            // 设置缓冲
            bgfx::setVertexBuffer(0, s_Data.vertexBuffer, 0, vertexCount);
            bgfx::setIndexBuffer(s_Data.indexBuffer, 0, indexCount);

            // 提交渲染
            bgfx::submit(0, s_Data.shader);
            
            TINA_LOG_DEBUG("Submitted render with {} quads", s_Data.quadCount);
        }

    } catch (const std::exception& e) {
        TINA_LOG_ERROR("Error during rendering flush: {}", e.what());
    }

    // 重置状态
    s_Data.quadCount = 0;
    s_Data.vertexBufferPtr = s_Data.vertexBufferBase;
}

}
