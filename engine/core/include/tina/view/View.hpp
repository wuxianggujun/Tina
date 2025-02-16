//
// Created by wuxianggujun on 2025/2/16.
//

#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ViewManager.hpp"
#include "tina/core/Node.hpp"

namespace Tina
{
    class TINA_CORE_API View : public Node
    {
    public:
        struct RenderState
        {
            uint16_t viewId = 0;
            Rect viewport;
            Rect scissor;
            uint32_t clearFlags = 0;
            uint32_t clearColor = 0;
            float clearDepth = 1.0f;
            uint8_t clearStencil = 0;
            bgfx::ViewMode::Enum viewMode = bgfx::ViewMode::Default;
            glm::mat4 viewMatrix{1.0f};
            glm::mat4 projMatrix{1.0f};
        };

        explicit View(const std::string& name = "View"): Node(), renderState(), m_initialized(false)
        {
            this->name = name;
            renderState.viewId = ViewManager::getInstance().allocateViewId();
        }

        ~View() override
        {
            ViewManager::getInstance().releaseViewId(renderState.viewId);
        }

        // 生命周期
        void onAttach() override
        {
            if (!m_initialized)
            {
                initializeView();
                m_initialized = true;
            }
        }

        void onDetach() override
        {
            m_initialized = false;
        }

        void onUpdate(const float deltaTime) override
        {
            // 基类默认实现 - 更新子视图
            for (auto* child : getChildren())
            {
                if (auto* view = dynamic_cast<View*>(child))
                {
                    view->onUpdate(deltaTime);
                }
            }
        }


        // 渲染状态管理
        void setViewPort(const Rect& viewport)
        {
            TINA_LOG_DEBUG("View '{}': Viewport set to {}x{} at ({},{})",
                name,
                viewport.getWidth(), viewport.getHeight(),
                viewport.getX(), viewport.getY());

            renderState.viewport = viewport;
            renderState.scissor = viewport;  // 同时更新裁剪区域

            // 立即更新渲染状态
            updateRenderState();

            // 通知子视图
            for (auto* child : getChildren())
            {
                if (View* childView = dynamic_cast<View*>(child))
                {
                    childView->setViewPort(viewport);
                }
            }
        }

        void setScissor(const Rect& scissor)
        {
            renderState.scissor = scissor;
            updateRenderState();
        }

        void setClearFlag(uint32_t clearFlag)
        {
            renderState.clearFlags = clearFlag;
            updateRenderState();
        }

        void setClearColor(uint32_t clearColor)
        {
            renderState.clearColor = clearColor;
            updateRenderState();
        }


        bool onEvent(Event& event) override
        {
            bool handled = false;

            if (event.type == Event::WindowResize)
            {
                TINA_LOG_INFO("View '{}': Handling WindowResize {}x{}",
                    name,
                    event.windowResize.width,
                    event.windowResize.height);

                // 更新 bgfx 的分辨率和视口
                bgfx::reset(
                    event.windowResize.width,
                    event.windowResize.height,
                    BGFX_RESET_VSYNC
                );

                // 更新视口
                setViewPort(Rect(
                    0, 0,
                    event.windowResize.width,
                    event.windowResize.height
                ));

                handled = true;
            }

            // 如果当前视图没有处理事件，传递给子视图
            if (!handled)
            {
                handled = Node::onEvent(event);
            }

            return handled;
        }

        virtual void beginRender()
        {
            if (!isVisible()) return;

            TINA_LOG_DEBUG("View '{}': Begin render with viewport {}x{}",
                name,
                renderState.viewport.getWidth(),
                renderState.viewport.getHeight());

            // 设置视图状态
            bgfx::setViewRect(
                renderState.viewId,
                static_cast<uint16_t>(renderState.viewport.getX()),
                static_cast<uint16_t>(renderState.viewport.getY()),
                static_cast<uint16_t>(renderState.viewport.getWidth()),
                static_cast<uint16_t>(renderState.viewport.getHeight())
            );

            bgfx::setViewScissor(
                renderState.viewId,
                static_cast<uint16_t>(renderState.scissor.getX()),
                static_cast<uint16_t>(renderState.scissor.getY()),
                static_cast<uint16_t>(renderState.scissor.getWidth()),
                static_cast<uint16_t>(renderState.scissor.getHeight())
            );

            // 设置视图和投影矩阵
            bgfx::setViewTransform(
                renderState.viewId,
                glm::value_ptr(renderState.viewMatrix),
                glm::value_ptr(renderState.projMatrix)
            );

            // 设置视图模式
            bgfx::setViewMode(
                renderState.viewId,
                renderState.viewMode
            );

            // 设置视图清除标志
            bgfx::setViewClear(
                renderState.viewId,
                renderState.clearFlags,
                renderState.clearColor,
                renderState.clearDepth,
                renderState.clearStencil
            );

            // 触发视图渲染
            bgfx::touch(renderState.viewId);
        }

        virtual void endRender()
        {
        }

        // 强制更新渲染状态的公共接口
        void forceUpdateRenderState()
        {
            updateRenderState();
            // 立即应用渲染状态
            beginRender();
        }

        // 强制更新渲染状态
    protected:
        void updateRenderState()
        {
            TINA_LOG_DEBUG("View '{}': Updating render state, viewport: {}x{}, scissor: {}x{}",
                name,
                renderState.viewport.getWidth(), renderState.viewport.getHeight(),
                renderState.scissor.getWidth(), renderState.scissor.getHeight());

            // 设置视图矩形
            bgfx::setViewRect(
                renderState.viewId,
                static_cast<uint16_t>(renderState.viewport.getX()),
                static_cast<uint16_t>(renderState.viewport.getY()),
                static_cast<uint16_t>(renderState.viewport.getWidth()),
                static_cast<uint16_t>(renderState.viewport.getHeight())
            );

            // 设置裁剪区域
            bgfx::setViewScissor(
                renderState.viewId,
                static_cast<uint16_t>(renderState.scissor.getX()),
                static_cast<uint16_t>(renderState.scissor.getY()),
                static_cast<uint16_t>(renderState.scissor.getWidth()),
                static_cast<uint16_t>(renderState.scissor.getHeight())
            );

            // 设置视图和投影矩阵
            bgfx::setViewTransform(
                renderState.viewId,
                glm::value_ptr(renderState.viewMatrix),
                glm::value_ptr(renderState.projMatrix)
            );

            // 设置视图模式
            bgfx::setViewMode(
                renderState.viewId,
                renderState.viewMode
            );

            // 触发视图渲染
            bgfx::touch(renderState.viewId);
        }

        // 渲染状态
        RenderState renderState;
        bool m_initialized;

        virtual void initializeView()
        {
            // 初始化渲染状态
            renderState.clearFlags = BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH;
            renderState.clearColor = 0x303030ff;
            renderState.clearDepth = 1.0f;
            renderState.clearStencil = 0;
            renderState.viewMode = bgfx::ViewMode::Default;
        }

        // 当视口大小改变时调用
        void updateViewport(const Rect& viewport)
        {
            renderState.viewport = viewport;
            renderState.scissor = viewport; // 默认scissor与viewport相同
        }

        // 当需要更新变换矩阵时调用
        void updateTransform(const glm::mat4& view, const glm::mat4& proj)
        {
            renderState.viewMatrix = view;
            renderState.projMatrix = proj;
        }
    };
}
