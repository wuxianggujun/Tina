//
// BatchStrategy - UI批处理策略系统
// 职责：提供灵活的批处理策略，优化渲染性能
//

#pragma once

#include <vector>
#include <algorithm>
#include <bgfx/bgfx.h>
#include "UIConstants.hpp"
#include "UIBatch.hpp"
#include "../core/Container.hpp"

namespace Tina::UI {

// 批处理策略接口
class IBatchStrategy {
public:
    virtual ~IBatchStrategy() = default;

    // 判断是否可以合并到现有批次
    virtual bool canMergeSprite(const SpriteBatch& batch,
                                bgfx::TextureHandle tex,
                                uint32_t depth,
                                uint64_t stateFlags) const = 0;

    // 判断颜色批次是否可以合并
    virtual bool canMergeColor(const ColorBatch& batch,
                               uint32_t depth) const = 0;

    // 排序精灵批次
    virtual void sortSpriteBatches(Container::Vector<SpriteBatch>& batches) = 0;

    // 获取建议的最大批次大小
    virtual uint32_t getMaxBatchSize() const = 0;

    // 获取策略名称（用于调试）
    virtual const char* getName() const = 0;

    // 获取策略描述
    virtual const char* getDescription() const = 0;
};

// ==================== 具体策略实现 ====================

// 简单批处理策略（默认）
class SimpleBatchStrategy : public IBatchStrategy {
public:
    bool canMergeSprite(const SpriteBatch& batch,
                       bgfx::TextureHandle tex,
                       uint32_t depth,
                       uint64_t stateFlags) const override;

    bool canMergeColor(const ColorBatch& batch,
                      uint32_t depth) const override {
        // 简单策略：总是合并颜色批次
        return true;
    }

    void sortSpriteBatches(Container::Vector<SpriteBatch>& batches) override {
        // 简单策略：不排序
    }

    uint32_t getMaxBatchSize() const override {
        return MAX_VERTICES_PER_BATCH;
    }

    const char* getName() const override { return "Simple"; }

    const char* getDescription() const override {
        return "基础批处理策略，只要纹理相同就合并";
    }
};

// 深度优化批处理策略
class DepthOptimizedStrategy : public IBatchStrategy {
private:
    uint32_t m_depthTolerance;  // 深度容差

public:
    explicit DepthOptimizedStrategy(uint32_t tolerance = 0)
        : m_depthTolerance(tolerance) {}

    bool canMergeSprite(const SpriteBatch& batch,
                       bgfx::TextureHandle tex,
                       uint32_t depth,
                       uint64_t stateFlags) const override;

    bool canMergeColor(const ColorBatch& batch,
                      uint32_t depth) const override;

    void sortSpriteBatches(Container::Vector<SpriteBatch>& batches) override;

    uint32_t getMaxBatchSize() const override {
        return DEPTH_SORTED_MAX_VERTICES;  // 深度排序时使用较小批次
    }

    const char* getName() const override { return "DepthOptimized"; }

    const char* getDescription() const override {
        return "深度优化策略，按深度排序并允许一定深度容差";
    }

    // 设置深度容差
    void setDepthTolerance(uint32_t tolerance) { m_depthTolerance = tolerance; }
    uint32_t getDepthTolerance() const { return m_depthTolerance; }
};

// 状态变化最小化策略
class StateChangeMinimizedStrategy : public IBatchStrategy {
public:
    bool canMergeSprite(const SpriteBatch& batch,
                       bgfx::TextureHandle tex,
                       uint32_t depth,
                       uint64_t stateFlags) const override;

    bool canMergeColor(const ColorBatch& batch,
                      uint32_t depth) const override {
        // 状态优化策略：相同深度才合并
        return batch.currentDepth == depth;
    }

    void sortSpriteBatches(Container::Vector<SpriteBatch>& batches) override;

    uint32_t getMaxBatchSize() const override {
        return 16384;  // 状态优化时使用更小的批次
    }

    const char* getName() const override { return "StateChangeMinimized"; }

    const char* getDescription() const override {
        return "状态切换最小化策略，减少GPU状态变化";
    }
};

// 纹理图集优化策略（未来扩展）
class TextureAtlasStrategy : public IBatchStrategy {
private:
    bool m_atlasEnabled = false;

public:
    bool canMergeSprite(const SpriteBatch& batch,
                       bgfx::TextureHandle tex,
                       uint32_t depth,
                       uint64_t stateFlags) const override;

    bool canMergeColor(const ColorBatch& batch,
                      uint32_t depth) const override {
        return true;  // 颜色批次总是合并
    }

    void sortSpriteBatches(Container::Vector<SpriteBatch>& batches) override;

    uint32_t getMaxBatchSize() const override {
        return MAX_VERTICES_PER_BATCH;
    }

    const char* getName() const override { return "TextureAtlas"; }

    const char* getDescription() const override {
        return "纹理图集优化策略，支持将多个纹理合并到一个批次";
    }

    // 设置是否启用图集
    void setAtlasEnabled(bool enabled) { m_atlasEnabled = enabled; }
    bool isAtlasEnabled() const { return m_atlasEnabled; }
};

// 自适应批处理策略（根据性能动态调整）
class AdaptiveBatchStrategy : public IBatchStrategy {
private:
    uint32_t m_currentMaxSize = MAX_VERTICES_PER_BATCH;
    float m_lastFrameTime = 0.0f;
    float m_targetFrameTime = 16.0f;  // 目标60fps

public:
    bool canMergeSprite(const SpriteBatch& batch,
                       bgfx::TextureHandle tex,
                       uint32_t depth,
                       uint64_t stateFlags) const override;

    bool canMergeColor(const ColorBatch& batch,
                      uint32_t depth) const override {
        return true;
    }

    void sortSpriteBatches(Container::Vector<SpriteBatch>& batches) override;

    uint32_t getMaxBatchSize() const override {
        return m_currentMaxSize;
    }

    const char* getName() const override { return "Adaptive"; }

    const char* getDescription() const override {
        return "自适应策略，根据帧时间动态调整批次大小";
    }

    // 更新帧时间，自动调整批次大小
    void updateFrameTime(float frameTimeMs) {
        m_lastFrameTime = frameTimeMs;

        // 如果帧时间过长，减小批次大小
        if (frameTimeMs > m_targetFrameTime * 1.2f) {
            m_currentMaxSize = std::max(8192u, m_currentMaxSize * 3 / 4);
        }
        // 如果帧时间很短，增大批次大小
        else if (frameTimeMs < m_targetFrameTime * 0.8f) {
            m_currentMaxSize = std::min(MAX_VERTICES_PER_BATCH, m_currentMaxSize * 5 / 4);
        }
    }

    void setTargetFrameTime(float ms) { m_targetFrameTime = ms; }
};

} // namespace Tina::UI