//
// 轻量 UI 绘制工具：矩形 + 文本
// - 依赖 bgfx + ShaderManager + TextRenderer
// - 面向视图 2（UI 视图，正交像素坐标）

#pragma once

#include <bgfx/bgfx.h>
#include <string>
#include "../renderer/ShaderManager.hpp"
#include "../core/Color.hpp"
#include "TextRenderer.hpp"

namespace Tina::UI {

class UIRenderer {
public:
    UIRenderer() = default;
    ~UIRenderer() = default;

    // 传入 ShaderManager，加载 color 程序；可选绑定 TextRenderer 用于绘制文本
    bool initialize(Tina::Renderer::ShaderManager& sm, TextRenderer* text = nullptr);
    void shutdown();

    void setTextRenderer(TextRenderer* text) { m_text = text; }

    // 在指定视图绘制纯色矩形（像素坐标，左上为原点）
    void drawRect(uint16_t viewId, float x, float y, float w, float h,
                  float r, float g, float b, float a);

    // 在指定视图绘制纯色矩形（Core::Color 版本）
    void drawRect(uint16_t viewId, float x, float y, float w, float h,
                  const Tina::Core::Color& color);

    // 使用 TextRenderer 绘制文本（如未绑定，则忽略调用）
    void drawText(uint16_t viewId, float x, float y,
                  float r, float g, float b, float a,
                  const std::string& utf8);

    // 使用 TextRenderer 绘制文本（Core::Color 版本）
    void drawText(uint16_t viewId, float x, float y,
                  const Tina::Core::Color& color,
                  const std::string& utf8);

    // 批处理：统一提交所有缓存的绘制指令
    // 必须在所有 drawXXX 调用之后，每帧仅调用一次
    void beginFrame(uint16_t viewId);
    void flush();

    // 文本扩展绘制：基于矩形与对齐参数绘制，内部使用 TextRenderer 的精确测量与基线对齐
    enum class AlignH { Left, Center, Right };
    enum class AlignV { Top, Center, Bottom, Baseline };
    void drawTextEx(uint16_t viewId, float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    const std::string& utf8,
                    AlignH halign = AlignH::Center,
                    AlignV valign = AlignV::Center,
                    float padX = 0.0f, float padY = 0.0f);

    // 文本扩展绘制（Core::Color 版本）
    void drawTextEx(uint16_t viewId, float x, float y, float w, float h,
                    const Tina::Core::Color& color,
                    const std::string& utf8,
                    AlignH halign = AlignH::Center,
                    AlignV valign = AlignV::Center,
                    float padX = 0.0f, float padY = 0.0f);

    // 文本测量代理：返回像素宽高；如未绑定 TextRenderer 返回 false
    bool measureText(const std::string& utf8, float& outW, float& outH) const {
        if (!m_text) { outW = outH = 0.0f; return false; }
        const_cast<TextRenderer*>(m_text)->measureText(utf8, outW, outH);
        return true;
    }

    bool measureTextExtents(const std::string& utf8, float& outW, float& outH,
                            float& outTop, float& outBottom) const {
        if (!m_text) { outW = outH = outTop = outBottom = 0.0f; return false; }
        const_cast<TextRenderer*>(m_text)->measureTextExtents(utf8, outW, outH, outTop, outBottom);
        return true;
    }
    int textAscenderPx() const { return m_text ? m_text->ascenderPx() : 0; }

    // 绘制纹理图片（像素坐标）。
    // 若未加载 sprite 程序或纹理无效，则忽略绘制。
    void drawImage(uint16_t viewId, float x, float y, float w, float h,
                   bgfx::TextureHandle tex,
                   float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);

private:
    // 顶点结构
    struct ColorVtx {
        float x, y, z;
        float r, g, b, a;
    };
    struct SpriteVtx {
        float x, y, z;
        float u, v;
        float r, g, b, a;
    };

    // 批处理结构
    struct ColorBatch {
        Container::Vector<ColorVtx> vertices;
        Container::Vector<uint16_t> indices;
        void clear() { vertices.clear(); indices.clear(); }
    };
    struct SpriteBatch {
        Container::Vector<SpriteVtx> vertices;
        Container::Vector<uint16_t> indices;
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        void clear() { vertices.clear(); indices.clear(); texture = BGFX_INVALID_HANDLE; }
    };

    bgfx::ProgramHandle m_progColor = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout  m_colorLayout{};
    TextRenderer*       m_text = nullptr;

    // sprite 绘制（贴图）
    bgfx::ProgramHandle m_progSprite = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout  m_spriteLayout{};
    bgfx::UniformHandle m_sTex = BGFX_INVALID_HANDLE;

    // 批处理缓冲
    ColorBatch m_colorBatch;
    Container::Vector<SpriteBatch> m_spriteBatches;
    uint16_t m_currentViewId = 0;

    // 内部工具方法
    SpriteBatch* findOrCreateSpriteBatch(bgfx::TextureHandle tex);
    void flushColorBatch();
    void flushSpriteBatches();
};

} // namespace Tina::UI
