//
// RenderCommand - 统一的渲染命令系统
// 用途：将所有渲染调用抽象为命令，支持排序、批处理和延迟执行
//

#pragma once

#include <bgfx/bgfx.h>
#include "../core/Color.hpp"
#include "../core/Math.hpp"
#include <cstdint>
#include <cstring>  // for std::memset

namespace Tina::Renderer {

// 渲染类型枚举
enum class RenderType : uint8_t {
    Rectangle,     // 矩形（纯色）
    Sprite         // 精灵（纹理）
};

// 渲染层级定义（用于排序）
enum class RenderLayer : uint8_t {
    Background = 0,  // 背景层（最底层）
    World = 1,       // 世界层
    Effects = 2,     // 特效层
    UI = 3,          // UI层
    Overlay = 4,     // 遮罩层
    Debug = 5        // 调试层（最顶层）
};

// 混合模式
enum class BlendMode : uint8_t {
    Opaque,         // 不透明
    Alpha,          // 标准Alpha混合
    Additive,       // 加法混合
    Multiply,       // 乘法混合
    PremultAlpha    // 预乘Alpha
};

// 矩形数据
struct RectData {
    float x, y, w, h;
    Core::Color color;
    float rotation = 0.0f;  // 旋转角度（弧度）
};

// 精灵数据
struct SpriteData {
    float x, y, w, h;
    bgfx::TextureHandle texture;
    float u0 = 0.0f, v0 = 0.0f;  // UV坐标
    float u1 = 1.0f, v1 = 1.0f;
    Core::Color tint{1.0f, 1.0f, 1.0f, 1.0f};  // 颜色调制
    float rotation = 0.0f;
};


// 渲染命令结构
struct RenderCommand {
    // 排序键（64位）
    // [63-56]: Layer(8bit) | [55-48]: RenderType(8bit) | [47-32]: TextureID(16bit) | [31-16]: Depth(16bit) | [15-0]: SequenceID(16bit)
    uint64_t sortKey = 0;

    // 视图ID
    uint16_t viewId = 0;

    // 渲染类型
    RenderType type = RenderType::Rectangle;

    // 渲染状态
    BlendMode blendMode = BlendMode::Alpha;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;  // 使用的着色器程序

    // 数据联合体（根据type选择）
    union Data {
        RectData rect;
        SpriteData sprite;
        // 已精简：仅保留矩形与精灵所需的数据

        // ���合体构造函数（初始化为空）
        Data() { std::memset(this, 0, sizeof(Data)); }
        ~Data() {}  // 空析构函数
    } data;

    // 构造函数
    RenderCommand() : type(RenderType::Rectangle), viewId(0), sortKey(0), blendMode(BlendMode::Alpha), program(BGFX_INVALID_HANDLE), data() {
    }

    // 创建排序键（高位合并 viewId 与 layer，减少跨视图切换导致的批次中断）
    // 顶部8位：viewLayer = (viewId[5bit] << 3) | layer[3bit]
    // [63-56]: viewLayer | [55-48]: RenderType | [47-32]: TextureID | [31-16]: Depth | [15-0]: Sequence
    static uint64_t MakeSortKey(uint16_t viewId, RenderLayer layer, RenderType type,
                                uint16_t textureId = 0,
                                uint16_t depth = 0,
                                uint16_t sequence = 0) {
        uint8_t viewLayer = static_cast<uint8_t>(((viewId & 0x1F) << 3) | (static_cast<uint8_t>(layer) & 0x07));
        return (static_cast<uint64_t>(viewLayer) << 56) |
               (static_cast<uint64_t>(type) << 48) |
               (static_cast<uint64_t>(textureId) << 32) |
               (static_cast<uint64_t>(depth) << 16) |
               static_cast<uint64_t>(sequence);
    }

    // 便捷创建方法
    static RenderCommand MakeRect(uint16_t viewId, RenderLayer layer,
                                  float x, float y, float w, float h,
                                  const Core::Color& color,
                                  uint16_t depth = 0) {
        RenderCommand cmd;
        cmd.viewId = viewId;
        cmd.type = RenderType::Rectangle;
        cmd.data.rect = {x, y, w, h, color, 0.0f};
        cmd.sortKey = MakeSortKey(viewId, layer, RenderType::Rectangle, 0, depth);
        cmd.blendMode = (color.a() < 1.0f) ? BlendMode::Alpha : BlendMode::Opaque;
        return cmd;
    }

    static RenderCommand MakeSprite(uint16_t viewId, RenderLayer layer,
                                    float x, float y, float w, float h,
                                    bgfx::TextureHandle texture,
                                    uint16_t depth = 0) {
        RenderCommand cmd;
        cmd.viewId = viewId;
        cmd.type = RenderType::Sprite;
        cmd.data.sprite = {x, y, w, h, texture, 0, 0, 1, 1, {1,1,1,1}, 0};
        // 使用纹理句柄的idx作为textureId用于排序
        cmd.sortKey = MakeSortKey(viewId, layer, RenderType::Sprite, texture.idx, depth);
        cmd.blendMode = BlendMode::Alpha;
        return cmd;
    }
};

} // namespace Tina::Renderer
