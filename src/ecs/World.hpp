//
// ECS 世界管理器（封装 entt::registry，管理所有系统）
//

#pragma once

#include "../core/Core.hpp"
#include "Components.hpp"
#include <entt/entt.hpp>

// 前向声明
namespace Tina::Game {
    class TileMap;
}

namespace Tina::ECS {

// 前向声明系统类
class PlayerInputSystem;
class AISystem;
class CharacterMovementSystem;
class PhysicsSystem;
class CharacterRenderSystem;

// 输入状态（简化版，后续可替换为更完善的输入系统）
struct InputState {
    bool moveLeft = false;
    bool moveRight = false;
    bool jump = false;
};

class World {
public:
    World();
    ~World();

    // 核心更新和渲染
    void update(float dt, const Tina::Game::TileMap& tilemap, const InputState& input);
    void render(uint16_t viewId, bgfx::ProgramHandle program, const bgfx::VertexLayout& layout);

    // 角色管理
    entt::entity createCharacter(float x, float y, bool isPlayerControlled = false);
    void destroyCharacter(entt::entity entity);

    // 玩家控制切换
    void switchControl(entt::entity newControlled);
    entt::entity getControlledEntity() const { return m_controlledEntity; }

    // 实用工具函数
    Container::Vector<entt::entity> getAllCharacters() const;
    entt::entity getClosestCharacter(float x, float y, entt::entity exclude = entt::null) const;
    int getCharacterCount() const;

    // 访问registry（供外部查询使用）
    entt::registry& registry() { return m_registry; }
    const entt::registry& registry() const { return m_registry; }

private:
    entt::registry m_registry;

    // 系统实例（使用 EASTL 智能指针）
    Memory::UniquePtr<PlayerInputSystem> m_playerInputSys;
    Memory::UniquePtr<AISystem> m_aiSys;
    Memory::UniquePtr<CharacterMovementSystem> m_movementSys;
    Memory::UniquePtr<PhysicsSystem> m_physicsSys;
    Memory::UniquePtr<CharacterRenderSystem> m_renderSys;

    // 当前玩家控制的实体
    entt::entity m_controlledEntity = entt::null;
};

} // namespace Tina::ECS
