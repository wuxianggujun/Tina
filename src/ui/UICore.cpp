//
// UIRenderer - 批处理优化版本
// 性能提升：100个控件从100 drawcalls降至2-5个
//

#include "UICore.hpp"
#include "UIConstants.hpp"  // 添加常量定义
#include "BatchStrategy.hpp" // 添加批处理策略
#include <bx/math.h>
#include <cstring>
#include <algorithm>
#include "../core/Color.hpp"
#include "../core/Log.hpp"
#include "../core/Memory.hpp"

namespace Tina::UI {

// 析构函数定义（需要在这里，因为IBatchStrategy的完整定义在这里可见）
UIRenderer::~UIRenderer() = default;

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

    // 分层批处理在首次使用各层时分配内存；无需在此预分配全局缓冲

    // 初始化默认批处理策略
    m_defaultStrategy = Memory::MakeUnique<SimpleBatchStrategy>();
    m_batchStrategy = m_defaultStrategy.get();

    TINA_INFO("UIRenderer: 初始化完成（预分配内存：{}个顶点，{}个批次，策略：{}）",
              DEFAULT_COLOR_VERTEX_RESERVE, DEFAULT_SPRITE_BATCH_RESERVE,
              m_batchStrategy->getName());
    return true;
}

void UIRenderer::shutdown()
{
    // 程序句柄由 ShaderManager 统一管理与清理，这里无需销毁
    if (bgfx::isValid(m_sTex)) { bgfx::destroy(m_sTex); m_sTex = BGFX_INVALID_HANDLE; }
    
    // 清理批处理缓冲
    m_layers.clear();
}

void UIRenderer::beginFrame(uint16_t viewId)
{
    // 开始性能监控
    m_perfMonitor.beginFrame();

    // 分层批处理：新帧清理所有层并压入默认层0
    m_layers.clear();
    m_layerStack.clear();
    m_layerStack.push_back(0);
    m_currentViewId = viewId;

    // 性能优化：预检查瞬态缓冲区容量，避免每次绘制都检查
    m_availableTransientVB = bgfx::getAvailTransientVertexBuffer(MAX_VERTICES_PER_BATCH, m_colorLayout);
    m_availableTransientIB = bgfx::getAvailTransientIndexBuffer(MAX_VERTICES_PER_BATCH * 3 / 2);  // 1.5倍索引

    if (m_availableTransientVB < 1000 || m_availableTransientIB < 1500) {
        TINA_WARN("UIRenderer: 瞬态缓冲区容量不足 (VB:{}, IB:{})",
                  m_availableTransientVB, m_availableTransientIB);
    }
}

// 批处理版本：缓存而不是立即提交
void UIRenderer::drawRect(uint16_t viewId, float x, float y, float w, float h,
                          float r, float g, float b, float a)
{
    if (!bgfx::isValid(m_progColor)) {
        reportError(UIErrorCode::ShaderInvalid,
                   "Color shader is invalid",
                   "drawRect");
        return;
    }
    
    if (w <= 0.0f || h <= 0.0f) return;
    
    auto& layer = m_layers[currentLayer()];
    ColorBatch& colorBatch = layer.color;

    // ✅ 检查索引溢出：如果添加顶点会超过uint16_t最大值，先flush本层
    if (colorBatch.vertices.size() + VERTICES_PER_RECT > MAX_VERTICES_PER_BATCH) {
        flushLayerColorBatch(layer);
    }

    // 检查是否需要创建新批次（防止单个批次过大）
    if (shouldCreateNewBatch(colorBatch.vertices.size(), VERTICES_PER_RECT)) {
        // 如果当前批次已经很大，先flush再继续（本层）
        flushLayerColorBatch(layer);
    }

    // 计算当前批次的基索引（已有顶点数）
    uint16_t baseIdx = static_cast<uint16_t>(colorBatch.vertices.size());

    // 添加4个顶点
    colorBatch.vertices.push_back({ x,     y,     0.0f, r,g,b,a });
    colorBatch.vertices.push_back({ x+w,   y,     0.0f, r,g,b,a });
    colorBatch.vertices.push_back({ x+w,   y+h,   0.0f, r,g,b,a });
    colorBatch.vertices.push_back({ x,     y+h,   0.0f, r,g,b,a });

    // 添加6个索引（2个三角形）
    colorBatch.indices.push_back(baseIdx + 0);
    colorBatch.indices.push_back(baseIdx + 1);
    colorBatch.indices.push_back(baseIdx + 2);
    colorBatch.indices.push_back(baseIdx + 0);
    colorBatch.indices.push_back(baseIdx + 2);
    colorBatch.indices.push_back(baseIdx + 3);
    
    // 更新统计
    m_perfMonitor.recordRect();
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
    // 记录当前裁剪状态
    cmd.hasClip = m_hasClip; cmd.clipX = m_clipX; cmd.clipY = m_clipY; cmd.clipW = m_clipW; cmd.clipH = m_clipH;
    m_layers[currentLayer()].texts.push_back(std::move(cmd));
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
    // 记录当前裁剪状态
    cmd.hasClip = m_hasClip; cmd.clipX = m_clipX; cmd.clipY = m_clipY; cmd.clipW = m_clipW; cmd.clipH = m_clipH;
    m_layers[currentLayer()].texts.push_back(std::move(cmd));
}

// 批处理版本：缓存而不是立即提交
void UIRenderer::drawImage(uint16_t viewId, float x, float y, float w, float h,
                           bgfx::TextureHandle tex,
                           float r, float g, float b, float a)
{
    if (!bgfx::isValid(m_progSprite) || !bgfx::isValid(tex) || w <= 0.0f || h <= 0.0f) return;

    // 找到或创建匹配纹理的批次（考虑深度） - 分层
    auto& layer = m_layers[currentLayer()];
    SpriteBatch* batch = findOrCreateSpriteBatch(layer.sprites, tex, m_currentRenderDepth);
    if (!batch) return;
    
    // ✅ 检查索引溢出
    if (batch->vertices.size() + VERTICES_PER_RECT > MAX_VERTICES_PER_BATCH) {
        // 当前批次已满，创建新批次
        SpriteBatch newBatch;
        newBatch.texture = tex;
        newBatch.currentDepth = m_currentRenderDepth;
        layer.sprites.push_back(newBatch);
        batch = &layer.sprites.back();
    }

    // 检查是否需要创建新批次
    if (shouldCreateNewBatch(batch->vertices.size(), VERTICES_PER_RECT)) {
        // 创建新的批次
        SpriteBatch newBatch;
        newBatch.texture = tex;
        newBatch.currentDepth = m_currentRenderDepth;
        layer.sprites.push_back(newBatch);
        batch = &layer.sprites.back();
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
    m_perfMonitor.recordImage();
}

// 统一提交所有批次（按层：先图元/图片，后文本）
void UIRenderer::flush()
{
    // 使用策略对每层的精灵批次进行排序，并按层提交
    for (auto& kv : m_layers) {
        auto& L = kv.second;
        if (m_batchStrategy) {
            m_batchStrategy->sortSpriteBatches(L.sprites);
        }
        flushLayerColorBatch(L);
        flushLayerSpriteBatches(L);
        flushLayerTextCommands(L);
    }
    
    // 结束性能监控
    m_perfMonitor.endFrame();
    
    // 重置深度计数器
    m_currentRenderDepth = 0;
    
    // 清理（为下一帧做准备）
    m_layers.clear();
}

// 提交当前层的纯色批次
void UIRenderer::flushLayerColorBatch(UIRenderer::LayerBatches& L)
{
    if (L.color.vertices.empty()) {
        // 批次为空，正常情况
        return;
    }
    
    if (!bgfx::isValid(m_progColor)) {
        reportError(UIErrorCode::ShaderInvalid,
                   "Color shader is invalid, cannot render UI background",
                   "flushColorBatch");
        return;
    }

    const uint32_t vcount = static_cast<uint32_t>(L.color.vertices.size());
    const uint32_t icount = static_cast<uint32_t>(L.color.indices.size());
    
    // ✅ 性能日志（已注释，避免刷屏）
    // TINA_TRACE("UIRenderer: flushColorBatch - 提交 {} 个矩形", vcount / 4);
    
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
    
    std::memcpy(tvb.data, L.color.vertices.data(), vcount * sizeof(ColorVtx));
    std::memcpy(tib.data, L.color.indices.data(), icount * sizeof(uint16_t));
    
    // 提交单个批次（合并了所有矩形）
    bgfx::Encoder* enc = bgfx::begin(false);  // 不排序，按顺序渲染
    if (enc) {
        float mtx[16];
        bx::mtxIdentity(mtx);
        enc->setTransform(mtx);  // 显式设置单位矩阵
        enc->setVertexBuffer(0, &tvb);
        enc->setIndexBuffer(&tib);
        enc->setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        // 应用裁剪区域（如有）
        if (m_hasClip) {
            enc->setScissor(m_clipX, m_clipY, m_clipW, m_clipH);
        }
        enc->submit(m_currentViewId, m_progColor);
        bgfx::end(enc);
        
        // 更新统计
        m_perfMonitor.recordDrawCall();
        m_perfMonitor.recordVertices(vcount);
        m_perfMonitor.recordTriangles(icount / 3);
    }
}

// 提交当前层的所有纹理批次
void UIRenderer::flushLayerSpriteBatches(UIRenderer::LayerBatches& L)
{
    if (!bgfx::isValid(m_progSprite)) return;

    for (auto& batch : L.sprites) {
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
            // 应用裁剪区域（如有）
            if (m_hasClip) {
                enc->setScissor(m_clipX, m_clipY, m_clipW, m_clipH);
            }
            enc->submit(m_currentViewId, m_progSprite);
            bgfx::end(enc);
            
            // 更新统计
            m_perfMonitor.recordDrawCall();
            m_perfMonitor.recordVertices(vcount);
            m_perfMonitor.recordTriangles(icount / 3);
        }
    }
}

// 分层 API 实现
void UIRenderer::pushLayer(int layer) {
    m_layerStack.push_back(layer);
}

void UIRenderer::popLayer() {
    if (!m_layerStack.empty()) m_layerStack.pop_back();
}

int UIRenderer::currentLayer() const {
    return m_layerStack.empty() ? 0 : m_layerStack.back();
}

// 在裁剪切换时，提交所有层的颜色与图片批次，避免不同裁剪混批
void UIRenderer::flushAllColorsAndSprites() {
    for (auto& kv : m_layers) {
        flushLayerColorBatch(kv.second);
        flushLayerSpriteBatches(kv.second);
    }
}

// 查找或创建纹理批次（考虑深度） - 分层版本
SpriteBatch* UIRenderer::findOrCreateSpriteBatch(Container::Vector<SpriteBatch>& batches, bgfx::TextureHandle tex, uint32_t depth)
{
    // 根据批处理策略查找合适的批次
    for (auto& batch : batches) {
        // 使用策略判断是否可以合并
        if (m_batchStrategy && m_batchStrategy->canMergeSprite(batch, tex, depth, 0)) {
            return &batch;
        }
    }
    
    // 创建新批次
    SpriteBatch newBatch;
    newBatch.texture = tex;
    newBatch.currentDepth = depth;
    batches.push_back(newBatch);
    return &batches.back();
}

// 判断是否需要创建新批次
bool UIRenderer::shouldCreateNewBatch(uint32_t currentSize, uint32_t newSize) const
{
    // 如果加入新元素后超过最大批次大小，则需要新批次
    return (currentSize + newSize) > m_maxBatchVertices;
}

// 文本：延迟到 flush()，按层提交
void UIRenderer::flushLayerTextCommands(UIRenderer::LayerBatches& L)
{
    if (!m_text || L.texts.empty()) return;
    
    // ✅ 性能日志（已注释，避免刷屏）
    // TINA_TRACE("UIRenderer: flushTextCommands - 提交 {} 条文本命令", (int)m_textCmds.size());
    
    // 记录文本统计
    for (size_t i = 0; i < L.texts.size(); ++i) {
        m_perfMonitor.recordText();
        m_perfMonitor.recordDrawCall();  // 文本渲染的drawcall估算
    }
    
    for (const auto& cmd : L.texts) {
        // 每条命令单独设置裁剪
        if (cmd.hasClip) m_text->setClipRect(cmd.clipX, cmd.clipY, cmd.clipW, cmd.clipH);
        else             m_text->clearClipRect();
        int prevPx = m_text->currentFontPx();
        bool needRestore = false;
        if (cmd.fontPx > 0 && cmd.fontPx != prevPx) {
            if (m_text->setFontPx(cmd.fontPx)) {
                needRestore = true;
            }
        }
        // 小字号使用线性过滤，提升可读性；否则使用点采样保持锐利
        bool prevFilter = m_text->linearFilter();
        bool wantLinear = (cmd.fontPx > 0 ? cmd.fontPx : prevPx) <= LINEAR_FILTER_FONT_SIZE_THRESHOLD;
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



// 内部错误报告实现
void UIRenderer::reportError(UIErrorCode code, const std::string& message, const std::string& location)
{
    m_lastError.setError(code, message, location);
    m_lastError.droppedItems++;

    // 使用错误处理器报告错误
    if (m_errorHandler) {
        m_errorHandler->onError(m_lastError);
    } else {
        m_defaultErrorHandler.onError(m_lastError);
    }
}

// 设置批处理策略
void UIRenderer::setBatchStrategy(IBatchStrategy* strategy)
{
    if (strategy) {
        m_batchStrategy = strategy;
        m_maxBatchVertices = strategy->getMaxBatchSize();
        TINA_INFO("UIRenderer: 切换到批处理策略 '{}' - {}",
                  strategy->getName(), strategy->getDescription());
    } else {
        // 恢复默认策略
        m_batchStrategy = m_defaultStrategy.get();
        m_maxBatchVertices = m_defaultStrategy->getMaxBatchSize();
        TINA_INFO("UIRenderer: 恢复默认批处理策略");
    }
}

// ==================== 裁剪栈实现 ====================

void UIRenderer::pushClip(float x, float y, float w, float h)
{
    // 像素对齐（避免半像素问题）
    int16_t clipX = static_cast<int16_t>(std::floor(x));
    int16_t clipY = static_cast<int16_t>(std::floor(y));
    uint16_t clipW = static_cast<uint16_t>(std::ceil(w));
    uint16_t clipH = static_cast<uint16_t>(std::ceil(h));

    // 如果栈不为空，与栈顶裁剪相交
    if (!m_clipStack.empty()) {
        const auto& parent = m_clipStack.back();
        if (parent.active) {
            // 计算相交区域
            int16_t x0 = std::max(clipX, parent.x);
            int16_t y0 = std::max(clipY, parent.y);
            int16_t x1 = std::min(static_cast<int16_t>(clipX + clipW),
                                 static_cast<int16_t>(parent.x + parent.w));
            int16_t y1 = std::min(static_cast<int16_t>(clipY + clipH),
                                 static_cast<int16_t>(parent.y + parent.h));

            // 如果相交区域无效，推入空裁剪
            if (x1 <= x0 || y1 <= y0) {
                m_clipStack.emplace_back();  // 非激活裁剪
                setClipRect(0, 0, 0, 0);
                return;
            }

            clipX = x0;
            clipY = y0;
            clipW = static_cast<uint16_t>(x1 - x0);
            clipH = static_cast<uint16_t>(y1 - y0);
        }
    }

    // 推入裁剪栈
    m_clipStack.emplace_back(clipX, clipY, clipW, clipH);

    // 设置硬件裁剪
    setClipRect(clipX, clipY, clipW, clipH);

    #ifdef TINA_UI_DEBUG_CLIP
    TINA_TRACE("UIRenderer::pushClip - 栈深度: {}, 裁剪区域: ({},{},{}x{})",
               m_clipStack.size(), clipX, clipY, clipW, clipH);
    #endif
}

void UIRenderer::popClip()
{
    if (m_clipStack.empty()) {
        TINA_WARN("UIRenderer::popClip - 裁剪栈为空，忽略");
        return;
    }

    // 弹出栈顶
    m_clipStack.pop_back();

    // 恢复父裁剪
    if (!m_clipStack.empty()) {
        const auto& parent = m_clipStack.back();
        if (parent.active) {
            setClipRect(parent.x, parent.y, parent.w, parent.h);
        } else {
            clearClipRect();
        }
    } else {
        // 栈空，清除裁剪
        clearClipRect();
    }

    #ifdef TINA_UI_DEBUG_CLIP
    TINA_TRACE("UIRenderer::popClip - 栈深度: {}", m_clipStack.size());
    #endif
}

void UIRenderer::drawRectClipped(uint16_t viewId, float x, float y, float w, float h,
                                  const Tina::Core::Color& color)
{
    // 如果没有裁剪，直接绘制
    if (!m_hasClip) {
        drawRect(viewId, x, y, w, h, color);
        return;
    }

    // 计算裁剪后的矩形
    float x0 = std::max(x, static_cast<float>(m_clipX));
    float y0 = std::max(y, static_cast<float>(m_clipY));
    float x1 = std::min(x + w, static_cast<float>(m_clipX + m_clipW));
    float y1 = std::min(y + h, static_cast<float>(m_clipY + m_clipH));

    // 如果完全在裁剪区域外，跳过
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    // 绘制裁剪后的矩形
    drawRect(viewId, x0, y0, x1 - x0, y1 - y0, color);
}

} // namespace Tina::UI
