//
// ECS世界管理器实现
//

#include "World.hpp"
#include "systems/PlayerInputSystem.hpp"
#include "systems/AISystem.hpp"
#include "systems/CharacterMovementSystem.hpp"
#include "systems/PhysicsSystem.hpp"
#include "systems/CharacterRenderSystem.hpp"
#include "../game/TileMap.hpp"
#include "../game/GameConfig.hpp"
#include "../core/Container.hpp"
#include <limits>
#include <algorithm>

namespace Tina::ECS {

World::World() {
    // 创建所有系统实例
    m_playerInputSys = Memory::MakeUnique<PlayerInputSystem>();
    m_aiSys = Memory::MakeUnique<AISystem>();
    m_movementSys = Memory::MakeUnique<CharacterMovementSystem>();
    m_physicsSys = Memory::MakeUnique<PhysicsSystem>();
    m_renderSys = Memory::MakeUnique<CharacterRenderSystem>();
}

World::~World() {
    // 智能指针自动释放
}

void World::update(float dt, const Tina::Game::TileMap& tilemap, const InputState& input) {
    // 按顺序执行系统
    m_playerInputSys->update(m_registry, input);
    m_aiSys->update(m_registry, dt);
    m_movementSys->update(m_registry, dt);
    m_physicsSys->update(m_registry, tilemap, dt);
}

void World::render(uint16_t viewId, bgfx::ProgramHandle program, const bgfx::VertexLayout& layout) {
    m_renderSys->render(m_registry, viewId, program, layout);
}

entt::entity World::createCharacter(float x, float y, bool isPlayerControlled) {
    auto entity = m_registry.create();

    // 添加通用组件
    m_registry.emplace<Transform>(entity, x, y, 0.0f);
    m_registry.emplace<Velocity>(entity, 0.0f, 0.0f);
    m_registry.emplace<PhysicsBody>(entity, 0.8f, 2.0f, 32.0f, false);
    m_registry.emplace<CharacterController>(entity, 8.0f, 14.0f, 0.3f);
    m_registry.emplace<Renderable>(entity, Tina::GameConfig::PLAYER_COLOR_R, Tina::GameConfig::PLAYER_COLOR_G, Tina::GameConfig::PLAYER_COLOR_B, Tina::GameConfig::PLAYER_COLOR_A);
    m_registry.emplace<CharacterTag>(entity);

    // 根据控制类型添加控制组件
    if (isPlayerControlled) {
        m_registry.emplace<PlayerController>(entity);
        m_controlledEntity = entity;
    } else {
        m_registry.emplace<AIController>(entity);
    }

    return entity;
}

void World::destroyCharacter(entt::entity entity) {
    if (m_registry.valid(entity)) {
        m_registry.destroy(entity);
        if (entity == m_controlledEntity) {
            m_controlledEntity = entt::null;
        }
    }
}

void World::switchControl(entt::entity newEntity) {
    // 1. 旧角色从玩家控制切换到AI控制
    if (m_controlledEntity != entt::null && m_registry.valid(m_controlledEntity)) {
        m_registry.remove<PlayerController>(m_controlledEntity);
        m_registry.emplace<AIController>(m_controlledEntity);
    }

    // 2. 新角色从AI控制切换到玩家控制
    if (newEntity != entt::null && m_registry.valid(newEntity)) {
        m_registry.remove<AIController>(newEntity);
        auto& pc = m_registry.emplace<PlayerController>(newEntity);
        // 重置输入状态
        pc.moveLeft = pc.moveRight = pc.jumpHeld = false;
    }

    m_controlledEntity = newEntity;
}

Tina::Container::Vector<entt::entity> World::getAllCharacters() const {
    Tina::Container::Vector<entt::entity> characters;
    auto view = m_registry.view<CharacterTag>();
    for (auto entity : view) {
        characters.push_back(entity);
    }
    return characters;
}

entt::entity World::getClosestCharacter(float x, float y, entt::entity exclude) const {
    auto view = m_registry.view<CharacterTag, Transform>();

    entt::entity closest = entt::null;
    float minDist = std::numeric_limits<float>::max();

    for (auto entity : view) {
        if (entity == exclude) continue;

        auto& transform = view.get<Transform>(entity);
        float dx = transform.x - x;
        float dy = transform.y - y;
        float dist = dx * dx + dy * dy; // 使用距离平方避免开方

        if (dist < minDist) {
            minDist = dist;
            closest = entity;
        }
    }

    return closest;
}

int World::getCharacterCount() const {
    return (int)m_registry.view<CharacterTag>().size();
}

} // namespace Tina::ECS

