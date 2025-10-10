//
// RenderQueue - 渲染队列系统
// 用途：收集、排序和批量执行渲染命令
//

#pragma once

#include "RenderCommand.hpp"
#include "../core/Container.hpp"
#include "../core/Memory.hpp"
#include <algorithm>
#include <unordered_map>

namespace Tina::Renderer {

// 前向声明
class ShaderManager;

// 渲染统计信息
struct RenderStats {
    uint32_t numCommands = 0;      // 命令总数
    uint32_t numDrawCalls = 0;     // Draw Call次数
    uint32_t numVertices = 0;      // 顶点总数
    uint32_t numTriangles = 0;     // 三角形总数
    uint32_t numTextureBinds = 0;  // 纹理绑定次数
    uint32_t numStateChanges = 0;  // 状态切换次数

    void reset() {
        numCommands = 0;
        numDrawCalls = 0;
        numVertices = 0;
        numTriangles = 0;
        numTextureBinds = 0;
        numStateChanges = 0;
    }
};

// 渲染批次
struct RenderBatch {
    RenderType type;
    bgfx::ProgramHandle program;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    BlendMode blendMode;

    // 批次顶点数据（动态增长）
    Container::Vector<uint8_t> vertexData;
    Container::Vector<uint16_t> indexData;
    bgfx::VertexLayout layout;

    uint32_t numVertices = 0;
    uint32_t numIndices = 0;

    // 是否可以合并新的命令到这个批次
    bool canMerge(const RenderCommand& cmd) const {
        if (type != cmd.type) return false;
        if (program.idx != cmd.program.idx) return false;
        if (blendMode != cmd.blendMode) return false;

        // 纹理相关类型需要检查纹理
        if (type == RenderType::Sprite || type == RenderType::Particle) {
            if (texture.idx != cmd.data.sprite.texture.idx) return false;
        }

        // 检查批次容量（防止索引溢出）
        const uint32_t MAX_VERTICES_PER_BATCH = 65000;  // 留一些余量
        const uint32_t verticesNeeded = (type == RenderType::Rectangle) ? 4 : 6;
        if (numVertices + verticesNeeded > MAX_VERTICES_PER_BATCH) return false;

        return true;
    }

    void clear() {
        vertexData.clear();
        indexData.clear();
        numVertices = 0;
        numIndices = 0;
    }
};

// 渲染队列
class RenderQueue {
public:
    RenderQueue();
    ~RenderQueue();

    // 初始化
    bool initialize(ShaderManager* shaders);
    void shutdown();

    // 帧管理
    void beginFrame();
    void endFrame();

    // 提交渲染命令
    void submit(const RenderCommand& cmd);
    void submit(RenderCommand&& cmd);

    // 批量提交
    void submitBatch(const RenderCommand* cmds, uint32_t count);

    // 执行渲染（排序、批处理、提交到GPU）
    void flush();

    // 清空队列
    void clear();

    // 获取统计信息
    const RenderStats& getStats() const { return m_stats; }

    // 设置最大批次大小
    void setMaxBatchSize(uint32_t size) { m_maxBatchSize = size; }

    // 启用/禁用批处理
    void setBatchingEnabled(bool enabled) { m_batchingEnabled = enabled; }

    // 启用/禁用自动排序
    void setSortingEnabled(bool enabled) { m_sortingEnabled = enabled; }

private:
    // 内部方法
    void sortCommands();
    void processBatches();
    void executeBatch(const RenderBatch& batch);
    void executeCommand(const RenderCommand& cmd);

    // 批处理相关
    void beginBatch(const RenderCommand& cmd);
    void addToBatch(const RenderCommand& cmd);
    void flushCurrentBatch();

    // 状态管理
    uint64_t getRenderState(BlendMode mode) const;
    void applyRenderState(const RenderCommand& cmd);

    // 顶点数据生成
    void generateRectVertices(const RectData& rect, uint8_t* vertices, uint16_t* indices, uint32_t baseVertex);
    void generateSpriteVertices(const SpriteData& sprite, uint8_t* vertices, uint16_t* indices, uint32_t baseVertex);

private:
    // 渲染命令队列
    Container::Vector<RenderCommand> m_commands;
    Container::Vector<RenderCommand> m_sortedCommands;  // 排序后的命令

    // 当前批次
    Memory::UniquePtr<RenderBatch> m_currentBatch;
    Container::Vector<RenderBatch> m_completedBatches;

    // 着色器管理器引用
    ShaderManager* m_shaders = nullptr;

    // 默认着色器程序
    bgfx::ProgramHandle m_progColor = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progSprite = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progText = BGFX_INVALID_HANDLE;

    // 顶点布局
    bgfx::VertexLayout m_layoutColor;
    bgfx::VertexLayout m_layoutSprite;
    bgfx::VertexLayout m_layoutText;

    // Uniform句柄
    bgfx::UniformHandle m_sTexture = BGFX_INVALID_HANDLE;

    // 状态缓存
    uint16_t m_currentViewId = UINT16_MAX;
    bgfx::TextureHandle m_currentTexture = BGFX_INVALID_HANDLE;
    uint64_t m_currentState = 0;

    // 配置
    uint32_t m_maxBatchSize = 1000;     // 每批次最大命令数
    bool m_batchingEnabled = true;      // 是否启用批处理
    bool m_sortingEnabled = true;       // 是否启用排序

    // 统计信息
    RenderStats m_stats;

    // 命令序列号（用于保持提交顺序）
    uint16_t m_sequenceCounter = 0;
};

} // namespace Tina::Renderer