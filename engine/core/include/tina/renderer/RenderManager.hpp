#pragma once

#include "tina/renderer/IRenderer.hpp"
#include "tina/renderer/BatchRenderer2D.hpp"
#include "tina/renderer/TextRenderer.hpp"
#include <memory>
#include <unordered_map>
#include <vector>

namespace Tina
{
    class RenderManager
    {
    public:
        static RenderManager& getInstance()
        {
            static RenderManager instance;
            return instance;
        }

        // 初始化渲染管理器
        void init();

        // 清理资源
        void shutdown();

        // 获取特定类型的渲染器
        template <typename T>
        T* getRenderer()
        {
            static_assert(std::is_base_of<IRenderer, T>::value, "T must inherit from IRenderer");

            for (auto& renderer : m_Renderers)
            {
                if (auto typed = dynamic_cast<T*>(renderer.get()))
                {
                    return typed;
                }
            }
            return nullptr;
        }

        // 添加自定义渲染器
        void addRenderer(std::unique_ptr<IRenderer> renderer);

        // 移除渲染器
        void removeRenderer(RendererType type);

        // 开始渲染帧
        void beginFrame();

        // 结束渲染帧
        void endFrame();

        // 设置全局视图变换
        void setViewTransform(const glm::mat4& view, const glm::mat4& proj);

        // 设置全局视口
        void setViewport(uint16_t x, uint16_t y, uint16_t width, uint16_t height);

    private:
        RenderManager() = default;
        ~RenderManager() = default;

        RenderManager(const RenderManager&) = delete;
        RenderManager& operator=(const RenderManager&) = delete;

        // 渲染器列表(按渲染顺序排序)
        std::vector<std::unique_ptr<IRenderer>> m_Renderers;

        // 视图变换
        glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
        glm::mat4 m_ProjectionMatrix = glm::mat4(1.0f);

        // 视口信息
        struct Viewport
        {
            uint16_t x = 0;
            uint16_t y = 0;
            uint16_t width = 0;
            uint16_t height = 0;
        } m_Viewport;

        bool m_Initialized = false;
    };
}
