#include <tina/asset/CharacterController2D.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::Asset {
namespace {

[[nodiscard]] bool isFinitePositive(float value) noexcept
{
    return value == value && value > 0.0f;
}

} // namespace

CharacterController2D::CharacterController2D(CharacterController2DConfig config) noexcept : m_config(config) {}

void CharacterController2D::teleport(float x, float y, bool clearVelocity) noexcept
{
    m_state.positionX = x;
    m_state.positionY = y;
    if (clearVelocity)
    {
        m_state.velocityX = 0.0f;
        m_state.velocityY = 0.0f;
    }
    m_state.grounded = false;
    m_state.hitCeiling = false;
    m_state.hitLeft = false;
    m_state.hitRight = false;
}

TileMapSolidQuery CharacterController2D::bodyAabbAt(float x, float y) const noexcept
{
    return TileMapSolidQuery{
        .minX = x - m_config.halfWidth,
        .minY = y - m_config.halfHeight,
        .maxX = x + m_config.halfWidth,
        .maxY = y + m_config.halfHeight,
    };
}

Core::Status CharacterController2D::refreshGrounded(const IGridCollisionProvider& grid,
                                                    std::pmr::vector<TileMapSolidHit>& scratch)
{
    const float probe = std::max(m_config.groundProbe, m_config.skin);
    const TileMapSolidQuery feet{
        .minX = m_state.positionX - m_config.halfWidth + m_config.skin,
        .minY = m_state.positionY - m_config.halfHeight - probe,
        .maxX = m_state.positionX + m_config.halfWidth - m_config.skin,
        .maxY = m_state.positionY - m_config.halfHeight + m_config.skin,
    };
    auto count = grid.querySolidAabb(feet, scratch);
    if (!count)
    {
        return Core::failure(std::move(count.error()));
    }
    m_state.grounded = *count > 0U && m_state.velocityY <= 0.0f;
    return Core::success();
}

Core::Status CharacterController2D::moveAxis(const IGridCollisionProvider& grid, float deltaSeconds, bool horizontal,
                                             std::pmr::vector<TileMapSolidHit>& scratch)
{
    const float velocity = horizontal ? m_state.velocityX : m_state.velocityY;
    if (velocity == 0.0f || deltaSeconds <= 0.0f)
    {
        return Core::success();
    }

    float nextX = m_state.positionX;
    float nextY = m_state.positionY;
    if (horizontal)
    {
        nextX += velocity * deltaSeconds;
    } else
    {
        nextY += velocity * deltaSeconds;
    }

    auto query = bodyAabbAt(nextX, nextY);
    // Shrink slightly on non-moving axis to reduce corner snags.
    if (horizontal)
    {
        query.minY += m_config.skin;
        query.maxY -= m_config.skin;
    } else
    {
        query.minX += m_config.skin;
        query.maxX -= m_config.skin;
    }

    auto count = grid.querySolidAabb(query, scratch);
    if (!count)
    {
        return Core::failure(std::move(count.error()));
    }
    if (*count == 0U)
    {
        m_state.positionX = nextX;
        m_state.positionY = nextY;
        return Core::success();
    }

    const float cell = grid.cellSizeMeters();
    if (horizontal)
    {
        if (velocity > 0.0f)
        {
            // Hit right: place just left of leftmost overlapping solid max edge.
            float best = nextX;
            bool any = false;
            for (const auto& hit : scratch)
            {
                const float solidMinX = static_cast<float>(hit.cellX) * cell;
                const float resolved = solidMinX - m_config.halfWidth - m_config.skin;
                if (!any || resolved < best)
                {
                    best = resolved;
                    any = true;
                }
            }
            m_state.positionX = any ? best : m_state.positionX;
            m_state.velocityX = 0.0f;
            m_state.hitRight = true;
        } else
        {
            float best = nextX;
            bool any = false;
            for (const auto& hit : scratch)
            {
                const float solidMaxX = static_cast<float>(hit.cellX + 1U) * cell;
                const float resolved = solidMaxX + m_config.halfWidth + m_config.skin;
                if (!any || resolved > best)
                {
                    best = resolved;
                    any = true;
                }
            }
            m_state.positionX = any ? best : m_state.positionX;
            m_state.velocityX = 0.0f;
            m_state.hitLeft = true;
        }
    } else
    {
        if (velocity > 0.0f)
        {
            // Moving up → ceiling.
            float best = nextY;
            bool any = false;
            for (const auto& hit : scratch)
            {
                const float solidMinY = static_cast<float>(hit.cellY) * cell;
                const float resolved = solidMinY - m_config.halfHeight - m_config.skin;
                if (!any || resolved < best)
                {
                    best = resolved;
                    any = true;
                }
            }
            m_state.positionY = any ? best : m_state.positionY;
            m_state.velocityY = 0.0f;
            m_state.hitCeiling = true;
        } else
        {
            // Moving down → floor.
            float best = nextY;
            bool any = false;
            for (const auto& hit : scratch)
            {
                const float solidMaxY = static_cast<float>(hit.cellY + 1U) * cell;
                const float resolved = solidMaxY + m_config.halfHeight + m_config.skin;
                if (!any || resolved > best)
                {
                    best = resolved;
                    any = true;
                }
            }
            m_state.positionY = any ? best : m_state.positionY;
            m_state.velocityY = 0.0f;
            m_state.grounded = true;
        }
    }
    return Core::success();
}

Core::Status CharacterController2D::move(const IGridCollisionProvider& grid, float deltaSeconds,
                                         const CharacterController2DMoveInput& input,
                                         std::pmr::vector<TileMapSolidHit>& scratch)
{
    if (!isFinitePositive(m_config.halfWidth) || !isFinitePositive(m_config.halfHeight) ||
        !(m_config.gravity >= 0.0f) || !(m_config.maxFallSpeed >= 0.0f) || !(m_config.skin >= 0.0f) ||
        !(deltaSeconds >= 0.0f) || deltaSeconds != deltaSeconds || input.wishVelocityX != input.wishVelocityX)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "character controller move arguments invalid");
    }
    if (grid.cellSizeMeters() <= 0.0f)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "grid cell size invalid");
    }

    m_state.hitCeiling = false;
    m_state.hitLeft = false;
    m_state.hitRight = false;

    if (const auto status = refreshGrounded(grid, scratch); !status)
    {
        return status;
    }

    m_state.velocityX = input.wishVelocityX;
    if (input.jump && m_state.grounded)
    {
        m_state.velocityY = input.jumpSpeed;
        m_state.grounded = false;
    }

    // Integrate gravity when airborne / always for simple slice.
    m_state.velocityY -= m_config.gravity * deltaSeconds;
    if (m_state.velocityY < -m_config.maxFallSpeed)
    {
        m_state.velocityY = -m_config.maxFallSpeed;
    }

    // Axis separation: horizontal then vertical (classic platformer order).
    if (const auto status = moveAxis(grid, deltaSeconds, true, scratch); !status)
    {
        return status;
    }
    if (const auto status = moveAxis(grid, deltaSeconds, false, scratch); !status)
    {
        return status;
    }
    if (const auto status = refreshGrounded(grid, scratch); !status)
    {
        return status;
    }
    if (m_state.grounded && m_state.velocityY < 0.0f)
    {
        m_state.velocityY = 0.0f;
    }
    return Core::success();
}

} // namespace Tina::Asset
