//
// UIRenderer - 批处理优化版本
// 性能提升：100个控件从100 drawcalls降至2-5个
//

#include "UICore.hpp"
#include <bx/math.h>
#include <cstring>
#include "../core/Color.hpp"
#include "../core/Log.hpp"

namespace Tina::UI {

bool UIRenderer::initialize(Tina::Renderer::ShaderManager& sm, TextRenderer* text)
{
    m_progColor = sm.loadProgram("color", "color");
    if (!bgfx::isValid(m_progColor)) return false;
    m_colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Float)
    .end();
    m_text = text;

    // sprite 程序与布局
    m_progSprite = sm.loadProgram("sprite", "sprite");
    if (bgfx::isValid(m_progSprite)) {
        m_spriteLayout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Float)
        .end();
        m_sTex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    }
    return true;
}

void UIRenderer::shutdown()
{
    // 程序句柄由 ShaderManager 统一管理与清理，这里无需销毁
    if (bgfx::isValid(m_sTex)) { bgfx::destroy(m_sTex); m_sTex = BGFX_INVALID_HANDLE; }
    
    // 清理批处理缓冲
    m_colorBatch.clear();
    m_spriteBatches.clear();
}

void UIRenderer::beginFrame(uint16_t viewId)
{
    // 清理上一帧的缓冲
    m_colorBatch.clear();
    m_spriteBatches.clear();
    m_currentViewId = viewId;
}

// 批处理版本：缓存而不是立即提交
void UIRenderer::drawRect(uint16_t viewId, float x, float y, float w, float h,
                          float r, float g, float b, float a)
{
    if (!bgfx::isValid(m_progColor) || w <= 0.0f || h <= 0.0f) return;

    // 计算当前批次的基索引（已有顶点数）
    uint16_t baseIdx = static_cast<uint16_t>(m_colorBatch.vertices.size());
    
    // 添加4个顶点
    m_colorBatch.vertices.push_back({ x,     y,     0.0f, r,g,b,a });
    m_colorBatch.vertices.push_back({ x+w,   y,     0.0f, r,g,b,a });
    m_colorBatch.vertices.push_back({ x+w,   y+h,   0.0f, r,g,b,a });
    m_colorBatch.vertices.push_back({ x,     y+h,   0.0f, r,g,b,a });
    
    // 添加6个索引（2个三角形）
    m_colorBatch.indices.push_back(baseIdx + 0);
    m_colorBatch.indices.push_back(baseIdx + 1);
    m_colorBatch.indices.push_back(baseIdx + 2);
    m_colorBatch.indices.push_back(baseIdx + 0);
    m_colorBatch.indices.push_back(baseIdx + 2);
    m_colorBatch.indices.push_back(baseIdx + 3);
}

void UIRenderer::drawRect(uint16_t viewId, float x, float y, float w, float h,
                          const Tina::Core::Color& color)
{
    drawRect(viewId, x, y, w, h, color.r(), color.g(), color.b(), color.a());
}

void UIRenderer::drawText(uint16_t viewId, float x, float y,
                          const std::string& utf8,
                          const TextOptions& opts)
{
    if (!m_text) return;
    TextCmd cmd{}; cmd.viewId = viewId;
    cmd.x = x; cmd.y = y; cmd.r = opts.r; cmd.g = opts.g; cmd.b = opts.b; cmd.a = opts.a; cmd.text = utf8; cmd.fontPx = opts.fontPx;
    m_textCmds.push_back(std::move(cmd));
}

void UIRenderer::drawTextBox(uint16_t viewId, float x, float y, float w, float h,
                             const std::string& utf8,
                             const TextOptions& opts)
{
    if (!m_text || w <= 0.0f || h <= 0.0f) {
        if (!m_text) {
            TINA_WARN("UIRenderer: 未绑定 TextRenderer，文本无法绘制");
        }
        return;
    }
    TextCmd cmd{}; cmd.viewId = viewId;
    cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h; cmd.padX = opts.padX; cmd.padY = opts.padY;
    cmd.r = opts.r; cmd.g = opts.g; cmd.b = opts.b; cmd.a = opts.a; cmd.text = utf8; cmd.fontPx = opts.fontPx;
    cmd.hAlign = opts.hAlign; cmd.vAlign = opts.vAlign;
    m_textCmds.push_back(std::move(cmd));
}

// 批处理版本：缓存而不是立即提交
void UIRenderer::drawImage(uint16_t viewId, float x, float y, float w, float h,
                           bgfx::TextureHandle tex,
                           float r, float g, float b, float a)
{
    if (!bgfx::isValid(m_progSprite) || !bgfx::isValid(tex) || w <= 0.0f || h <= 0.0f) return;

    // 找到或创建匹配纹理的批次
    SpriteBatch* batch = findOrCreateSpriteBatch(tex);
    if (!batch) return;

    uint16_t baseIdx = static_cast<uint16_t>(batch->vertices.size());
    
    // 添加4个顶点（带UV坐标）
    batch->vertices.push_back({ x,     y,     0.0f, 0.0f, 0.0f, r,g,b,a });
    batch->vertices.push_back({ x+w,   y,     0.0f, 1.0f, 0.0f, r,g,b,a });
    batch->vertices.push_back({ x+w,   y+h,   0.0f, 1.0f, 1.0f, r,g,b,a });
    batch->vertices.push_back({ x,     y+h,   0.0f, 0.0f, 1.0f, r,g,b,a });
    
    // 添加6个索引
    batch->indices.push_back(baseIdx + 0);
    batch->indices.push_back(baseIdx + 1);
    batch->indices.push_back(baseIdx + 2);
    batch->indices.push_back(baseIdx + 0);
    batch->indices.push_back(baseIdx + 2);
    batch->indices.push_back(baseIdx + 3);
}

// 统一提交所有批次
void UIRenderer::flush()
{
    flushColorBatch();
    flushSpriteBatches();
    flushTextCommands();
    
    // 清理（为下一帧做准备，但保留内存容量）
    m_colorBatch.vertices.clear();
    m_colorBatch.indices.clear();
    for (auto& batch : m_spriteBatches) {
        batch.vertices.clear();
        batch.indices.clear();
    }
    m_spriteBatches.clear();
    m_textCmds.clear();
}

// 提交纯色批次
void UIRenderer::flushColorBatch()
{
    if (m_colorBatch.vertices.empty() || !bgfx::isValid(m_progColor)) return;

    const uint32_t vcount = static_cast<uint32_t>(m_colorBatch.vertices.size());
    const uint32_t icount = static_cast<uint32_t>(m_colorBatch.indices.size());
    
    TINA_INFO("UIRenderer: flushColorBatch - 提交 {} 个矩形", vcount / 4);
    
    // 检查瞬态缓冲区容量
    if (bgfx::getAvailTransientVertexBuffer(vcount, m_colorLayout) < vcount ||
        bgfx::getAvailTransientIndexBuffer(icount) < icount) {
        TINA_WARN("UIRenderer: 瞬态缓冲区不足，跳过 {} 个纯色矩形", vcount / 4);
        return;
    }
    
    // 分配并填充瞬态缓冲
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientVertexBuffer(&tvb, vcount, m_colorLayout);
    bgfx::allocTransientIndexBuffer(&tib, icount);
    
    std::memcpy(tvb.data, m_colorBatch.vertices.data(), vcount * sizeof(ColorVtx));
    std::memcpy(tib.data, m_colorBatch.indices.data(), icount * sizeof(uint16_t));
    
    // 提交单个批次（合并了所有矩形）
    bgfx::Encoder* enc = bgfx::begin(false);  // 不排序，按顺序渲染
    if (enc) {
        float mtx[16];
        bx::mtxIdentity(mtx);
        enc->setTransform(mtx);  // 显式设置单位矩阵
        enc->setVertexBuffer(0, &tvb);
        enc->setIndexBuffer(&tib);
        enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        enc->submit(m_currentViewId, m_progColor);
        bgfx::end(enc);
    }
    
    // 性能日志（调试用）
    // TINA_INFO("UIRenderer: 纯色批次提交 {} 个矩形，1个drawcall", vcount / 4);
}

// 提交所有纹理批次
void UIRenderer::flushSpriteBatches()
{
    if (!bgfx::isValid(m_progSprite)) return;

    for (auto& batch : m_spriteBatches) {
        if (batch.vertices.empty()) continue;

        const uint32_t vcount = static_cast<uint32_t>(batch.vertices.size());
        const uint32_t icount = static_cast<uint32_t>(batch.indices.size());
        
        if (bgfx::getAvailTransientVertexBuffer(vcount, m_spriteLayout) < vcount ||
            bgfx::getAvailTransientIndexBuffer(icount) < icount) {
            TINA_WARN("UIRenderer: 瞬态缓冲区不足，跳过 {} 个图片", vcount / 4);
            continue;
        }
        
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, vcount, m_spriteLayout);
        bgfx::allocTransientIndexBuffer(&tib, icount);
        
        std::memcpy(tvb.data, batch.vertices.data(), vcount * sizeof(SpriteVtx));
        std::memcpy(tib.data, batch.indices.data(), icount * sizeof(uint16_t));
        
        bgfx::Encoder* enc = bgfx::begin(false);  // 不排序，按顺序渲染
        if (enc) {
            float mtx[16];
            bx::mtxIdentity(mtx);
            enc->setTransform(mtx);  // 显式设置单位矩阵
            enc->setVertexBuffer(0, &tvb);
            enc->setIndexBuffer(&tib);
            enc->setTexture(0, m_sTex, batch.texture,
                BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
            enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
            enc->submit(m_currentViewId, m_progSprite);
            bgfx::end(enc);
        }
    }
}

// 查找或创建纹理批次
UIRenderer::SpriteBatch* UIRenderer::findOrCreateSpriteBatch(bgfx::TextureHandle tex)
{
    // 查找是否已有相同纹理的批次
    for (auto& batch : m_spriteBatches) {
        if (batch.texture.idx == tex.idx) {
            return &batch;
        }
    }
    
    // 创建新批次
    SpriteBatch newBatch;
    newBatch.texture = tex;
    m_spriteBatches.push_back(newBatch);
    return &m_spriteBatches.back();
}

// 顶层文本：延迟到 flush() 统一提交
void UIRenderer::flushTextCommands()
{
    if (!m_text || m_textCmds.empty()) return;
    TINA_INFO("UIRenderer: flushTextCommands - 提交 {} 条文本命令", (int)m_textCmds.size());
    for (const auto& cmd : m_textCmds) {
        int prevPx = m_text->currentFontPx();
        bool needRestore = false;
        if (cmd.fontPx > 0 && cmd.fontPx != prevPx) {
            if (m_text->setFontPx(cmd.fontPx)) {
                needRestore = true;
            }
        }
        if (!(cmd.w > 0.0f && cmd.h > 0.0f)) {
            m_text->drawText(cmd.viewId, cmd.x, cmd.y, cmd.r, cmd.g, cmd.b, cmd.a, cmd.text);
        } else {
            float tw=0.0f, th=0.0f, tTop=0.0f, tBottom=0.0f;
            m_text->measureTextExtents(cmd.text, tw, th, tTop, tBottom);
            float textX = cmd.x + cmd.padX;
            if (cmd.hAlign == AlignH::Center) textX = cmd.x + (cmd.w - tw)*0.5f;
            else if (cmd.hAlign == AlignH::Right) textX = cmd.x + cmd.w - cmd.padX - tw;
            float baselineY = cmd.y + cmd.padY + tTop;
            if (cmd.vAlign == AlignV::Center) {
                baselineY = cmd.y + cmd.h*0.5f + (tTop - tBottom)*0.5f;
            } else if (cmd.vAlign == AlignV::Bottom) {
                baselineY = cmd.y + cmd.h - cmd.padY - tBottom;
            } else if (cmd.vAlign == AlignV::Baseline) {
                baselineY = cmd.y + cmd.h - cmd.padY;
            }
            float textY = baselineY - (float)m_text->ascenderPx();
            m_text->drawText(cmd.viewId, textX, textY, cmd.r, cmd.g, cmd.b, cmd.a, cmd.text);
        }
        if (needRestore) {
            m_text->setFontPx(prevPx);
        }
    }
}


} // namespace Tina::UI
