//
// RenderCommandBuilder - 渲染命令构建器
// 用途：提供流式API来构建渲染命令，简化命令创建过程
//

#pragma once

#include "RenderCommand.hpp"
#include "RenderQueue.hpp"
#include "../core/Color.hpp"

namespace Tina::Renderer {

class RenderCommandBuilder {
public:
    explicit RenderCommandBuilder(RenderQueue* queue = nullptr)
        : m_queue(queue) {}

    // 设置渲染队列
    RenderCommandBuilder& setQueue(RenderQueue* queue) {
        m_queue = queue;
        return *this;
    }

    // 基础设置
    RenderCommandBuilder& view(uint16_t viewId) {
        m_cmd.viewId = viewId;
        return *this;
    }

    RenderCommandBuilder& layer(RenderLayer layer) {
        m_layer = layer;
        return *this;
    }

    RenderCommandBuilder& depth(uint16_t depth) {
        m_depth = depth;
        return *this;
    }

    RenderCommandBuilder& blend(BlendMode mode) {
        m_cmd.blendMode = mode;
        return *this;
    }

    RenderCommandBuilder& program(bgfx::ProgramHandle prog) {
        m_cmd.program = prog;
        return *this;
    }

    // === 矩形构建 ===
    RenderCommandBuilder& rect(float x, float y, float w, float h) {
        m_cmd.type = RenderType::Rectangle;
        m_cmd.data.rect = {x, y, w, h, Core::Color{1,1,1,1}, 0.0f};
        return *this;
    }

    RenderCommandBuilder& color(const Core::Color& c) {
        if (m_cmd.type == RenderType::Rectangle) {
            m_cmd.data.rect.color = c;
            // 自动设置混合模式
            if (c.a() < 1.0f && m_cmd.blendMode == BlendMode::Opaque) {
                m_cmd.blendMode = BlendMode::Alpha;
            }
        } else if (m_cmd.type == RenderType::Sprite) {
            m_cmd.data.sprite.tint = c;
        }
        return *this;
    }

    RenderCommandBuilder& color(float r, float g, float b, float a = 1.0f) {
        return color(Core::Color{r, g, b, a});
    }

    RenderCommandBuilder& rotation(float radians) {
        if (m_cmd.type == RenderType::Rectangle) {
            m_cmd.data.rect.rotation = radians;
        } else if (m_cmd.type == RenderType::Sprite) {
            m_cmd.data.sprite.rotation = radians;
        }
        return *this;
    }

    // === 精灵构建 ===
    RenderCommandBuilder& sprite(float x, float y, float w, float h, bgfx::TextureHandle tex) {
        m_cmd.type = RenderType::Sprite;
        m_cmd.data.sprite = {x, y, w, h, tex, 0, 0, 1, 1, {1,1,1,1}, 0};
        m_textureId = tex.idx;
        return *this;
    }

    RenderCommandBuilder& uv(float u0, float v0, float u1, float v1) {
        if (m_cmd.type == RenderType::Sprite) {
            m_cmd.data.sprite.u0 = u0;
            m_cmd.data.sprite.v0 = v0;
            m_cmd.data.sprite.u1 = u1;
            m_cmd.data.sprite.v1 = v1;
        }
        return *this;
    }

    RenderCommandBuilder& tint(const Core::Color& c) {
        return color(c);  // 复用color方法
    }

    // （已移除文本与自定义渲染构建接口，统一由 UIRenderer/TextRenderer 负责）

    // 构建并提交
    void submit() {
        // 生成排序键
        m_cmd.sortKey = RenderCommand::MakeSortKey(m_cmd.viewId, m_layer, m_cmd.type, m_textureId, m_depth);

        // 提交到队列
        if (m_queue) {
            m_queue->submit(m_cmd);
        }

        // 重置
        reset();
    }

    // 构建并返回命令（不提交）
    RenderCommand build() {
        m_cmd.sortKey = RenderCommand::MakeSortKey(m_cmd.viewId, m_layer, m_cmd.type, m_textureId, m_depth);
        RenderCommand result = m_cmd;
        reset();
        return result;
    }

    // 重置构建器
    void reset() {
        m_cmd = RenderCommand{};
        m_layer = RenderLayer::World;
        m_depth = 0;
        m_textureId = 0;
    }

private:
    RenderQueue* m_queue = nullptr;
    RenderCommand m_cmd;
    RenderLayer m_layer = RenderLayer::World;
    uint16_t m_depth = 0;
    uint16_t m_textureId = 0;
};

// 便捷函数：快速创建常用命令
namespace QuickDraw {
    // 绘制带颜色的矩形
    inline void rect(RenderQueue& queue, uint16_t viewId, RenderLayer layer,
                    float x, float y, float w, float h,
                    const Core::Color& color, uint16_t depth = 0) {
        RenderCommandBuilder(&queue)
            .view(viewId)
            .layer(layer)
            .depth(depth)
            .rect(x, y, w, h)
            .color(color)
            .submit();
    }

    // 绘制精灵
    inline void sprite(RenderQueue& queue, uint16_t viewId, RenderLayer layer,
                      float x, float y, float w, float h,
                      bgfx::TextureHandle texture,
                      const Core::Color& tint = {1,1,1,1},
                      uint16_t depth = 0) {
        RenderCommandBuilder(&queue)
            .view(viewId)
            .layer(layer)
            .depth(depth)
            .sprite(x, y, w, h, texture)
            .tint(tint)
            .submit();
    }

    // （已移除 QuickDraw::text 与 QuickDraw::gradient，文本与背景渐变请使用 TextRenderer/SceneRenderer）
}

} // namespace Tina::Renderer
