#include "tina/renderer/Renderer2D.hpp"
#include "tina/renderer/ShaderManager.hpp"
#include "tina/log/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "bx/math.h"
#include "tina/core/Engine.hpp"
#include "tina/renderer/Color.hpp"

namespace Tina
{
    bgfx::VertexLayout Vertex2D::layout;
    Renderer2D::Renderer2DData Renderer2D::s_Data;

    void Vertex2D::init()
    {
        try
        {
            TINA_LOG_DEBUG("Initializing Vertex2D layout");
            layout
                .begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true, false)  // RGBA format, normalized
                .end();

            if (!layout.getStride())
            {
                throw std::runtime_error("Failed to initialize vertex layout");
            }

            TINA_LOG_DEBUG("Vertex2D layout initialized successfully with stride: {}", layout.getStride());
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Failed to initialize Vertex2D layout: {}", e.what());
            throw;
        }
    }

    void Renderer2D::init(bgfx::ProgramHandle shaderProgram)
    {
        TINA_LOG_INFO("Initializing Renderer2D");

        if (s_Data.isInitialized)
        {
            TINA_LOG_WARN("Renderer2D already initialized");
            return;
        }

        try
        {
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
            for (uint32_t i = 0; i < s_Data.MaxIndices; i += 6)
            {
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

            if (!bgfx::isValid(s_Data.indexBuffer))
            {
                throw std::runtime_error("Failed to create index buffer");
            }

            // 创建BGFX顶点缓冲
            TINA_LOG_DEBUG("Creating BGFX vertex buffer");
            s_Data.vertexBuffer = bgfx::createDynamicVertexBuffer(
                s_Data.MaxVertices,
                Vertex2D::layout,
                BGFX_BUFFER_ALLOW_RESIZE
            );

            if (!bgfx::isValid(s_Data.vertexBuffer))
            {
                throw std::runtime_error("Failed to create vertex buffer");
            }

            // 创建uniforms
            TINA_LOG_DEBUG("Creating uniforms");
            s_Data.shader = shaderProgram;

            TINA_LOG_DEBUG("Creating texture sampler uniform");
            s_Data.s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
            s_Data.u_useTexture = bgfx::createUniform("u_useTexture", bgfx::UniformType::Vec4);

            if (!bgfx::isValid(s_Data.s_texColor) || !bgfx::isValid(s_Data.u_useTexture))
            {
                throw std::runtime_error("Failed to create uniforms");
            }

            s_Data.isInitialized = true;
            TINA_LOG_INFO("Renderer2D initialized successfully");
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Failed to initialize Renderer2D: {}", e.what());
            shutdown();
            throw;
        }
    }

    void Renderer2D::shutdown()
    {
        if (!s_Data.isInitialized)
        {
            return;
        }

        TINA_LOG_INFO("Shutting down Renderer2D");

        try
        {
            // 一次性销毁所有BGFX资源
            if (bgfx::isValid(s_Data.vertexBuffer))
            {
                bgfx::destroy(s_Data.vertexBuffer);
            }
            if (bgfx::isValid(s_Data.indexBuffer))
            {
                bgfx::destroy(s_Data.indexBuffer);
            }
            if (bgfx::isValid(s_Data.s_texColor))
            {
                bgfx::destroy(s_Data.s_texColor);
            }
            if (bgfx::isValid(s_Data.u_useTexture))
            {
                bgfx::destroy(s_Data.u_useTexture);
            }

            // 释放内存
            delete[] s_Data.vertexBufferBase;
            delete[] s_Data.indices;

            // 重置所有句柄和指针
            s_Data = Renderer2DData();

            TINA_LOG_INFO("Renderer2D shutdown complete");
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error during Renderer2D shutdown: {}", e.what());
            // 确保标记为未初始化，防止重复调用
            s_Data.isInitialized = false;
        }
    }

    void Renderer2D::beginScene(const glm::mat4& viewProj)
    {
        if (!s_Data.isInitialized)
        {
            TINA_LOG_ERROR("Renderer2D not initialized");
            return;
        }

        TINA_LOG_DEBUG("Beginning scene with view matrix");

        // 设置变换矩阵
        bgfx::setViewTransform(0, nullptr, glm::value_ptr(viewProj));

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

    void Renderer2D::endScene()
    {
        if (!s_Data.isInitialized)
        {
            return;
        }

        flush();
    }

    void Renderer2D::drawRect(const glm::vec2& position, const glm::vec2& size, const Color& color)
    {
        if (!s_Data.isInitialized)
        {
            return;
        }

        // 如果当前有纹理状态,需要先flush
        if (bgfx::isValid(s_Data.currentTexture))
        {
            flush();
        }

        if (s_Data.quadCount >= s_Data.MaxQuads)
        {
            flush();
        }

        float x = position.x;
        float y = position.y;
        float width = size.x;
        float height = size.y;
        float z = 0.0f;

        uint32_t packedColor = static_cast<uint32_t>(color);
        TINA_LOG_DEBUG("Packed color: 0x{:08x}", packedColor);

        // 添加四个顶点，按照逆时针顺序
        s_Data.vertexBufferPtr->x = x;
        s_Data.vertexBufferPtr->y = y;
        s_Data.vertexBufferPtr->z = z;
        s_Data.vertexBufferPtr->u = 0.0f;
        s_Data.vertexBufferPtr->v = 0.0f;
        s_Data.vertexBufferPtr->color = packedColor;
        s_Data.vertexBufferPtr++;

        s_Data.vertexBufferPtr->x = x + width;
        s_Data.vertexBufferPtr->y = y;
        s_Data.vertexBufferPtr->z = z;
        s_Data.vertexBufferPtr->u = 1.0f;
        s_Data.vertexBufferPtr->v = 0.0f;
        s_Data.vertexBufferPtr->color = packedColor;
        s_Data.vertexBufferPtr++;

        s_Data.vertexBufferPtr->x = x + width;
        s_Data.vertexBufferPtr->y = y + height;
        s_Data.vertexBufferPtr->z = z;
        s_Data.vertexBufferPtr->u = 1.0f;
        s_Data.vertexBufferPtr->v = 1.0f;
        s_Data.vertexBufferPtr->color = packedColor;
        s_Data.vertexBufferPtr++;

        s_Data.vertexBufferPtr->x = x;
        s_Data.vertexBufferPtr->y = y + height;
        s_Data.vertexBufferPtr->z = z;
        s_Data.vertexBufferPtr->u = 0.0f;
        s_Data.vertexBufferPtr->v = 1.0f;
        s_Data.vertexBufferPtr->color = packedColor;
        s_Data.vertexBufferPtr++;

        s_Data.quadCount++;
    }

    void Renderer2D::drawTexturedRect(const glm::vec2& position, const glm::vec2& size, bgfx::TextureHandle texture, const Color& color)
    {
        if (!s_Data.isInitialized)
        {
            TINA_LOG_ERROR("Renderer2D not initialized");
            return;
        }

        if (!bgfx::isValid(texture))
        {
            TINA_LOG_ERROR("Invalid texture handle");
            return;
        }

        // 如果当前batch不为空或纹理改变,需要先flush
        if (s_Data.quadCount > 0)
        {
            flush();
        }

        // 设置当前纹理
        s_Data.currentTexture = texture;
        TINA_LOG_DEBUG("Setting texture handle: {} for drawing", texture.idx);

        float x = position.x;
        float y = position.y;
        float width = size.x;
        float height = size.y;
        float z = 0.0f;

        uint32_t packedColor = static_cast<uint32_t>(color);
        TINA_LOG_DEBUG("Packed color for texture: 0x{:08x}", packedColor);

        // 添加四个顶点，按照逆时针顺序
        s_Data.vertexBufferPtr->x = x;
        s_Data.vertexBufferPtr->y = y;
        s_Data.vertexBufferPtr->z = z;
        s_Data.vertexBufferPtr->u = 0.0f;
        s_Data.vertexBufferPtr->v = 0.0f;
        s_Data.vertexBufferPtr->color = packedColor;
        s_Data.vertexBufferPtr++;

        s_Data.vertexBufferPtr->x = x + width;
        s_Data.vertexBufferPtr->y = y;
        s_Data.vertexBufferPtr->z = z;
        s_Data.vertexBufferPtr->u = 1.0f;
        s_Data.vertexBufferPtr->v = 0.0f;
        s_Data.vertexBufferPtr->color = packedColor;
        s_Data.vertexBufferPtr++;

        s_Data.vertexBufferPtr->x = x + width;
        s_Data.vertexBufferPtr->y = y + height;
        s_Data.vertexBufferPtr->z = z;
        s_Data.vertexBufferPtr->u = 1.0f;
        s_Data.vertexBufferPtr->v = 1.0f;
        s_Data.vertexBufferPtr->color = packedColor;
        s_Data.vertexBufferPtr++;

        s_Data.vertexBufferPtr->x = x;
        s_Data.vertexBufferPtr->y = y + height;
        s_Data.vertexBufferPtr->z = z;
        s_Data.vertexBufferPtr->u = 0.0f;
        s_Data.vertexBufferPtr->v = 1.0f;
        s_Data.vertexBufferPtr->color = packedColor;
        s_Data.vertexBufferPtr++;

        s_Data.quadCount++;
        
        // 立即提交纹理绘制
        flush();
    }

    void Renderer2D::flush()
    {
        if (!s_Data.isInitialized || s_Data.quadCount == 0)
        {
            return;
        }

        try
        {
            // 计算实际使用的顶点数
            uint32_t vertexCount = s_Data.quadCount * 4;
            uint32_t indexCount = s_Data.quadCount * 6;

            // 更新顶点缓冲
            uint32_t dataSize = vertexCount * sizeof(Vertex2D);
            if (dataSize > 0)
            {
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
                    | BGFX_STATE_BLEND_ALPHA;  // 使用预定义的alpha混合模式

                bgfx::setState(state);

                // 设置纹理状态
                if (bgfx::isValid(s_Data.currentTexture))
                {
                    TINA_LOG_DEBUG("Setting texture state: enabled with handle: {}", s_Data.currentTexture.idx);
                    bgfx::setTexture(0, s_Data.s_texColor, s_Data.currentTexture);
                    float useTexture[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
                    bgfx::setUniform(s_Data.u_useTexture, useTexture);
                }
                else
                {
                    TINA_LOG_DEBUG("Setting texture state: disabled (solid color rendering)");
                    float noTexture[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    bgfx::setUniform(s_Data.u_useTexture, noTexture);
                }

                // 设置缓冲
                bgfx::setVertexBuffer(0, s_Data.vertexBuffer, 0, vertexCount);
                bgfx::setIndexBuffer(s_Data.indexBuffer, 0, indexCount);

                // 提交渲染
                bgfx::submit(0, s_Data.shader);

                TINA_LOG_DEBUG("Submitted render with {} quads (texture: {})", 
                    s_Data.quadCount, 
                    bgfx::isValid(s_Data.currentTexture) ? "yes" : "no");
            }
        }
        catch (const std::exception& e)
        {
            TINA_LOG_ERROR("Error during rendering flush: {}", e.what());
        }

        // 重置状态
        s_Data.quadCount = 0;
        s_Data.vertexBufferPtr = s_Data.vertexBufferBase;
        s_Data.currentTexture = BGFX_INVALID_HANDLE;
    }
}
