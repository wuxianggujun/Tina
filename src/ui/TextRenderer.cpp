#include "TextRenderer.hpp"
#include "../core/Log.hpp"
#include <bx/math.h>

namespace Tina::UI {

namespace {
struct Vtx {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a; // 颜色压缩为归一化 U8，减少带宽
};
}

TextRenderer::TextRenderer() {}
TextRenderer::~TextRenderer() { shutdown(); }

bool TextRenderer::initialize(Tina::Renderer::ShaderManager& sm, Tina::Engine::ResourceManagerHub& hub, int atlasW, int atlasH)
{
    m_resHub = &hub;

    m_atlasW = atlasW;
    m_atlasH = atlasH;
    // 使用 RGBA8 图集，字形覆盖度写入 A 通道，RGB 固定为 1.0（白）
    m_atlasPixels.resize((size_t)atlasW * atlasH * 4, 0);

    // 创建可更新（mutable）的图集纹理：_mem 传入 NULL，后续使用 updateTexture2D 写入内容
    m_atlasTex = bgfx::createTexture2D((uint16_t)atlasW, (uint16_t)atlasH, false, 1,
                                       bgfx::TextureFormat::RGBA8,
                                       BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                       BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
                                       nullptr);
    if (!bgfx::isValid(m_atlasTex)) {
        TINA_ERROR("TextRenderer: 创建图集纹理失败");
        return false;
    }
    m_sText = bgfx::createUniform("s_text", bgfx::UniformType::Sampler);

    // 初始化：将图集初始填充为 RGBA = (255,255,255,0)，便于验证采样是否生效
    {
        const size_t pxCount = (size_t)m_atlasW * (size_t)m_atlasH;
        for (size_t i = 0; i < pxCount; ++i) {
            m_atlasPixels[i*4 + 0] = 255;
            m_atlasPixels[i*4 + 1] = 255;
            m_atlasPixels[i*4 + 2] = 255;
            m_atlasPixels[i*4 + 3] = 0;
        }
        const bgfx::Memory* all = bgfx::copy(m_atlasPixels.data(), (uint32_t)m_atlasPixels.size());
        bgfx::updateTexture2D(m_atlasTex, 0, 0, 0, 0, (uint16_t)m_atlasW, (uint16_t)m_atlasH, all);
    }

    // 创建 2x2 调试纹理（红、绿、蓝、白），用于快速验证采样绑定
    {
        const uint32_t w = 2, h = 2;
        uint8_t pixels[w*h*4] = {
            255, 0,   0,   255,   0, 255, 0, 255,
            0,   0, 255, 255,   255,255,255,255,
        };
        const bgfx::Memory* memdbg = bgfx::copy(pixels, sizeof(pixels));
        m_debugTex = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1, bgfx::TextureFormat::RGBA8,
                                           BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                           BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
                                           memdbg);
    }

    // 顶点布局
    m_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
    .end();

    // 着色器（由全局 ShaderManager 统一管理与清理）
    m_prog = sm.loadProgram("text", "text");
    if (!bgfx::isValid(m_prog)) {
        TINA_ERROR("TextRenderer: 加载 text 着色器失败");
        return false;
    }
    // 降噪：初始化成功的详细日志仅在调试时需要
    // TINA_INFO("TextRenderer: 初始化完成，图集={}x{}，采样=PointClamp，程序有效={}", m_atlasW, m_atlasH, (int)bgfx::isValid(m_prog));
    return true;
}

void TextRenderer::shutdown()
{
    if (bgfx::isValid(m_atlasTex)) { bgfx::destroy(m_atlasTex); m_atlasTex = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_debugTex)) { bgfx::destroy(m_debugTex); m_debugTex = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_sText))    { bgfx::destroy(m_sText);    m_sText = BGFX_INVALID_HANDLE; }
    // 程序由全局 ShaderManager 管理，这里不做销毁
    m_font.face = nullptr; m_font.glyphs.clear();
    m_fontRef.reset(); m_resHub = nullptr;
}

bool TextRenderer::loadFont(const std::string& path, int pixelSize)
{
    if (!m_resHub) return false;
    auto* fr = m_resHub->load<Tina::Engine::FontResource>(Tina::Engine::Path(path.c_str()));
    if (!fr) { TINA_ERROR("TextRenderer: 字体资源加载失败: {}", path); return false; }
    m_fontRef = Tina::Engine::ResourceRef<Tina::Engine::FontResource>(m_resHub, fr);
    m_requestedFontPx = pixelSize;
    m_font.glyphs.clear();

    // 尝试立即建立 Face；如资源尚未 READY，延迟到后续渲染/测量时再完成
    if (!ensureFontReady()) {
        TINA_INFO("TextRenderer: 字体未就绪，等待异步加载完成: {}@{}", path, pixelSize);
    }
    return true;
}

bool TextRenderer::ensureGlyph(Font& font, int codepoint)
{
    if (font.glyphs.find(codepoint) != font.glyphs.end()) return true;
    if (!font.face) return false;
    if (FT_Load_Char(font.face, (FT_ULong)codepoint, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) return false;
    FT_GlyphSlot g = font.face->glyph;
    int gw = g->bitmap.width;
    int gh = g->bitmap.rows;

    if (gw == 0 || gh == 0) {
        Glyph gi; gi.codepoint = codepoint; gi.advance = (int)(g->advance.x >> 6);
        font.glyphs[codepoint] = gi;
        return true;
    }

    // 行打包
    if (m_penX + gw + 1 >= m_atlasW) { m_penX = 1; m_penY += m_rowH + 1; m_rowH = 0; }
    if (m_penY + gh + 1 >= m_atlasH) {
        TINA_ERROR("TextRenderer: 图集已满 ({}x{}), 无法放入更多字形", m_atlasW, m_atlasH);
        return false;
    }
    int dstX = m_penX;
    int dstY = m_penY;
    m_rowH = std::max(m_rowH, gh);
    m_penX += gw + 1;

    // 生成连续 RGBA 块（gw x gh），A=覆盖度；用于一次性提交 GPU，减少 update 次数
    Tina::Container::Vector<uint8_t> block;
    block.resize((size_t)gw * (size_t)gh * 4);
    for (int y = 0; y < gh; ++y) {
        const int pitch = g->bitmap.pitch;
        const uint8_t* base = g->bitmap.buffer;
        const uint8_t* src = (pitch >= 0)
            ? base + (size_t)y * (size_t)pitch
            : base + (size_t)(gh - 1 - y) * (size_t)(-pitch);
        uint8_t* row = block.data() + (size_t)y * (size_t)gw * 4;
        for (int x = 0; x < gw; ++x) {
            row[x*4 + 0] = 255;
            row[x*4 + 1] = 255;
            row[x*4 + 2] = 255;
            row[x*4 + 3] = src[x];
        }
        // 同步写入 CPU 图集缓存
        uint8_t* dstCpu = m_atlasPixels.data() + ((size_t)(dstY + y) * (size_t)m_atlasW + (size_t)dstX) * 4;
        memcpy(dstCpu, row, (size_t)gw * 4);
    }
    const bgfx::Memory* memBlock = bgfx::copy(block.data(), (uint32_t)block.size());
    bgfx::updateTexture2D(m_atlasTex, 0, 0, (uint16_t)dstX, (uint16_t)dstY, (uint16_t)gw, (uint16_t)gh, memBlock);

    // 上面已逐行提交（每行 gw*4 字节），此处不重复提交

    // 记录字形信息
    Glyph info;
    info.codepoint = codepoint;
    info.w = gw; info.h = gh;
    info.bearingX = g->bitmap_left;
    info.bearingY = g->bitmap_top;
    info.advance  = (int)(g->advance.x >> 6);
    info.atlasX = dstX; info.atlasY = dstY;
    // 半像素内缩，减少边缘采样越界带来的伪影
    const float halfU = 0.5f / (float)m_atlasW;
    const float halfV = 0.5f / (float)m_atlasH;
    info.u0 = (float)dstX / (float)m_atlasW + halfU; info.v0 = (float)dstY / (float)m_atlasH + halfV;
    info.u1 = (float)(dstX + gw) / (float)m_atlasW - halfU; info.v1 = (float)(dstY + gh) / (float)m_atlasH - halfV;
    font.glyphs[codepoint] = info;
    return true;
}

bool TextRenderer::utf8Next(const char*& p, const char* end, int& outCode)
{
    if (p >= end) return false;
    unsigned char c = (unsigned char)*p++; 
    if (c < 0x80) { outCode = c; return true; }
    if ((c >> 5) == 0x6 && p < end) {
        outCode = ((c & 0x1f) << 6) | ((unsigned char)*p++ & 0x3f); return true;
    } else if ((c >> 4) == 0xe && p + 1 < end) {
        outCode = ((c & 0x0f) << 12) | (((unsigned char)*p++ & 0x3f) << 6) | ((unsigned char)*p++ & 0x3f); return true;
    } else if ((c >> 3) == 0x1e && p + 2 < end) {
        outCode = ((c & 0x07) << 18) | (((unsigned char)*p++ & 0x3f) << 12) | (((unsigned char)*p++ & 0x3f) << 6) | ((unsigned char)*p++ & 0x3f); return true;
    }
    outCode = '?'; return true; // 容错
}

void TextRenderer::drawText(uint16_t viewId, float x, float y,
                            float r, float g, float b, float a,
                            const std::string& utf8)
{
    if (!bgfx::isValid(m_prog) || !bgfx::isValid(m_atlasTex)) return;
    if (!ensureFontReady()) return;

    // UTF-8 解码并准备顶点/索引
    Tina::Container::Vector<Vtx> verts;
    // 使用 16 位索引以匹配 bgfx::allocTransientBuffers 默认索引格式
    Tina::Container::Vector<uint16_t> idx;
    verts.reserve(utf8.size() * 4);
    idx.reserve(utf8.size() * 6);

    float penX = x;
    float baseY = y + (float)m_font.ascender;
    const char* p = utf8.data();
    const char* end = p + utf8.size();
    uint32_t vi = 0;
    int code = 0;
    const bool hasKerning = (m_font.face && FT_HAS_KERNING(m_font.face));
    FT_UInt prevGlyphIdx = 0;
    int missing = 0;
    int appended = 0;
    while (utf8Next(p, end, code)) {
        if (code == '\n') { penX = x; baseY += (float)m_font.sizePx; continue; }
        if (!ensureGlyph(m_font, code)) { ++missing; continue; }
        // Kerning（字距调整）
        if (hasKerning) {
            FT_UInt glyphIdx = FT_Get_Char_Index(m_font.face, (FT_ULong)code);
            if (prevGlyphIdx != 0 && glyphIdx != 0) {
                FT_Vector delta{};
                if (FT_Get_Kerning(m_font.face, prevGlyphIdx, glyphIdx, FT_KERNING_DEFAULT, &delta) == 0) {
                    penX += (float)(delta.x >> 6);
                }
            }
            prevGlyphIdx = glyphIdx;
        }
        const Glyph& gph = m_font.glyphs[code];
        float gx0 = penX + (float)gph.bearingX;
        float gy0 = baseY - (float)gph.bearingY;
        float gx1 = gx0 + (float)gph.w;
        float gy1 = gy0 + (float)gph.h;

        // 像素对齐：将顶点位置对齐到整数像素，提升清晰度
        if (m_pixelSnap) {
            float sx0 = std::floor(gx0 + 0.5f);
            float sy0 = std::floor(gy0 + 0.5f);
            float sx1 = sx0 + (float)gph.w;
            float sy1 = sy0 + (float)gph.h;
            gx0 = sx0; gy0 = sy0; gx1 = sx1; gy1 = sy1;
        }
        auto toU8 = [](float v)->uint8_t{ v = bx::clamp(v, 0.0f, 1.0f); return (uint8_t)(v*255.0f + 0.5f); };
        const uint8_t cr = toU8(r), cg = toU8(g), cb = toU8(b), ca = toU8(a);
        verts.push_back({gx0, gy0, 0.0f, gph.u0, gph.v0, cr,cg,cb,ca});
        verts.push_back({gx1, gy0, 0.0f, gph.u1, gph.v0, cr,cg,cb,ca});
        verts.push_back({gx1, gy1, 0.0f, gph.u1, gph.v1, cr,cg,cb,ca});
        verts.push_back({gx0, gy1, 0.0f, gph.u0, gph.v1, cr,cg,cb,ca});
        // 顶点数量通常远小于 65535，此处安全转换为 16 位索引
        idx.push_back(static_cast<uint16_t>(vi+0)); idx.push_back(static_cast<uint16_t>(vi+1)); idx.push_back(static_cast<uint16_t>(vi+2));
        idx.push_back(static_cast<uint16_t>(vi+0)); idx.push_back(static_cast<uint16_t>(vi+2)); idx.push_back(static_cast<uint16_t>(vi+3));
        vi += 4;
        ++appended;
        penX += (float)gph.advance; // advance 已为像素（>>6），我们上方转换了
    }

    if (idx.empty()) {
        // 降噪：无可绘制字形在 UI 空字符串或被裁剪时很常见，不警告
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, m_layout, (uint32_t)verts.size(), &tib, (uint32_t)idx.size()))
        return;
    memcpy(tvb.data, verts.data(), (size_t)verts.size() * sizeof(Vtx));
    memcpy(tib.data, idx.data(), (size_t)idx.size() * sizeof(uint16_t));

    // 额外日志：打印首个字形的四个顶点位置与 UV，辅助定位坐标系问题
    // 降噪：首字形顶点/UV 仅在排障时打印

    bgfx::Encoder* enc = bgfx::begin(false);
    if (!enc) { TINA_WARN("TextRenderer: 无法获取 Encoder，放弃绘制"); return; }
    float mtx[16]; bx::mtxIdentity(mtx);
    enc->setTransform(mtx);
    enc->setVertexBuffer(0, &tvb);
    enc->setIndexBuffer(&tib);
    // 先用 HLSL 反射名绑定（s_textTexture），再用通用名绑定（s_text）；二者任意其一生效即可
    enc->setTexture(0, m_sText, m_atlasTex,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_DEPTH_TEST_ALWAYS);
    if (m_hasClip) {
        enc->setScissor(m_clipX, m_clipY, m_clipW, m_clipH);
    }
    // 降噪：绘制统计仅在调试时需要
    enc->submit(viewId, m_prog);
    bgfx::end(enc);
}

void TextRenderer::setClipRect(int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    m_hasClip = true; m_clipX = x; m_clipY = y; m_clipW = w; m_clipH = h;
}

void TextRenderer::clearClipRect()
{
    m_hasClip = false;
}

bool TextRenderer::ensureFontReady()
{
    if (m_font.face) return true;
    if (!m_fontRef.get() || m_requestedFontPx <= 0) return false;
    if (m_fontRef.get()->getState() != Tina::Engine::Resource::State::READY) return false;

    if (!m_fontRef.get()->ensureFace(m_requestedFontPx)) return false;
    FT_Face face = m_fontRef.get()->getFace(m_requestedFontPx);
    if (!face) return false;
    m_font.face = face;
    m_font.sizePx = m_requestedFontPx;
    m_font.ascender = (int)(face->size->metrics.ascender >> 6);
    m_font.descender = (int)(face->size->metrics.descender >> 6);
    return true;
}

void TextRenderer::measureText(const std::string& utf8, float& outWidth, float& outHeight)
{
    outWidth = 0.0f; outHeight = 0.0f;
    if (utf8.empty()) return;
    if (!ensureFontReady()) return;

    float lineW = 0.0f;
    int lines = 1;
    const char* p = utf8.data();
    const char* end = p + utf8.size();
    int code = 0;
    const bool hasKerning = (m_font.face && FT_HAS_KERNING(m_font.face));
    FT_UInt prevGlyphIdx = 0;

    while (utf8Next(p, end, code)) {
        if (code == '\n') {
            if (lineW > outWidth) outWidth = lineW;
            lineW = 0.0f;
            ++lines;
            prevGlyphIdx = 0;
            continue;
        }
        if (!ensureGlyph(m_font, code)) continue;
        if (hasKerning) {
            FT_UInt glyphIdx = FT_Get_Char_Index(m_font.face, (FT_ULong)code);
            if (prevGlyphIdx != 0 && glyphIdx != 0) {
                FT_Vector delta{};
                if (FT_Get_Kerning(m_font.face, prevGlyphIdx, glyphIdx, FT_KERNING_DEFAULT, &delta) == 0) {
                    lineW += (float)(delta.x >> 6);
                }
            }
            prevGlyphIdx = glyphIdx;
        }
        const Glyph& g = m_font.glyphs[code];
        lineW += (float)g.advance;
    }
    if (lineW > outWidth) outWidth = lineW;
    outHeight = (float)(lines * m_font.sizePx);
}

void TextRenderer::measureTextExtents(const std::string& utf8,
                                      float& outWidth, float& outHeight,
                                      float& outTop, float& outBottom) const
{
    outWidth = 0.0f; outHeight = 0.0f; outTop = 0.0f; outBottom = 0.0f;
    if (utf8.empty()) return;
    if (!const_cast<TextRenderer*>(this)->ensureFontReady()) return;

    float lineW = 0.0f; int lines = 1;
    float topMax = 0.0f, bottomMax = 0.0f;
    const char* p = utf8.data();
    const char* end = p + utf8.size();
    int code = 0;
    const bool hasKerning = (m_font.face && FT_HAS_KERNING(m_font.face));
    FT_UInt prevGlyphIdx = 0;

    while (utf8Next(p, end, code)) {
        if (code == '\n') {
            if (lineW > outWidth) outWidth = lineW;
            lineW = 0.0f; ++lines; prevGlyphIdx = 0;
            continue;
        }
        // 确保已有字形（const_cast 以复用 ensureGlyph）
        const_cast<TextRenderer*>(this)->ensureGlyph(const_cast<Font&>(m_font), code);
        auto it = m_font.glyphs.find(code);
        if (it == m_font.glyphs.end()) continue;
        const Glyph& g = it->second;

        if (hasKerning) {
            FT_UInt glyphIdx = FT_Get_Char_Index(m_font.face, (FT_ULong)code);
            if (prevGlyphIdx != 0 && glyphIdx != 0) {
                FT_Vector delta{};
                if (FT_Get_Kerning(m_font.face, prevGlyphIdx, glyphIdx, FT_KERNING_DEFAULT, &delta) == 0) {
                    lineW += (float)(delta.x >> 6);
                }
            }
            prevGlyphIdx = glyphIdx;
        }

        lineW += (float)g.advance;
        // 基于字形 bitmap 尺寸与 bearingY 估计相对基线的上下范围
        float top = (float)g.bearingY;
        float bottom = (float)g.h - (float)g.bearingY;
        if (top > topMax) topMax = top;
        if (bottom > bottomMax) bottomMax = bottom;
    }
    if (lineW > outWidth) outWidth = lineW;
    outHeight = (float)(lines * m_font.sizePx);
    outTop = topMax; outBottom = bottomMax;
}

} // namespace Tina::UI

