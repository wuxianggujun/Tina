#pragma once

#include "graphics/Renderer2D.hpp"
#include "graphics/Camera.hpp"
#include "graphics/RenderLayerManager.hpp"
#include "core/Shader.hpp"
#include "components/RenderComponents.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <vector>

namespace Tina
{
    class RenderSystem {
    public:
        static RenderSystem& getInstance() {
            static RenderSystem instance;
            return instance;
        }

        void initialize();
        void shutdown();

        // 渲染循环
        void beginFrame();
        void render(entt::registry& registry);
        void endFrame();

        // 设置相机
        void setCamera(Camera* camera) { m_activeCamera = camera; }
        Camera* getActiveCamera() { return m_activeCamera; }

        // 获取渲染器
        Renderer2D* getRenderer2D() { return m_renderer2D.get(); }

        // 视口管理
        void setViewport(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
        void setClearColor(const Color& color);

        // 层级管理
        RenderLayerManager& getLayerManager() { return m_layerManager; }

    private:
        RenderSystem() = default;
        ~RenderSystem() = default;

        // 禁止拷贝和赋值
        RenderSystem(const RenderSystem&) = delete;
        RenderSystem& operator=(const RenderSystem&) = delete;

        // 渲染指定层的实体
        void renderLayer(entt::registry& registry, RenderLayer layer);
        float getDepthFromLayer(RenderLayer layer);

    private:
        std::unique_ptr<Renderer2D> m_renderer2D;
        Camera* m_activeCamera = nullptr;
        Color m_clearColor{0.2f, 0.2f, 0.2f, 1.0f};
        bool m_initialized = false;
        RenderLayerManager m_layerManager;
    };
}