//
// Created by wuxianggujun on 2025/2/24.
//

#pragma once
#include "tina/core/Core.hpp"
#include "tina/core/Component.hpp"
#include "tina/resource/TextureResource.hpp"
#include <bgfx/bgfx.h>

namespace Tina
{
    class TINA_CORE_API SpriteRenderer : public Component
    {
    public:
        SpriteRenderer();
        ~SpriteRenderer() override;

        void setTexture(RefPtr<TextureResource> texture);
        void render() override;

    private:
        RefPtr<TextureResource> m_texture;
        bgfx::VertexBufferHandle m_vbh{BGFX_INVALID_HANDLE};
        bgfx::IndexBufferHandle m_ibh{BGFX_INVALID_HANDLE};
        bgfx::UniformHandle m_textureSampler{BGFX_INVALID_HANDLE};
    };
} // Tina
