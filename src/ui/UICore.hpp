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

private:
    bgfx::ProgramHandle m_progColor = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout  m_colorLayout{};
    TextRenderer*       m_text = nullptr;
};

} // namespace Tina::UI
