#pragma once

#include "math/Vector.hpp"
#include "graphics/Color.hpp"
#include <bgfx/bgfx.h>
#include <functional>

namespace Tina
{
    enum class RenderCommandType
    {
        None = 0,
        Clear,
        DrawQuad,
        DrawSprite,
        DrawText,
        Custom
    };

    class RenderCommand
    {
    public:
        RenderCommand() = default;

        // 创建清除命令
        static RenderCommand createClear(const Color& color)
        {
            RenderCommand cmd;
            cmd.type = RenderCommandType::Clear;
            cmd.clearColor = color;
            return cmd;
        }

        // 创建绘制四边形命令
        static RenderCommand createDrawQuad(const Vector2f& position, const Vector2f& size,
                                            const Color& color, float rotation = 0.0f)
        {
            RenderCommand cmd;
            cmd.type = RenderCommandType::DrawQuad;
            cmd.position = position;
            cmd.size = size;
            cmd.color = color;
            cmd.rotation = rotation;
            return cmd;
        }

        // 创建绘制精灵命令
        static RenderCommand createDrawSprite(const Vector2f& position, const Vector2f& size,
                                              bgfx::TextureHandle texture, const Color& tint = Color::White)
        {
            RenderCommand cmd;
            cmd.type = RenderCommandType::DrawSprite;
            cmd.position = position;
            cmd.size = size;
            cmd.texture = texture;
            cmd.color = tint;
            return cmd;
        }

        // 创建自定义渲染命令
        static RenderCommand createCustom(std::function<void()> renderFunc)
        {
            RenderCommand cmd;
            cmd.type = RenderCommandType::Custom;
            cmd.customRenderFunc = std::move(renderFunc);
            return cmd;
        }

    public:
        RenderCommandType type = RenderCommandType::None;

        // 通用属性
        Vector2f position{0.0f, 0.0f};
        Vector2f size{1.0f, 1.0f};
        Color color = Color::White;
        float rotation = 0.0f;
        float depth = 0.0f;

        // 材质相关
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;

        // 自定义渲染函数
        std::function<void()> customRenderFunc;

        // 清除颜色
        Color clearColor{0.0f, 0.0f, 0.0f, 1.0f};

        // 排序比较运算符
        bool operator<(const RenderCommand& other) const
        {
            return depth < other.depth;
        }
    };
}
