//
// Created by wuxianggujun on 2025/2/14.
//

#pragma once

#include "BatchRenderer2D.hpp"
#include "Color.hpp"
#include "tina/core/Core.hpp"
#include "tina/core/OrthographicCamera.hpp"
#include "tina/math/Vec.hpp"

namespace Tina
{
    class Texture2D;

    class TINA_CORE_API Renderer2D
    {
    public:
        Renderer2D();
        ~Renderer2D();

        void init(bgfx::ProgramHandle shader);
        void shutdown();

        void beginScene(const SharedPtr<OrthographicCamera>& camera);
        void endScene();

        void drawQuad(const glm::vec2& position, const glm::vec2& size, const Color& color);
        void drawSprite(const glm::vec2& position, const glm::vec2& size, const SharedPtr<Texture2D>& texture,
                        const glm::vec4& texCoords = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                        const Color& color = Color::White);
        
        void drawQuads(const std::vector<BatchRenderer2D::InstanceData>& instances);

        [[nodiscard]] BatchRenderer2D* getBatchRenderer() const { return m_BatchRenderer.get(); }

    private:
        UniquePtr<BatchRenderer2D> m_BatchRenderer;
        SharedPtr<OrthographicCamera> m_Camera;
        bool m_sceneInProgress = false;
    };
} // Tina
