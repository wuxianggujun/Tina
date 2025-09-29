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

bool TextRenderer::initialize(int atlasW, int atlasH)
{
    if (FT_Init_FreeType(&m_ft)) {
        TINA_ERROR("TextRenderer: FreeType 初始化失败");
        return false;
    }

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
    m_sTextTexture = bgfx::createUniform("s_textTexture", bgfx::UniformType::Sampler);

    // 初始化：将图集初始填充为 RGBA = (255,255,255,128)，便于验证采样是否生效
    {
        const size_t pxCount = (size_t)m_atlasW * (size_t)m_atlasH;
        for (size_t i = 0; i < pxCount; ++i) {
            m_atlasPixels[i*4 + 0] = 255;
            m_atlasPixels[i*4 + 1] = 255;
            m_atlasPixels[i*4 + 2] = 255;
            m_atlasPixels[i*4 + 3] = 128;
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

    // 着色器
    m_shaderMgr.initialize();
    m_prog = m_shaderMgr.loadProgram("text", "text");
    if (!bgfx::isValid(m_prog)) {
        TINA_ERROR("TextRenderer: 加载 text 着色器失败");
        return false;
    }
    TINA_INFO("TextRenderer: 初始化完成，图集={}x{}，采样=PointClamp，程序有效={}",
              m_atlasW, m_atlasH, (int)bgfx::isValid(m_prog));
    return true;
}

void TextRenderer::shutdown()
{
    if (bgfx::isValid(m_atlasTex)) { bgfx::destroy(m_atlasTex); m_atlasTex = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_debugTex)) { bgfx::destroy(m_debugTex); m_debugTex = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_sText))    { bgfx::destroy(m_sText);    m_sText = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_sTextTexture)) { bgfx::destroy(m_sTextTexture); m_sTextTexture = BGFX_INVALID_HANDLE; }
    m_shaderMgr.cleanup();
    if (m_font.face) { FT_Done_Face(m_font.face); m_font.face = nullptr; }
    if (m_ft) { FT_Done_FreeType(m_ft); m_ft = nullptr; }
}

bool TextRenderer::loadFont(const std::string& path, int pixelSize)
{
    if (!m_ft) return false;
    if (m_font.face) { FT_Done_Face(m_font.face); m_font = {}; }
    if (FT_New_Face(m_ft, path.c_str(), 0, &m_font.face)) {
        TINA_ERROR("TextRenderer: 加载字体失败: {}", path);
        return false;
    }
    // 确保使用 Unicode 字符映射
    FT_Select_Charmap(m_font.face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(m_font.face, 0, (FT_UInt)pixelSize);
    m_font.sizePx = pixelSize;
    m_font.ascender = (int)(m_font.face->size->metrics.ascender >> 6);
    m_font.glyphs.clear();
    return true;
}

bool TextRenderer::ensureGlyph(Font& font, int codepoint)
{
    if (font.glyphs.find(codepoint) != font.glyphs.end()) return true;
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
    info.u0 = (float)dstX / (float)m_atlasW; info.v0 = (float)dstY / (float)m_atlasH;
    info.u1 = (float)(dstX + gw) / (float)m_atlasW; info.v1 = (float)(dstY + gh) / (float)m_atlasH;
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
    if (!m_font.face || !bgfx::isValid(m_prog) || !bgfx::isValid(m_atlasTex)) return;

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
        TINA_WARN("TextRenderer: 无可绘制字形，跳过绘制。缺失字形数={}，字符总数={}", missing, (int)utf8.size());
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, m_layout, (uint32_t)verts.size(), &tib, (uint32_t)idx.size()))
        return;
    memcpy(tvb.data, verts.data(), (size_t)verts.size() * sizeof(Vtx));
    memcpy(tib.data, idx.data(), (size_t)idx.size() * sizeof(uint16_t));

    // 额外日志：打印首个字形的四个顶点位置与 UV，辅助定位坐标系问题
    if (!verts.empty()) {
        const Vtx& v0 = verts[0];
        const Vtx& v1 = verts.size() > 1 ? verts[1] : verts[0];
        const Vtx& v2 = verts.size() > 2 ? verts[2] : verts[0];
        const Vtx& v3 = verts.size() > 3 ? verts[3] : verts[0];
        TINA_INFO("TextRenderer: 首字形 vtx0=({:.1f},{:.1f}) uv0=({:.3f},{:.3f}) vtx1=({:.1f},{:.1f}) uv1=({:.3f},{:.3f})",
                  v0.x, v0.y, v0.u, v0.v, v1.x, v1.y, v1.u, v1.v);
        TINA_INFO("TextRenderer: 首字形 vtx2=({:.1f},{:.1f}) uv2=({:.3f},{:.3f}) vtx3=({:.1f},{:.1f}) uv3=({:.3f},{:.3f})",
                  v2.x, v2.y, v2.u, v2.v, v3.x, v3.y, v3.u, v3.v);
    }

    bgfx::Encoder* enc = bgfx::begin(false);
    if (!enc) { TINA_WARN("TextRenderer: 无法获取 Encoder，放弃绘制"); return; }
    float mtx[16]; bx::mtxIdentity(mtx);
    enc->setTransform(mtx);
    enc->setVertexBuffer(0, &tvb);
    enc->setIndexBuffer(&tib);
    // 先用 HLSL 反射名绑定（s_textTexture），再用通用名绑定（s_text）；二者任意其一生效即可
    enc->setTexture(0, m_sTextTexture, m_atlasTex,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    enc->setTexture(0, m_sText, m_atlasTex,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_DEPTH_TEST_ALWAYS);
    if (m_hasClip) {
        enc->setScissor(m_clipX, m_clipY, m_clipW, m_clipH);
    }
    TINA_INFO("TextRenderer: 提交视图={} 顶点={} 索引={} 文本长度={} 缺失字形={}",
              (int)viewId, (int)verts.size(), (int)idx.size(), (int)utf8.size(), missing);
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

} // namespace Tina::UI
