#ifndef TINA_CORE_BGFX_RENDERER_HPP
#define TINA_CORE_BGFX_RENDERER_HPP

#include "core/IRenderer.hpp"
#include "graphics/IndexBuffer.hpp"
#include "graphics/VertexBuffer.hpp"
#include "core/Shader.hpp"
#include "tool/BgfxUtils.hpp"
#include "resource/ResourceManager.hpp"
#include "window/IWindow.hpp"

namespace Tina {

    class BgfxRenderer : public IRenderer{
    public:
        explicit BgfxRenderer(IWindow* window);
        ~BgfxRenderer() override;

        void init(Vector2i resolution) override;

        void shutdown() override;

        void render() override;

        void frame() override;

        void resize(Vector2i resolution) override;

        [[nodiscard]] Vector2i getResolution() const override;

        void setTexture(uint8_t stage, const ShaderUniform &uniform, const ResourceHandle &textureHandle) override;

        void submit(uint8_t viewId, const ResourceHandle &shaderHandle) override;

    private:
         struct PosNormalTangentTexcoordVertex {
            // 顶点坐标
            float m_x;
            float m_y;
            float m_z;
            // 纹理坐标
            int16_t m_u;
            int16_t m_v;
        };

        PosNormalTangentTexcoordVertex s_cubeVertices[24] = {
                {-1.0f, 1.0f, 1.0f, 0, 0},
                {1.0f, 1.0f, 1.0f, 0x7fff, 0},
                {-1.0f, -1.0f, 1.0f, 0, 0x7fff},
                {1.0f, -1.0f, 1.0f, 0x7fff, 0x7fff},
                {-1.0f, 1.0f, -1.0f, 0, 0},
                {1.0f, 1.0f, -1.0f, 0x7fff, 0},
                {-1.0f, -1.0f, -1.0f, 0, 0x7fff},
                {1.0f, -1.0f, -1.0f, 0x7fff, 0x7fff},
                {-1.0f, 1.0f, 1.0f, 0, 0},
                {1.0f, 1.0f, 1.0f, 0x7fff, 0},
                {-1.0f, 1.0f, -1.0f, 0, 0x7fff},
                {1.0f, 1.0f, -1.0f, 0x7fff, 0x7fff},
                {-1.0f, -1.0f, 1.0f, 0, 0},
                {1.0f, -1.0f, 1.0f, 0x7fff, 0},
                {-1.0f, -1.0f, -1.0f, 0, 0x7fff},
                {1.0f, -1.0f, -1.0f, 0x7fff, 0x7fff},
                {1.0f, -1.0f, 1.0f, 0, 0},
                {1.0f, 1.0f, 1.0f, 0x7fff, 0},
                {1.0f, -1.0f, -1.0f, 0, 0x7fff},
                {1.0f, 1.0f, -1.0f, 0x7fff, 0x7fff},
                {-1.0f, -1.0f, 1.0f, 0, 0},
                {-1.0f, 1.0f, 1.0f, 0x7fff, 0},
                {-1.0f, -1.0f, -1.0f, 0, 0x7fff},
                {-1.0f, 1.0f, -1.0f, 0x7fff, 0x7fff},
        };

        uint16_t s_cubeIndices[36] =
                {
                        0, 2, 1,
                        1, 2, 3,
                        4, 5, 6,
                        5, 7, 6,

                        8, 10, 9,
                        9, 10, 11,
                        12, 13, 14,
                        13, 15, 14,

                        16, 18, 17,
                        17, 18, 19,
                        20, 21, 22,
                        21, 23, 22,
                };
        IWindow* m_window; // 保存窗口指针
        VertexBuffer m_vbh;
        IndexBuffer m_ibh;
        // Shader m_shader;
        // bgfx::UniformHandle s_texColor;
        // bgfx::TextureHandle m_textureColor;
        RefPtr<ResourceManager> m_resourceManager;

        Vector2i m_resolution;
    };
    
}

#endif
