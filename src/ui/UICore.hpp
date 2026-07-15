//
// 轻量 UI 绘制工具：矩形 + 文本
// - 依赖 bgfx + ShaderManager + TextRenderer
// - 面向视图 2（UI 视图，正交像素坐标）

#pragma once

#include <bgfx/bgfx.h>
#include <string>
#include <map>
#include "../renderer/ShaderManager.hpp"
#include "../core/Container.hpp"  // 包含 Optional 定义
#include "../core/Color.hpp"
#include "TextRenderer.hpp"
#include "UIConstants.hpp"  // 添加常量定义
#include "UIError.hpp"      // 添加错误处理
#include "UIBatch.hpp"      // 添加批处理数据结构
#include "BatchStrategy.hpp" // 添加批处理策略完整定义
#include "UIPerformanceMonitor.hpp" // 添加性能监控

namespace Tina::UI {

class UIRenderer {
public:
    UIRenderer() = default;
    ~UIRenderer();  // 将析构函数定义移到.cpp文件

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
        Container::Optional<int> fontPx;      // nullopt = 使用当前全局字号，有值 = 使用指定字号
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

    // === 渲染分层（按层递增顺序提交） ===
    // 用途：避免控件间手动 flush，通过层号控制覆盖关系。
    // 规则：同一层内先提交图元/图片，后提交文本；层号越大越靠上。
    void pushLayer(int layer);
    void popLayer();
    int currentLayer() const;

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

    // === 裁剪区域（Scissor）支持 ===
    // 设置/清除裁剪区域（像素坐标，原点左上）
    // 该裁剪区域会作用于本渲染器提交的所有批次与文本绘制
    void setClipRect(int16_t x, int16_t y, uint16_t w, uint16_t h) {
        // 当裁剪区域发生变化时，先提交当前批次，防止不同裁剪混批
        if (!m_hasClip || m_clipX != x || m_clipY != y || m_clipW != w || m_clipH != h) {
            // 提交当前已缓存的矩形/图片批次（使用旧裁剪）
            flushAllColorsAndSprites();
        }
        m_hasClip = true; m_clipX = x; m_clipY = y; m_clipW = w; m_clipH = h;
    }
    void clearClipRect() {
        if (m_hasClip) {
            // 清除裁剪前，提交当前批次（使用旧裁剪）
            flushAllColorsAndSprites();
        }
        m_hasClip = false;
    }

    // === 裁剪栈（嵌套裁剪支持） ===
    // 裁剪矩形结构
    struct ClipRect {
        int16_t x, y;
        uint16_t w, h;
        bool active;

        ClipRect() : x(0), y(0), w(0), h(0), active(false) {}
        ClipRect(int16_t x_, int16_t y_, uint16_t w_, uint16_t h_)
            : x(x_), y(y_), w(w_), h(h_), active(true) {}
    };

    // 推入裁剪区域（与当前裁剪相交）
    // 支持嵌套容器裁剪，自动处理裁剪相交
    void pushClip(float x, float y, float w, float h);

    // 弹出裁剪区域
    void popClip();

    // 获取当前裁剪栈深度
    size_t getClipStackDepth() const { return m_clipStack.size(); }

    // 便捷方法：绘制裁剪后的矩形（自动处理几何裁剪）
    void drawRectClipped(uint16_t viewId, float x, float y, float w, float h,
                         const Tina::Core::Color& color);

    // 文本测量（可指定字号；nullopt = 使用当前字号）
    // 注意：这些方法不是 const，因为内部需要临时修改字体大小
    bool measureText(const std::string& utf8, float& outW, float& outH, Container::Optional<int> fontPx = Container::nullopt) {
        if (!m_text) { outW = outH = 0.0f; return false; }
        int prev = m_text->currentFontPx();
        if (fontPx.has_value() && fontPx.value() != prev) {
            if (!m_text->setFontPx(fontPx.value())) return false;
            m_text->measureText(utf8, outW, outH);
            m_text->setFontPx(prev);
            return true;
        }
        m_text->measureText(utf8, outW, outH);
        return true;
    }
    bool measureTextExtents(const std::string& utf8, float& outW, float& outH,
                            float& outTop, float& outBottom, Container::Optional<int> fontPx = Container::nullopt) {
        if (!m_text) { outW = outH = outTop = outBottom = 0.0f; return false; }
        int prev = m_text->currentFontPx();
        if (fontPx.has_value() && fontPx.value() != prev) {
            if (!m_text->setFontPx(fontPx.value())) return false;
            m_text->measureTextExtents(utf8, outW, outH, outTop, outBottom);
            m_text->setFontPx(prev);
            return true;
        }
        m_text->measureTextExtents(utf8, outW, outH, outTop, outBottom);
        return true;
    }
    int textAscenderPx() const { return m_text ? m_text->ascenderPx() : 0; }

    // 绘制纹理图片（像素坐标）。
    // 若未加载 sprite 程序或纹理无效，则忽略绘制。
    void drawImage(uint16_t viewId, float x, float y, float w, float h,
                   bgfx::TextureHandle tex,
                   float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    
    // === 批处理优化控制 ===
    
    // 设置批处理策略（新的策略系统）
    void setBatchStrategy(IBatchStrategy* strategy);
    IBatchStrategy* getBatchStrategy() const { return m_batchStrategy; }
    
    // 设置最大批次大小（防止单个批次过大）
    void setMaxBatchSize(uint32_t maxVertices) { m_maxBatchVertices = maxVertices; }
    uint32_t getMaxBatchSize() const { return m_maxBatchVertices; }
   
   // === 性能监控 ===
   
   // 获取性能监控器
   UIPerformanceMonitor& getPerformanceMonitor() { return m_perfMonitor; }
   const UIPerformanceMonitor& getPerformanceMonitor() const { return m_perfMonitor; }
   
   // 便捷方法：启用/禁用性能监控
   void setPerformanceMonitoringEnabled(bool enabled) { m_perfMonitor.setEnabled(enabled); }
   bool isPerformanceMonitoringEnabled() const { return m_perfMonitor.isEnabled(); }

   // === 错误处理 ===

   // 获取最后的错误
   const UIError& getLastError() const { return m_lastError; }

   // 清除错误
   void clearError() { m_lastError.clear(); }

   // 设置错误处理器
   void setErrorHandler(IUIErrorHandler* handler) { m_errorHandler = handler; }

private:
    // 文本命令（延后统一提交，保证文本在图元之上）
    struct TextCmd {
        uint16_t viewId = 0;
        // 通用
        float x = 0, y = 0;
        float r = 1, g = 1, b = 1, a = 1;
        std::string text;
        Container::Optional<int> fontPx; // nullopt = 使用当前全局字号
        // Box 参数（w/h>0 时启用）
        float w = 0, h = 0; float padX = 0, padY = 0;
        AlignH hAlign = AlignH::Left; AlignV vAlign = AlignV::Top;
        // 每条文本命令的独立裁剪（避免受后续 setClipRect 影响）
        bool     hasClip = false;
        int16_t  clipX = 0;
        int16_t  clipY = 0;
        uint16_t clipW = 0;
        uint16_t clipH = 0;
    };

    bgfx::ProgramHandle m_progColor = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout  m_colorLayout{};
    TextRenderer*       m_text = nullptr;

    // sprite 绘制（贴图）
    bgfx::ProgramHandle m_progSprite = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout  m_spriteLayout{};
    bgfx::UniformHandle m_sTex = BGFX_INVALID_HANDLE;

    // 批处理缓冲（分层）
    struct LayerBatches { ColorBatch color; Container::Vector<SpriteBatch> sprites; Container::Vector<TextCmd> texts; };
    std::map<int, LayerBatches> m_layers;   // 按层号有序
    Container::Vector<int> m_layerStack;    // 层栈（支持嵌套）
    uint16_t m_currentViewId = 0;

    // 内部工具方法
    SpriteBatch* findOrCreateSpriteBatch(Container::Vector<SpriteBatch>& batches, bgfx::TextureHandle tex, uint32_t depth = 0);
    void flushLayerColorBatch(LayerBatches& L);
    void flushLayerSpriteBatches(LayerBatches& L);
    void flushLayerTextCommands(LayerBatches& L);
    void flushAllColorsAndSprites();
    
    // 批处理优化
    bool shouldCreateNewBatch(uint32_t currentSize, uint32_t newSize) const;
    
    // 性能监控
    UIPerformanceMonitor m_perfMonitor;

    // 批处理控制
    IBatchStrategy* m_batchStrategy = nullptr;  // 策略指针
    Memory::UniquePtr<IBatchStrategy> m_defaultStrategy;  // 默认策略
    uint32_t m_maxBatchVertices = DEFAULT_MAX_BATCH_VERTICES;  // 使用常量定义
    uint32_t m_currentRenderDepth = 0;    // 当前渲染深度

    // 性能优化：瞬态缓冲区容量缓存
    uint32_t m_availableTransientVB = 0;
    uint32_t m_availableTransientIB = 0;

    // 错误处理
    UIError m_lastError;
    IUIErrorHandler* m_errorHandler = nullptr;
    DefaultUIErrorHandler m_defaultErrorHandler;

    // 内部错误报告方法
    void reportError(UIErrorCode code, const std::string& message, const std::string& location);

    // === Scissor 状态 ===
    bool     m_hasClip = false;
    int16_t  m_clipX = 0;
    int16_t  m_clipY = 0;
    uint16_t m_clipW = 0;
    uint16_t m_clipH = 0;

    // 裁剪栈
    Container::Vector<ClipRect> m_clipStack;
};

} // namespace Tina::UI
