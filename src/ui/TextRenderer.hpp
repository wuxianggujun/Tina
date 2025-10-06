//
// 极简文字渲染器：FreeType + bgfx 图集
//

#pragma once

#include <bgfx/bgfx.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <string>
#include "../core/Container.hpp"
#include "../renderer/ShaderManager.hpp"

namespace Tina::UI {

class TextRenderer {
public:
    TextRenderer();
    ~TextRenderer();

    bool initialize(int atlasW = 2048, int atlasH = 2048);
    void shutdown();

    // 加载字体（ttf/otf），size 为像素高度
    bool loadFont(const std::string& path, int pixelSize);

    // 在指定视图绘制 UTF-8 文本（左上坐标系），颜色 RGBA [0..1]
    void drawText(uint16_t viewId, float x, float y,
                  float r, float g, float b, float a,
                  const std::string& utf8);

    // 可选：设置/清除裁剪矩形（屏幕像素坐标，针对 UI 视图等像素空间）
    void setClipRect(int16_t x, int16_t y, uint16_t w, uint16_t h);
    void clearClipRect();

    // 文本测量：计算 UTF-8 文本在当前字体下的像素宽高（多行按换行分行）
    // 注意：测量过程中会按需生成字形，确保与渲染一致
    void measureText(const std::string& utf8, float& outWidth, float& outHeight);
    void measureTextExtents(const std::string& utf8,
                            float& outWidth, float& outHeight,
                            float& outTop, float& outBottom) const;

    // 像素对齐（开启可显著减少文字轻微模糊/抖动，默认开启）
    void setPixelSnap(bool enable) { m_pixelSnap = enable; }
    bool pixelSnap() const { return m_pixelSnap; }
    int ascenderPx() const { return m_font.ascender; }
    int descenderPx() const { return m_font.descender; }

private:
    struct Glyph {
        int codepoint = 0;
        int w = 0, h = 0;
        int bearingX = 0, bearingY = 0; // 像素
        int advance = 0;                 // 1/64 像素（FreeType 原始单位）
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        int atlasX = 0, atlasY = 0;
    };

    struct Font {
        FT_Face face = nullptr;
        int sizePx = 0;
        int ascender = 0;
        int descender = 0;
        Tina::Container::HashMap<int, Glyph> glyphs;
    };

    bool ensureGlyph(Font& font, int codepoint);
    static bool utf8Next(const char*& p, const char* end, int& outCode);

private:
    FT_Library m_ft = nullptr;
    Font m_font; // 当前仅支持加载一个字体（可扩展为多字体）

    // 图集
    int m_atlasW = 0, m_atlasH = 0;
    Tina::Container::Vector<uint8_t> m_atlasPixels;
    int m_penX = 1, m_penY = 1, m_rowH = 0;
    bgfx::TextureHandle m_atlasTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sText = BGFX_INVALID_HANDLE;           // 兼容 GLSL/Metal 名称
    bgfx::UniformHandle m_sTextTexture = BGFX_INVALID_HANDLE;     // 兼容 HLSL 反射名
    bgfx::TextureHandle m_debugTex = BGFX_INVALID_HANDLE;         // 调试 2x2 纹理

    // 着色器
    Tina::Renderer::ShaderManager m_shaderMgr;
    bgfx::ProgramHandle m_prog = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;

    bool m_hasClip = false;
    int16_t m_clipX = 0, m_clipY = 0; uint16_t m_clipW = 0, m_clipH = 0;

    bool m_pixelSnap = true;
};

} // namespace Tina::UI


