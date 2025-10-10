//
// BatchStrategy - 批处理策略实现
//

#include "BatchStrategy.hpp"
#include "UIBatch.hpp"
#include <algorithm>

namespace Tina::UI {

// ==================== SimpleBatchStrategy ====================

bool SimpleBatchStrategy::canMergeSprite(const SpriteBatch& batch,
                                         bgfx::TextureHandle tex,
                                         uint32_t depth,
                                         uint64_t stateFlags) const {
    // 简单策略：只要纹理相同就合并
    return batch.texture.idx == tex.idx;
}

// ==================== DepthOptimizedStrategy ====================

bool DepthOptimizedStrategy::canMergeSprite(const SpriteBatch& batch,
                                           bgfx::TextureHandle tex,
                                           uint32_t depth,
                                           uint64_t stateFlags) const {
    if (batch.texture.idx != tex.idx) return false;

    // 允许一定的深度容差
    uint32_t depthDiff = (depth > batch.currentDepth) ?
                        (depth - batch.currentDepth) :
                        (batch.currentDepth - depth);

    return depthDiff <= m_depthTolerance;
}

bool DepthOptimizedStrategy::canMergeColor(const ColorBatch& batch,
                                          uint32_t depth) const {
    // 允许深度容差
    uint32_t depthDiff = (depth > batch.currentDepth) ?
                        (depth - batch.currentDepth) :
                        (batch.currentDepth - depth);

    return depthDiff <= m_depthTolerance;
}

void DepthOptimizedStrategy::sortSpriteBatches(Container::Vector<SpriteBatch>& batches) {
    // 按深度排序（从后到前）
    std::stable_sort(batches.begin(), batches.end(),
        [](const SpriteBatch& a, const SpriteBatch& b) {
            return a.currentDepth < b.currentDepth;
        });
}

// ==================== StateChangeMinimizedStrategy ====================

bool StateChangeMinimizedStrategy::canMergeSprite(const SpriteBatch& batch,
                                                 bgfx::TextureHandle tex,
                                                 uint32_t depth,
                                                 uint64_t stateFlags) const {
    // 纹理、状态标志和深度都相同才合并
    return batch.texture.idx == tex.idx &&
           batch.stateFlags == stateFlags &&
           batch.currentDepth == depth;
}

void StateChangeMinimizedStrategy::sortSpriteBatches(Container::Vector<SpriteBatch>& batches) {
    // 按状态签名排序，减少状态切换
    std::stable_sort(batches.begin(), batches.end(),
        [](const SpriteBatch& a, const SpriteBatch& b) {
            // 优先按纹理排序
            if (a.texture.idx != b.texture.idx) {
                return a.texture.idx < b.texture.idx;
            }
            // 然后按状态标志
            if (a.stateFlags != b.stateFlags) {
                return a.stateFlags < b.stateFlags;
            }
            // 最后按深度
            return a.currentDepth < b.currentDepth;
        });
}

// ==================== TextureAtlasStrategy ====================

bool TextureAtlasStrategy::canMergeSprite(const SpriteBatch& batch,
                                         bgfx::TextureHandle tex,
                                         uint32_t depth,
                                         uint64_t stateFlags) const {
    if (m_atlasEnabled) {
        // 如果启用了图集，可以合并不同纹理（需要UV坐标映射支持）
        // 这里简化处理，暂时还是按纹理分批
        return batch.texture.idx == tex.idx && batch.currentDepth == depth;
    } else {
        // 未启用图集时，行为与简单策略相同
        return batch.texture.idx == tex.idx;
    }
}

void TextureAtlasStrategy::sortSpriteBatches(Container::Vector<SpriteBatch>& batches) {
    if (m_atlasEnabled) {
        // 按深度排序，允许不同纹理在同一批次
        std::stable_sort(batches.begin(), batches.end(),
            [](const SpriteBatch& a, const SpriteBatch& b) {
                return a.currentDepth < b.currentDepth;
            });
    }
    // 否则不排序
}

// ==================== AdaptiveBatchStrategy ====================

bool AdaptiveBatchStrategy::canMergeSprite(const SpriteBatch& batch,
                                          bgfx::TextureHandle tex,
                                          uint32_t depth,
                                          uint64_t stateFlags) const {
    // 自适应策略：基于当前性能动态决定
    if (m_lastFrameTime > m_targetFrameTime) {
        // 性能不足，严格分批
        return batch.texture.idx == tex.idx &&
               batch.currentDepth == depth &&
               batch.vertices.size() < m_currentMaxSize / 2;
    } else {
        // 性能良好，宽松合并
        return batch.texture.idx == tex.idx &&
               batch.vertices.size() < m_currentMaxSize;
    }
}

void AdaptiveBatchStrategy::sortSpriteBatches(Container::Vector<SpriteBatch>& batches) {
    // 根据性能动态选择排序策略
    if (m_lastFrameTime > m_targetFrameTime * 1.5f) {
        // 性能很差，不排序以节省CPU
        return;
    }

    // 性能正常，进行基本排序
    std::stable_sort(batches.begin(), batches.end(),
        [](const SpriteBatch& a, const SpriteBatch& b) {
            // 按纹理分组，减少纹理切换
            if (a.texture.idx != b.texture.idx) {
                return a.texture.idx < b.texture.idx;
            }
            return a.currentDepth < b.currentDepth;
        });
}

} // namespace Tina::UI