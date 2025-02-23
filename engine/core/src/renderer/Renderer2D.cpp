// //
// // Created by wuxianggujun on 2025/2/14.
// //
//
// #include "tina/renderer/Renderer2D.hpp"
//
// #include "tina/log/Logger.hpp"
// #include "tina/core/Engine.hpp"
//
// namespace Tina
// {
//     Renderer2D::Renderer2D() = default;
//     Renderer2D::~Renderer2D() = default;
//
//     void Renderer2D::init()
//     {
//         m_BatchRenderer = MakeUnique<BatchRenderer2D>();
//         m_BatchRenderer->init();
//         RenderCommandQueue::getInstance().setBatchRenderer(m_BatchRenderer.get());
//     }
//
//     void Renderer2D::shutdown()
//     {
//         if (m_BatchRenderer)
//         {
//             RenderCommandQueue::getInstance().setBatchRenderer(nullptr);
//             m_BatchRenderer->shutdown();
//             m_BatchRenderer.reset();
//         }
//     }
//
//     void Renderer2D::beginScene(const SharedPtr<OrthographicCamera>& camera)
//     {
//         m_Camera = camera;
//         if (m_Camera)
//         {
//             const auto& view = m_Camera->getViewMatrix();
//             const auto& proj = m_Camera->getProjectionMatrix();
//
//             bgfx::setViewTransform(m_ViewId,
//                                    glm::value_ptr(view),
//                                    glm::value_ptr(proj)
//             );
//             TINA_CORE_LOG_DEBUG("Setting view transform with camera matrices");
//         }
//         else
//         {
//             TINA_CORE_LOG_WARN("No camera provided for scene rendering");
//         }
//         if (m_BatchRenderer)
//         {
//             m_BatchRenderer->begin();
//         }
//         m_sceneInProgress = true;
//     }
//
//     void Renderer2D::endScene()
//     {
//         if (m_sceneInProgress)
//         {
//             if (m_BatchRenderer)
//             {
//                 m_BatchRenderer->end();
//                 TINA_CORE_LOG_DEBUG("Scene end - ViewId: {}", m_ViewId);
//             }
//             m_sceneInProgress = false;
//         }
//     }
//
//     void Renderer2D::drawQuad(const glm::vec2& position, const glm::vec2& size, const Color& color)
//     {
//         if (!m_sceneInProgress)
//         {
//             TINA_CORE_LOG_WARN("Attempting to draw outside of scene");
//             return;
//         }
//
//         if (m_BatchRenderer)
//         {
//             m_BatchRenderer->drawQuad(position, size, color);
//             TINA_CORE_LOG_DEBUG("Drawing quad at ({}, {}) with size ({}, {})",
//                 position.x, position.y, size.x, size.y);
//         }
//     }
//
//     void Renderer2D::drawSprite(const glm::vec2& position, const glm::vec2& size, const SharedPtr<Texture2D>& texture,
//                                 const glm::vec4& texCoords, const Color& color)
//     {
//         if (!m_sceneInProgress)
//         {
//             TINA_CORE_LOG_WARN("Attempting to draw outside of scene");
//             return;
//         }
//
//         if (m_BatchRenderer && texture && texture->isValid())
//         {
//             m_BatchRenderer->drawTexturedQuad(position, size, texture, texCoords, color);
//             TINA_CORE_LOG_DEBUG("Drawing sprite at ({}, {}) with size ({}, {}), texture: {}", 
//                 position.x, position.y, size.x, size.y, texture->getName());
//         }
//     }
//
//     void Renderer2D::drawQuads(const std::vector<BatchRenderer2D::InstanceData>& instances)
//     {
//         if (!m_sceneInProgress)
//         {
//             TINA_CORE_LOG_WARN("Attempting to draw outside of scene");
//             return;
//         }
//
//         if (m_BatchRenderer)
//         {
//             m_BatchRenderer->drawQuads(instances);
//         }
//     }
// } // Tina
