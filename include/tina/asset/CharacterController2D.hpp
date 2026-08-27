#pragma once

#include <tina/asset/GridCollision.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>
#include <vector>

namespace Tina::Asset {

// Axis-separated AABB character controller against IGridCollisionProvider (game-2d).
// First slice: solid tiles only, no slopes/one-way platforms, no Box2D coupling.
struct CharacterController2DConfig final {
    float halfWidth = 0.4f;
    float halfHeight = 0.8f;
    float gravity = 30.0f;
    float maxFallSpeed = 40.0f;
    // Skin inset when resolving to reduce edge flicker (meters).
    float skin = 0.02f;
    // Probe distance below feet for grounded checks.
    float groundProbe = 0.05f;
};

struct CharacterController2DState final {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    bool grounded = false;
    bool hitCeiling = false;
    bool hitLeft = false;
    bool hitRight = false;
};

struct CharacterController2DMoveInput final {
    // Desired horizontal velocity for this step (m/s). Vertical velocity is integrated with gravity.
    float wishVelocityX = 0.0f;
    // If true, sets velocityY to jumpSpeed when grounded at start of move.
    bool jump = false;
    float jumpSpeed = 12.0f;
};

class CharacterController2D final {
  public:
    CharacterController2D() = delete;
    explicit CharacterController2D(CharacterController2DConfig config = {}) noexcept;

    [[nodiscard]] const CharacterController2DConfig& config() const noexcept
    {
        return m_config;
    }
    [[nodiscard]] const CharacterController2DState& state() const noexcept
    {
        return m_state;
    }
    [[nodiscard]] CharacterController2DState& state() noexcept
    {
        return m_state;
    }

    // Position and velocity are placed either through teleport() or through the
    // mutable state() reference above; there is deliberately no third way.
    void teleport(float x, float y, bool clearVelocity) noexcept;

    // Integrates one fixed/variable step against the grid. Uses provider solid AABB queries only.
    // `scratch` is temporary solid-hit storage (cleared each sub-query).
    [[nodiscard]] Core::Status move(const IGridCollisionProvider& grid, float deltaSeconds,
                                    const CharacterController2DMoveInput& input,
                                    std::pmr::vector<TileMapSolidHit>& scratch);

  private:
    [[nodiscard]] TileMapSolidQuery bodyAabbAt(float x, float y) const noexcept;
    [[nodiscard]] Core::Status moveAxis(const IGridCollisionProvider& grid, float deltaSeconds, bool horizontal,
                                        std::pmr::vector<TileMapSolidHit>& scratch);
    [[nodiscard]] Core::Status refreshGrounded(const IGridCollisionProvider& grid,
                                               std::pmr::vector<TileMapSolidHit>& scratch);

    CharacterController2DConfig m_config{};
    CharacterController2DState m_state{};
};

} // namespace Tina::Asset
