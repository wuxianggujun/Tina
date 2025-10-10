//
// UIRenderer - 批处理优化版本
// 性能提升：100个控件从100 drawcalls降至2-5个
//

#include "UICore.hpp"
#include <bx/math.h>
#include <cstring>
#include <algorithm>
#include "../core/Color.hpp"
#include "../core/Log.hpp"

namespace Tina::UI {

bool UIRenderer::initialize(Tina::Renderer::ShaderManager& sm, TextRenderer* text)
{
    m_progColor = sm.loadProgram("color", "color");
    if (!bgfx::isValid(m_progColor)) {
        TINA_ERROR("UIRenderer: 无法加载color着色器！");
        return false;
    }
    
    TINA_INFO("UIRenderer: color着色器加载成功 (handle: {})", m_progColor.idx);
    
    m_colorLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Float)
    .end();
    m_text = text;

    // sprite 程序与布局
    m_progSprite = sm.loadProgram("sprite", "sprite");
    if (bgfx::isValid(m_progSprite)) {
        TINA_INFO("UIRenderer: sprite着色器加载成功 (handle: {})", m_progSprite.idx);
        m_spriteLayout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Float)
        .end();
        m_sTex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    } else {
        TINA_WARN("UIRenderer: sprite着色器加载失败");
    }
    
    TINA_INFO("UIRenderer: 初始化完成");
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
    // 保存上一帧统计，重置当前帧
    if (m_statsEnabled) {
        m_lastFrameStats = m_frameStats;
        m_frameStats.reset();
    }
    
    // 清理上一帧的缓冲
    m_colorBatch.clear();
    m_spriteBatches.clear();
    m_currentViewId = viewId;
}

// 批处理版本：缓存而不是立即提交
void UIRenderer::drawRect(uint16_t viewId, float x, float y, float w, float h,
                          float r, float g, float b, float a)
{
    if (!bgfx::isValid(m_progColor)) {
        TINA_ERROR("UIRenderer::drawRect - color着色器无效!");
        return;
    }
    
    if (w <= 0.0f || h <= 0.0f) return;
    
    // ✅ 检查索引溢出：如果添加4个顶点会超过uint16_t最大值，先flush
    if (m_colorBatch.vertices.size() + 4 > 65536) {
        flushColorBatch();
    }
    
    // 检查是否需要创建新批次（防止单个批次过大）
    if (shouldCreateNewBatch(m_colorBatch.vertices.size(), 4)) {
        // 如果当前批次已经很大，先flush再继续
        flushColorBatch();
    }

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
    
    // 更新统计
    if (m_statsEnabled) {
        m_frameStats.rectCount++;
    }
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

    // 找到或创建匹配纹理的批次（考虑深度）
    SpriteBatch* batch = findOrCreateSpriteBatch(tex, m_currentRenderDepth);
    if (!batch) return;
    
    // ✅ 检查索引溢出
    if (batch->vertices.size() + 4 > 65536) {
        // 当前批次已满，创建新批次
        SpriteBatch newBatch;
        newBatch.texture = tex;
        newBatch.currentDepth = m_currentRenderDepth;
        m_spriteBatches.push_back(newBatch);
        batch = &m_spriteBatches.back();
    }
    
    // 检查是否需要创建新批次
    if (shouldCreateNewBatch(batch->vertices.size(), 4)) {
        // 创建新的批次
        SpriteBatch newBatch;
        newBatch.texture = tex;
        newBatch.currentDepth = m_currentRenderDepth;
        m_spriteBatches.push_back(newBatch);
        batch = &m_spriteBatches.back();
    }

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
    
    // 更新统计
    if (m_statsEnabled) {
        m_frameStats.imageCount++;
    }
}

// 统一提交所有批次
void UIRenderer::flush()
{
    // 根据策略进行排序
    if (m_batchStrategy == BatchStrategy::DepthSorted) {
        sortBatchesByDepth();
    }
    
    flushColorBatch();
    flushSpriteBatches();
    flushTextCommands();
    
    // 计算批处理效率
    if (m_statsEnabled) {
        m_frameStats.calculate();
    }
    
    // 重置深度计数器
    m_currentRenderDepth = 0;
    
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
    if (m_colorBatch.vertices.empty()) {
        // 批次为空，正常情况
        return;
    }
    
    if (!bgfx::isValid(m_progColor)) {
        TINA_ERROR("UIRenderer: color着色器无效！无法渲染UI背景");
        return;
    }

    const uint32_t vcount = static_cast<uint32_t>(m_colorBatch.vertices.size());
    const uint32_t icount = static_cast<uint32_t>(m_colorBatch.indices.size());
    
    // ✅ 移除性能日志，改为TRACE级别（默认不输出）
    TINA_TRACE("UIRenderer: flushColorBatch - 提交 {} 个矩形", vcount / 4);
    
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
        
        // 更新统计
        if (m_statsEnabled) {
            m_frameStats.drawCalls++;
            m_frameStats.vertices += vcount;
            m_frameStats.triangles += icount / 3;
        }
    }
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
            
            // 更新统计
            if (m_statsEnabled) {
                m_frameStats.drawCalls++;
                m_frameStats.vertices += vcount;
                m_frameStats.triangles += icount / 3;
            }
        }
    }
}

// 查找或创建纹理批次（考虑深度）
UIRenderer::SpriteBatch* UIRenderer::findOrCreateSpriteBatch(bgfx::TextureHandle tex, uint32_t depth)
{
    // 根据批处理策略查找合适的批次
    for (auto& batch : m_spriteBatches) {
        if (batch.canMerge(tex, depth)) {
            // 简单策略：只要纹理相同就合并
            // 深度排序策略：纹理和深度都相同才合并
            if (m_batchStrategy == BatchStrategy::DepthSorted) {
                if (batch.currentDepth == depth) {
                    return &batch;
                }
            } else {
                return &batch;
            }
        }
    }
    
    // 创建新批次
    SpriteBatch newBatch;
    newBatch.texture = tex;
    newBatch.currentDepth = depth;
    m_spriteBatches.push_back(newBatch);
    return &m_spriteBatches.back();
}

// 根据深度排序批次
void UIRenderer::sortBatchesByDepth()
{
    // 对sprite批次按深度排序
    std::sort(m_spriteBatches.begin(), m_spriteBatches.end(),
        [](const SpriteBatch& a, const SpriteBatch& b) {
            return a.currentDepth < b.currentDepth;
        });
    
    // 如果需要，也可以对批次内的元素按深度排序
    // 但通常批次级别的排序就足够了
}

// 判断是否需要创建新批次
bool UIRenderer::shouldCreateNewBatch(uint32_t currentSize, uint32_t newSize) const
{
    // 如果加入新元素后超过最大批次大小，则需要新批次
    return (currentSize + newSize) > m_maxBatchVertices;
}

// 顶层文本：延迟到 flush() 统一提交
void UIRenderer::flushTextCommands()
{
    if (!m_text || m_textCmds.empty()) return;
    
    // ✅ 使用TRACE级别日志
    TINA_TRACE("UIRenderer: flushTextCommands - 提交 {} 条文本命令", (int)m_textCmds.size());
    
    if (m_statsEnabled) {
        m_frameStats.textCount += static_cast<uint32_t>(m_textCmds.size());
        // 文本渲染的drawcall由TextRenderer内部处理，这里估算
        m_frameStats.drawCalls += static_cast<uint32_t>(m_textCmds.size());
    }
    
    for (const auto& cmd : m_textCmds) {
        int prevPx = m_text->currentFontPx();
        bool needRestore = false;
        if (cmd.fontPx > 0 && cmd.fontPx != prevPx) {
            if (m_text->setFontPx(cmd.fontPx)) {
                needRestore = true;
            }
        }
        // 小字号使用线性过滤，提升可读性；否则使用点采样保持锐利
        bool prevFilter = m_text->linearFilter();
        bool wantLinear = (cmd.fontPx > 0 ? cmd.fontPx : prevPx) <= 24;
        if (wantLinear != prevFilter) {
            m_text->setLinearFilter(wantLinear);
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
        // 恢复过滤模式
        if (m_text->linearFilter() != prevFilter) {
            m_text->setLinearFilter(prevFilter);
        }
    }
}


// 打印性能统计到日志
void UIRenderer::logStats() const
{
    if (!m_statsEnabled) {
        TINA_WARN("UIRenderer: 性能统计未启用");
        return;
    }
    
    const auto& stats = m_lastFrameStats;
    TINA_INFO("=== UI渲染性能统计 ===");
    TINA_INFO("Draw Calls: {}", stats.drawCalls);
    TINA_INFO("顶点数: {}", stats.vertices);
    TINA_INFO("三角形数: {}", stats.triangles);
    TINA_INFO("矩形数: {}", stats.rectCount);
    TINA_INFO("图片数: {}", stats.imageCount);
    TINA_INFO("文本数: {}", stats.textCount);
    TINA_INFO("批处理效率: {:.1f}%", stats.batchEfficiency * 100.0f);
    TINA_INFO("====================");
}

} // namespace Tina::UI
