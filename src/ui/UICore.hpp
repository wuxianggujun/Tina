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

// 性能统计结构
struct RenderStats {
    uint32_t drawCalls = 0;        // 绘制调用次数
    uint32_t vertices = 0;          // 顶点数
    uint32_t triangles = 0;         // 三角形数
    uint32_t rectCount = 0;         // 矩形数量
    uint32_t imageCount = 0;        // 图片数量
    uint32_t textCount = 0;         // 文本数量
    float batchEfficiency = 0.0f;  // 批处理效率 (1 - drawCalls/totalElements)
    
    void reset() {
        drawCalls = vertices = triangles = 0;
        rectCount = imageCount = textCount = 0;
        batchEfficiency = 0.0f;
    }
    
    void calculate() {
        uint32_t totalElements = rectCount + imageCount + textCount;
        if (totalElements > 0 && drawCalls > 0) {
            batchEfficiency = 1.0f - (float)drawCalls / (float)totalElements;
            batchEfficiency = std::max(0.0f, std::min(1.0f, batchEfficiency));
        }
    }
};

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

    // 统一文本绘制 API（其余旧 API 已移除）
    enum class AlignH { Left, Center, Right };
    enum class AlignV { Top, Center, Bottom, Baseline };
    struct TextOptions {
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        int fontPx = 0;                 // 0 表示使用当前字号
        AlignH hAlign = AlignH::Left;   // 仅用于 drawTextBox
        AlignV vAlign = AlignV::Top;    // 仅用于 drawTextBox
        float padX = 0.0f, padY = 0.0f; // 仅用于 drawTextBox
    };
    void drawText(uint16_t viewId, float x, float y,
                  const std::string& utf8,
                  const TextOptions& opts = {});
    void drawTextBox(uint16_t viewId, float x, float y, float w, float h,
                     const std::string& utf8,
                     const TextOptions& opts = {});

    // 批处理：统一提交所有缓存的绘制指令
    // 必须在所有 drawXXX 调用之后，每帧仅调用一次
    void beginFrame(uint16_t viewId);
    void flush();
    
    // RAII渲染作用域：自动管理beginFrame/flush，防止忘记调用
    // 使用方式：auto scope = renderer.beginRender(viewId);
    class RenderScope {
    public:
        RenderScope(UIRenderer& renderer, uint16_t viewId)
            : m_renderer(renderer) {
            m_renderer.beginFrame(viewId);
        }
        
        ~RenderScope() {
            m_renderer.flush();  // 析构时自动flush，异常安全
        }
        
        // 禁止复制和移动
        RenderScope(const RenderScope&) = delete;
        RenderScope& operator=(const RenderScope&) = delete;
        RenderScope(RenderScope&&) = delete;
        RenderScope& operator=(RenderScope&&) = delete;
        
    private:
        UIRenderer& m_renderer;
    };
    
    // 创建RAII作用域（推荐使用，自动管理）
    RenderScope beginRender(uint16_t viewId) {
        return RenderScope(*this, viewId);
    }

    // 文本测量（可指定字号；fontPx=0 使用当前字号）
    bool measureText(const std::string& utf8, float& outW, float& outH, int fontPx = 0) const {
        if (!m_text) { outW = outH = 0.0f; return false; }
        int prev = m_text->currentFontPx();
        if (fontPx > 0 && fontPx != prev) {
            if (!const_cast<TextRenderer*>(m_text)->setFontPx(fontPx)) return false;
            const_cast<TextRenderer*>(m_text)->measureText(utf8, outW, outH);
            const_cast<TextRenderer*>(m_text)->setFontPx(prev);
            return true;
        }
        const_cast<TextRenderer*>(m_text)->measureText(utf8, outW, outH);
        return true;
    }
    bool measureTextExtents(const std::string& utf8, float& outW, float& outH,
                            float& outTop, float& outBottom, int fontPx = 0) const {
        if (!m_text) { outW = outH = outTop = outBottom = 0.0f; return false; }
        int prev = m_text->currentFontPx();
        if (fontPx > 0 && fontPx != prev) {
            if (!const_cast<TextRenderer*>(m_text)->setFontPx(fontPx)) return false;
            const_cast<TextRenderer*>(m_text)->measureTextExtents(utf8, outW, outH, outTop, outBottom);
            const_cast<TextRenderer*>(m_text)->setFontPx(prev);
            return true;
        }
        const_cast<TextRenderer*>(m_text)->measureTextExtents(utf8, outW, outH, outTop, outBottom);
        return true;
    }
    int textAscenderPx() const { return m_text ? m_text->ascenderPx() : 0; }

    // 绘制纹理图片（像素坐标）。
    // 若未加载 sprite 程序或纹理无效，则忽略绘制。
    void drawImage(uint16_t viewId, float x, float y, float w, float h,
                   bgfx::TextureHandle tex,
                   float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    
    // === 批处理优化控制 ===
    
    // 设置批处理策略
    enum class BatchStrategy {
        Simple,         // 简单批处理（当前实现）
        DepthSorted,    // 深度排序批处理
        StateOptimized  // 状态切换优化批处理
    };
    void setBatchStrategy(BatchStrategy strategy) { m_batchStrategy = strategy; }
    BatchStrategy getBatchStrategy() const { return m_batchStrategy; }
    
    // 设置最大批次大小（防止单个批次过大）
    void setMaxBatchSize(uint32_t maxVertices) { m_maxBatchVertices = maxVertices; }
    uint32_t getMaxBatchSize() const { return m_maxBatchVertices; }
   
   // === 性能监控 ===
   
   // 获取当前帧的性能统计
   const RenderStats& getFrameStats() const { return m_frameStats; }
   
   // 获取上一帧的性能统计（用于显示）
   const RenderStats& getLastFrameStats() const { return m_lastFrameStats; }
   
   // 启用/禁用性能统计
   void setStatsEnabled(bool enabled) { m_statsEnabled = enabled; }
   bool isStatsEnabled() const { return m_statsEnabled; }
   
   // 调试：打印性能统计到日志
   void logStats() const;

private:
    // 顶点结构
    struct ColorVtx {
        float x, y, z;
        float r, g, b, a;
        // 注意：depth字段仅用于CPU端排序，不传递给GPU
    };
    struct SpriteVtx {
        float x, y, z;
        float u, v;
        float r, g, b, a;
        // 注意：depth字段仅用于CPU端排序，不传递给GPU
    };

    // 批处理结构（增强版）
    struct ColorBatch {
        Container::Vector<ColorVtx> vertices;
        Container::Vector<uint16_t> indices;
        uint32_t currentDepth = 0;  // 当前深度层级
        
        void clear() {
            vertices.clear();
            indices.clear();
            currentDepth = 0;
        }
        
        bool canMerge(uint32_t depth) const {
            // 简单策略：总是合并
            // 深度排序策略：只合并相同深度
            return true;  // 将在实现中根据策略判断
        }
    };
    struct SpriteBatch {
        Container::Vector<SpriteVtx> vertices;
        Container::Vector<uint16_t> indices;
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        uint32_t currentDepth = 0;  // 当前深度层级
        
        void clear() {
            vertices.clear();
            indices.clear();
            texture = BGFX_INVALID_HANDLE;
            currentDepth = 0;
        }
        
        bool canMerge(bgfx::TextureHandle tex, uint32_t depth) const {
            return texture.idx == tex.idx;  // 相同纹理才能合并
        }
    };

    // 文本命令（延后统一提交，保证文本在图元之上）
    struct TextCmd {
        uint16_t viewId = 0;
        // 通用
        float x = 0, y = 0;
        float r = 1, g = 1, b = 1, a = 1;
        std::string text;
        int fontPx = 0; // 0 表示使用当前全局字号
        // Box 参数（w/h>0 时启用）
        float w = 0, h = 0; float padX = 0, padY = 0;
        AlignH hAlign = AlignH::Left; AlignV vAlign = AlignV::Top;
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
    Container::Vector<TextCmd> m_textCmds;

    // 内部工具方法
    SpriteBatch* findOrCreateSpriteBatch(bgfx::TextureHandle tex, uint32_t depth = 0);
    void flushColorBatch();
    void flushSpriteBatches();
    void flushTextCommands();
    
    // 批处理优化
    void sortBatchesByDepth();
    bool shouldCreateNewBatch(uint32_t currentSize, uint32_t newSize) const;
    
    // 性能统计
    RenderStats m_frameStats;      // 当前帧统计
    RenderStats m_lastFrameStats;  // 上一帧统计（用于显示）
    bool m_statsEnabled = false;   // 是否启用统计
    
    // 批处理控制
    BatchStrategy m_batchStrategy = BatchStrategy::Simple;
    uint32_t m_maxBatchVertices = 65536;  // 默认最大64K顶点
    uint32_t m_currentRenderDepth = 0;    // 当前渲染深度
};

} // namespace Tina::UI
