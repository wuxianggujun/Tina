//
// RenderQueue 实现
//

#include "RenderQueue.hpp"
#include "ShaderManager.hpp"
#include "../core/Log.hpp"
#include "../core/STLCompat.hpp"  // 使用 STL 兼容层
#include <bgfx/bgfx.h>
#include <bx/math.h>

namespace Tina::Renderer {

// 顶点结构定义
struct ColorVertex {
    float x, y, z;
    float r, g, b, a;
};

struct SpriteVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

RenderQueue::RenderQueue() {
    m_commands.reserve(1000);
    m_sortIndices.reserve(1000);  // 预分配索引数组
    m_completedBatches.reserve(100);
}

RenderQueue::~RenderQueue() {
    shutdown();
}

bool RenderQueue::initialize(ShaderManager* shaders) {
    if (!shaders) {
        TINA_ERROR("RenderQueue: ShaderManager不能为空");
        return false;
    }

    m_shaders = shaders;

    // 加载默认着色器
    m_progColor = m_shaders->loadProgram("color", "color");
    m_progSprite = m_shaders->loadProgram("sprite", "sprite");

    // 初始化顶点布局
    m_layoutColor.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .end();

    m_layoutSprite.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .end();


    // 创建纹理采样器uniform
    m_sTexture = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

    TINA_INFO("RenderQueue: 初始化完成");
    return true;
}

void RenderQueue::shutdown() {
    clear();

    if (bgfx::isValid(m_sTexture)) {
        bgfx::destroy(m_sTexture);
        m_sTexture = BGFX_INVALID_HANDLE;
    }

    m_shaders = nullptr;
}

void RenderQueue::beginFrame() {
    clear();
    m_stats.reset();
    m_sequenceCounter = 0;
    m_currentViewId = UINT16_MAX;
    m_currentTexture = BGFX_INVALID_HANDLE;
    m_currentState = 0;
}

void RenderQueue::endFrame() {
    flush();
}

void RenderQueue::submit(const RenderCommand& cmd) {
    // 自动添加序列号以保持提交顺序
    RenderCommand modCmd = cmd;
    uint64_t sequenceMask = 0xFFFF;  // 保留低16位
    modCmd.sortKey = (cmd.sortKey & ~sequenceMask) | m_sequenceCounter++;

    m_commands.push_back(modCmd);
    m_stats.numCommands++;
}

void RenderQueue::submit(RenderCommand&& cmd) {
    uint64_t sequenceMask = 0xFFFF;
    cmd.sortKey = (cmd.sortKey & ~sequenceMask) | m_sequenceCounter++;

    m_commands.emplace_back(std::move(cmd));
    m_stats.numCommands++;
}

void RenderQueue::submitBatch(const RenderCommand* cmds, uint32_t count) {
    if (!cmds || count == 0) return;

    m_commands.reserve(m_commands.size() + count);
    for (uint32_t i = 0; i < count; ++i) {
        submit(cmds[i]);
    }
}

void RenderQueue::flush() {
    if (m_commands.empty()) return;

    // 1. 排序
    if (m_sortingEnabled) {
        sortCommands();
    }

    // 2. 批处理
    if (m_batchingEnabled) {
        processBatches();
    } else {
        // 直接执行每个命令（使用索引访问）
        size_t cmdCount = m_commands.size();
        for (size_t i = 0; i < cmdCount; ++i) {
            uint32_t cmdIndex = m_sortingEnabled && !m_sortIndices.empty()
                              ? m_sortIndices[i]
                              : static_cast<uint32_t>(i);
            executeCommand(m_commands[cmdIndex]);
        }
    }

    // 3. 清理
    clear();
}

void RenderQueue::clear() {
    m_commands.clear();
    m_sortIndices.clear();  // 清空索引数组
    m_completedBatches.clear();
    if (m_currentBatch) {
        m_currentBatch->clear();
    }
}

void RenderQueue::sortCommands() {
    // 索引排序优化：不复制命令，只排序索引
    size_t cmdCount = m_commands.size();

    // 初始化索引数组
    m_sortIndices.resize(cmdCount);
    for (size_t i = 0; i < cmdCount; ++i) {
        m_sortIndices[i] = static_cast<uint32_t>(i);
    }

    // 使用 EASTL 排序索引而不是命令本身
    // 这避免了复制大量数据，只需要移动 4 字节的索引
    Tina::stl::sort(m_sortIndices.begin(), m_sortIndices.end(),
                    [this](uint32_t a, uint32_t b) {
                        return m_commands[a].sortKey < m_commands[b].sortKey;
                    });
}

void RenderQueue::processBatches() {
    // 使用索引访问命令，避免复制
    size_t cmdCount = m_commands.size();

    for (size_t i = 0; i < cmdCount; ++i) {
        // 根据是否排序选择索引
        uint32_t cmdIndex = m_sortingEnabled && !m_sortIndices.empty()
                          ? m_sortIndices[i]
                          : static_cast<uint32_t>(i);

        const auto& cmd = m_commands[cmdIndex];

        // 检查是否可以合并到当前批次
        if (m_currentBatch && m_currentBatch->canMerge(cmd)) {
            addToBatch(cmd);
        } else {
            // 结束当前批次，开始新批次
            flushCurrentBatch();
            beginBatch(cmd);
            addToBatch(cmd);
        }
    }

    // 刷新最后的批次
    flushCurrentBatch();

    // 执行所有批次
    for (const auto& batch : m_completedBatches) {
        executeBatch(batch);
    }
}

void RenderQueue::beginBatch(const RenderCommand& cmd) {
    if (!m_currentBatch) {
        m_currentBatch = Memory::MakeUnique<RenderBatch>();
    }

    m_currentBatch->clear();
    m_currentBatch->viewId = cmd.viewId;
    m_currentBatch->type = cmd.type;
    m_currentBatch->program = cmd.program;
    m_currentBatch->blendMode = cmd.blendMode;

    // 设置布局
    switch (cmd.type) {
        case RenderType::Rectangle:
            m_currentBatch->layout = m_layoutColor;
            if (!bgfx::isValid(m_currentBatch->program)) {
                m_currentBatch->program = m_progColor;
            }
            break;
        case RenderType::Sprite:
            m_currentBatch->layout = m_layoutSprite;
            m_currentBatch->texture = cmd.data.sprite.texture;
            if (!bgfx::isValid(m_currentBatch->program)) {
                m_currentBatch->program = m_progSprite;
            }
            break;
        default:
            break;
    }

    // 预分配容量
    const uint32_t estimatedVertices = 100 * 4;  // 假设100个矩形
    const uint32_t estimatedIndices = 100 * 6;
    m_currentBatch->vertexData.reserve(estimatedVertices * m_currentBatch->layout.getStride());
    m_currentBatch->indexData.reserve(estimatedIndices);
}

void RenderQueue::addToBatch(const RenderCommand& cmd) {
    if (!m_currentBatch) return;

    uint32_t stride = m_currentBatch->layout.getStride();
    uint32_t baseVertex = m_currentBatch->numVertices;

    switch (cmd.type) {
        case RenderType::Rectangle: {
            // 增加4个顶点，6个索引
            size_t oldVertexSize = m_currentBatch->vertexData.size();
            size_t oldIndexSize = m_currentBatch->indexData.size();

            m_currentBatch->vertexData.resize(oldVertexSize + 4 * stride);
            m_currentBatch->indexData.resize(oldIndexSize + 6);

            generateRectVertices(cmd.data.rect,
                               m_currentBatch->vertexData.data() + oldVertexSize,
                               m_currentBatch->indexData.data() + oldIndexSize,
                               baseVertex);

            m_currentBatch->numVertices += 4;
            m_currentBatch->numIndices += 6;
            break;
        }
        case RenderType::Sprite: {
            size_t oldVertexSize = m_currentBatch->vertexData.size();
            size_t oldIndexSize = m_currentBatch->indexData.size();

            m_currentBatch->vertexData.resize(oldVertexSize + 4 * stride);
            m_currentBatch->indexData.resize(oldIndexSize + 6);

            generateSpriteVertices(cmd.data.sprite,
                                 m_currentBatch->vertexData.data() + oldVertexSize,
                                 m_currentBatch->indexData.data() + oldIndexSize,
                                 baseVertex);

            m_currentBatch->numVertices += 4;
            m_currentBatch->numIndices += 6;
            break;
        }
        default:
            break;
    }
}

void RenderQueue::flushCurrentBatch() {
    if (!m_currentBatch || m_currentBatch->numVertices == 0) return;

    // 将当前批次移动到完成列表
    m_completedBatches.push_back(*m_currentBatch);
    m_currentBatch->clear();
}

void RenderQueue::executeBatch(const RenderBatch& batch) {
    if (batch.numVertices == 0 || batch.numIndices == 0) return;

    // 分配瞬态缓冲区
    uint32_t availVB = bgfx::getAvailTransientVertexBuffer(batch.numVertices, batch.layout);
    uint32_t availIB = bgfx::getAvailTransientIndexBuffer(batch.numIndices);

    if (availVB < batch.numVertices || availIB < batch.numIndices) {
        TINA_WARN("RenderQueue: 瞬态缓冲区不足 (需要 V:{}/I:{}, 可用 V:{}/I:{})",
                  batch.numVertices, batch.numIndices, availVB, availIB);
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;

    bgfx::allocTransientVertexBuffer(&tvb, batch.numVertices, batch.layout);
    bgfx::allocTransientIndexBuffer(&tib, batch.numIndices);

    // 复制数据（使用 C 标准库的 memcpy，这是性能关键路径）
    Tina::stl::memcpy(tvb.data, batch.vertexData.data(), batch.vertexData.size());
    Tina::stl::memcpy(tib.data, batch.indexData.data(), batch.indexData.size() * sizeof(uint16_t));

    // 设置缓冲区
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);

    // 设置纹理
    if (bgfx::isValid(batch.texture)) {
        // 统一采样器：UI/2D 默认使用点采样与边缘夹取，避免越界采样伪影
        bgfx::setTexture(0, m_sTexture, batch.texture,
                         BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                         BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
        m_stats.numTextureBinds++;
    }

    // 设置渲染状态
    uint64_t state = getRenderState(batch.blendMode);
    bgfx::setState(state);

    // 提交
    // 按批次所属的视图提交（确保使用正确的投影与视口）
    bgfx::submit(batch.viewId, batch.program);

    // 更新统计
    m_stats.numDrawCalls++;
    m_stats.numVertices += batch.numVertices;
    m_stats.numTriangles += batch.numIndices / 3;
}

void RenderQueue::executeCommand(const RenderCommand& cmd) {
    // 直接执行单个命令（不批处理）
    RenderBatch batch;
    batch.type = cmd.type;
    batch.program = cmd.program;
    batch.blendMode = cmd.blendMode;
    batch.layout = (cmd.type == RenderType::Rectangle) ? m_layoutColor : m_layoutSprite;

    beginBatch(cmd);
    addToBatch(cmd);
    executeBatch(*m_currentBatch);
    m_currentBatch->clear();
}

uint64_t RenderQueue::getRenderState(BlendMode mode) const {
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

    switch (mode) {
        case BlendMode::Opaque:
            // 不透明，无混合
            break;
        case BlendMode::Alpha:
            // 标准Alpha混合
            state |= BGFX_STATE_BLEND_ALPHA;
            break;
        case BlendMode::Additive:
            // 加法混合
            state |= BGFX_STATE_BLEND_ADD;
            break;
        case BlendMode::Multiply:
            // 乘法混合
            state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO);
            break;
        case BlendMode::PremultAlpha:
            // 预乘Alpha
            state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
            break;
    }

    return state;
}

void RenderQueue::generateRectVertices(const RectData& rect, uint8_t* vertices,
                                       uint16_t* indices, uint32_t baseVertex) {
    ColorVertex* verts = reinterpret_cast<ColorVertex*>(vertices);

    // 如果有旋转，计算旋转后的顶点
    if (rect.rotation != 0.0f) {
        float cx = rect.x + rect.w * 0.5f;
        float cy = rect.y + rect.h * 0.5f;
        float cos = Tina::stl::cos(rect.rotation);
        float sin = Tina::stl::sin(rect.rotation);

        auto rotatePoint = [&](float px, float py, float& ox, float& oy) {
            float dx = px - cx;
            float dy = py - cy;
            ox = cx + dx * cos - dy * sin;
            oy = cy + dx * sin + dy * cos;
        };

        float x0, y0, x1, y1, x2, y2, x3, y3;
        rotatePoint(rect.x, rect.y, x0, y0);
        rotatePoint(rect.x + rect.w, rect.y, x1, y1);
        rotatePoint(rect.x + rect.w, rect.y + rect.h, x2, y2);
        rotatePoint(rect.x, rect.y + rect.h, x3, y3);

        verts[0] = {x0, y0, 0.0f, rect.color.r(), rect.color.g(), rect.color.b(), rect.color.a()};
        verts[1] = {x1, y1, 0.0f, rect.color.r(), rect.color.g(), rect.color.b(), rect.color.a()};
        verts[2] = {x2, y2, 0.0f, rect.color.r(), rect.color.g(), rect.color.b(), rect.color.a()};
        verts[3] = {x3, y3, 0.0f, rect.color.r(), rect.color.g(), rect.color.b(), rect.color.a()};
    } else {
        // 无旋转，直接设置
        verts[0] = {rect.x, rect.y, 0.0f, rect.color.r(), rect.color.g(), rect.color.b(), rect.color.a()};
        verts[1] = {rect.x + rect.w, rect.y, 0.0f, rect.color.r(), rect.color.g(), rect.color.b(), rect.color.a()};
        verts[2] = {rect.x + rect.w, rect.y + rect.h, 0.0f, rect.color.r(), rect.color.g(), rect.color.b(), rect.color.a()};
        verts[3] = {rect.x, rect.y + rect.h, 0.0f, rect.color.r(), rect.color.g(), rect.color.b(), rect.color.a()};
    }

    // 索引
    indices[0] = baseVertex + 0;
    indices[1] = baseVertex + 1;
    indices[2] = baseVertex + 2;
    indices[3] = baseVertex + 0;
    indices[4] = baseVertex + 2;
    indices[5] = baseVertex + 3;
}

void RenderQueue::generateSpriteVertices(const SpriteData& sprite, uint8_t* vertices,
                                        uint16_t* indices, uint32_t baseVertex) {
    SpriteVertex* verts = reinterpret_cast<SpriteVertex*>(vertices);

    if (sprite.rotation != 0.0f) {
        // 带旋转的精灵
        float cx = sprite.x + sprite.w * 0.5f;
        float cy = sprite.y + sprite.h * 0.5f;
        float cos = Tina::stl::cos(sprite.rotation);
        float sin = Tina::stl::sin(sprite.rotation);

        auto rotatePoint = [&](float px, float py, float& ox, float& oy) {
            float dx = px - cx;
            float dy = py - cy;
            ox = cx + dx * cos - dy * sin;
            oy = cy + dx * sin + dy * cos;
        };

        float x0, y0, x1, y1, x2, y2, x3, y3;
        rotatePoint(sprite.x, sprite.y, x0, y0);
        rotatePoint(sprite.x + sprite.w, sprite.y, x1, y1);
        rotatePoint(sprite.x + sprite.w, sprite.y + sprite.h, x2, y2);
        rotatePoint(sprite.x, sprite.y + sprite.h, x3, y3);

        verts[0] = {x0, y0, 0.0f, sprite.u0, sprite.v0, sprite.tint.r(), sprite.tint.g(), sprite.tint.b(), sprite.tint.a()};
        verts[1] = {x1, y1, 0.0f, sprite.u1, sprite.v0, sprite.tint.r(), sprite.tint.g(), sprite.tint.b(), sprite.tint.a()};
        verts[2] = {x2, y2, 0.0f, sprite.u1, sprite.v1, sprite.tint.r(), sprite.tint.g(), sprite.tint.b(), sprite.tint.a()};
        verts[3] = {x3, y3, 0.0f, sprite.u0, sprite.v1, sprite.tint.r(), sprite.tint.g(), sprite.tint.b(), sprite.tint.a()};
    } else {
        verts[0] = {sprite.x, sprite.y, 0.0f, sprite.u0, sprite.v0, sprite.tint.r(), sprite.tint.g(), sprite.tint.b(), sprite.tint.a()};
        verts[1] = {sprite.x + sprite.w, sprite.y, 0.0f, sprite.u1, sprite.v0, sprite.tint.r(), sprite.tint.g(), sprite.tint.b(), sprite.tint.a()};
        verts[2] = {sprite.x + sprite.w, sprite.y + sprite.h, 0.0f, sprite.u1, sprite.v1, sprite.tint.r(), sprite.tint.g(), sprite.tint.b(), sprite.tint.a()};
        verts[3] = {sprite.x, sprite.y + sprite.h, 0.0f, sprite.u0, sprite.v1, sprite.tint.r(), sprite.tint.g(), sprite.tint.b(), sprite.tint.a()};
    }

    // 索引
    indices[0] = baseVertex + 0;
    indices[1] = baseVertex + 1;
    indices[2] = baseVertex + 2;
    indices[3] = baseVertex + 0;
    indices[4] = baseVertex + 2;
    indices[5] = baseVertex + 3;
}

} // namespace Tina::Renderer
