//
// Created by wuxianggujun on 2025/2/14.
//

#include "tina/renderer/Renderer2D.hpp"

#include "tina/log/Logger.hpp"

namespace Tina
{
    Renderer2D::Renderer2D() = default;
    Renderer2D::~Renderer2D() = default;

    void Renderer2D::init(const bgfx::ProgramHandle shader)
    {
        m_BatchRenderer = MakeUnique<BatchRenderer2D>();
        m_BatchRenderer->init(shader);
    }

    void Renderer2D::shutdown()
    {
        if (m_BatchRenderer)
        {
            m_BatchRenderer->shutdown();
        }
    }

    void Renderer2D::beginScene(const SharedPtr<OrthographicCamera>& camera)
    {
        m_Camera = camera;
        if (m_Camera)
        {
            bgfx::setViewTransform(0, nullptr, glm::value_ptr(m_Camera->getProjectionMatrix()));
        }
        m_BatchRenderer->begin();
        m_sceneInProgress = true;
    }

    void Renderer2D::endScene()
    {
        if (m_sceneInProgress)
        {
            m_BatchRenderer->end();
            m_sceneInProgress = false;
        }
    }

    void Renderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, const Color& color)
    {
        if (!m_sceneInProgress)
        {
            TINA_LOG_WARN("Attempting to draw outside of scene");
            return;
        }

        m_BatchRenderer->drawQuad(position, size, color);
    }

    void Renderer2D::drawSprite(const glm::vec2& position, const glm::vec2& size, const SharedPtr<Texture2D>& texture,
                                const glm::vec4& texCoords, const Color& color)
    {
        if (!m_sceneInProgress)
        {
            TINA_LOG_WARN("Attempting to draw outside of scene");
            return;
        }
        if (texture && texture->isValid())
        {
            m_BatchRenderer->drawTexturedQuad(position, size, texture, texCoords, color);
        }
    }
} // Tina
