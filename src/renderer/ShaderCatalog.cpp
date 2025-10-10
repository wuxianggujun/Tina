//
// ShaderCatalog 实现

#include "ShaderCatalog.hpp"
#include "ShaderManager.hpp"

namespace Tina::Renderer {

bgfx::ProgramHandle ShaderCatalog::Load(ShaderManager& sm, Tag tag)
{
    switch (tag) {
        case Tag::UiSolid:
            // UI 专用：ui_vs/ui_fs（已提供，若不存在回退 color）
            {
                auto p = sm.loadProgram("ui", "ui");
                if (bgfx::isValid(p)) return p;
                return sm.loadProgram("color", "color");
            }
        case Tag::WorldSolid:
            // 世界基础：world_vs/world_fs（已提供，若不存在回退 color）
            {
                auto p = sm.loadProgram("world", "world");
                if (bgfx::isValid(p)) return p;
                return sm.loadProgram("color", "color");
            }
        case Tag::Sprite:
            // 精灵：sprite_vs/sprite_fs（已存在）
            return sm.loadProgram("sprite", "sprite");
        case Tag::Particle:
            return sm.loadProgram("particle", "particle");
        case Tag::Text:
            // TextRenderer 内部管理；此处返回无效
        default:
            break;
    }
    return BGFX_INVALID_HANDLE;
}

} // namespace Tina::Renderer
