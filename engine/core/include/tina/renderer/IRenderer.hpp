#pragma once

#include <glm/glm.hpp>
#include <bgfx/bgfx.h>
#include "tina/renderer/Color.hpp"

namespace Tina
{
    // 渲染器类型枚举
    enum class RendererType
    {
        Batch2D, // 2D批量渲染
        Text, // 文本渲染
        Custom // 自定义渲染器
    };

    // 渲染器接口
    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        // 初始化渲染器
        virtual void init() = 0;

        // 清理资源
        virtual void shutdown() = 0;

        // 开始渲染
        virtual void begin() = 0;

        // 结束渲染
        virtual void end() = 0;

        // 刷新渲染缓冲
        virtual void flush() = 0;

        // 设置视图
        virtual void setViewId(uint16_t viewId) = 0;
        virtual uint16_t getViewId() const = 0;

        // 设置视图变换
        virtual void setViewTransform(const glm::mat4& view, const glm::mat4& proj) = 0;

        // 设置视口
        virtual void setViewRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height) = 0;

        // 设置视图清除标志和颜色
        virtual void setViewClear(uint16_t viewId, uint16_t flags, uint32_t rgba = 0x000000ff,
                                float depth = 1.0f, uint8_t stencil = 0) = 0;

        // 获取渲染器类型
        virtual RendererType getType() const = 0;

    protected:
        uint16_t m_ViewId = 0;
        bool m_Initialized = false;
    };
}
