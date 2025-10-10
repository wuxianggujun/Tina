//
// ShaderCatalog - 逻辑着色器目录（领域 → 物理 shader 名）
// - 目的：从“逻辑用途”出发获取 Program，而不是在各处硬编码同一名字
// - 当前默认将 ui/world 基础管线映射到内置 "color"，后续可替换为独立 shader

#pragma once

#include <bgfx/bgfx.h>

namespace Tina { namespace Renderer { class ShaderManager; } }

namespace Tina::Renderer {

struct ShaderCatalog {
    enum class Tag {
        UiSolid,       // UI 基础纯色（Primitive2D）
        WorldSolid,    // 世界基础纯色（TileRenderer 等简单几何）
        Sprite,        // 2D 精灵（位置/UV/颜色）
        Particle,      // 粒子（当前已使用 "particle"）
        Text           // 文本（由 UIRenderer/TextRenderer 管理）
    };

    // 获取 ProgramHandle（若未找到，返回无效句柄）
    static bgfx::ProgramHandle Load(ShaderManager& sm, Tag tag);
};

} // namespace Tina::Renderer

