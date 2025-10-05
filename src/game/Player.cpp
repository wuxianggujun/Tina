#include "Player.hpp"
#include <algorithm>
#include <cmath>

namespace Tina::Game {

Player::Player()
{
    // 默认构造
}

void Player::spawn(float x, float y)
{
    m_x = x;
    m_y = y;
    m_vx = 0.0f;
    m_vy = 0.0f;
    m_onGround = false;
}

bool Player::isSolid(int tx, int ty, const TileMap& tilemap) const
{
    if (tx < 0 || ty < 0 || tx >= tilemap.width() || ty >= tilemap.height()) {
        return true; // 边界视为固体
    }
    TileType t = tilemap.get(tx, ty);
    // 空气、水、岩浆不算固体碰撞
    return tilemap.isSolidTile(t);
}

bool Player::checkCollision(float nx, float ny, const TileMap& tilemap) const
{
    // 检查玩家AABB是否与固体方块重叠
    // 玩家AABB: (nx, ny) ~ (nx+m_width, ny+m_height)

    // 计算玩家覆盖的瓦片范围（收缩上边/右边的边界，避免恰好落在整数边界时误判）
    const float EPS = 1e-6f;
    float left   = nx;
    float right  = nx + m_width  - EPS;
    float bottom = ny;
    float top    = ny + m_height - EPS;
    int x0 = (int)std::floor(left);
    int y0 = (int)std::floor(bottom);
    int x1 = (int)std::floor(right);
    int y1 = (int)std::floor(top);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (isSolid(x, y, tilemap)) {
                // 检查精确重叠
                float tileL = (float)x;
                float tileR = (float)(x + 1);
                float tileB = (float)y;
                float tileT = (float)(y + 1);

                float playerL = left;
                float playerR = right + EPS;   // 恢复原始宽度以参与重叠判断
                float playerB = bottom;
                float playerT = top + EPS;

                // AABB重叠检测
                if (playerR > tileL && playerL < tileR &&
                    playerT > tileB && playerB < tileT) {
                    return true;
                }
            }
        }
    }
    return false;
}

void Player::update(float dt, const TileMap& tilemap)
{
    if (dt <= 0.0f) return;

    // 若长按跳跃且在地面上，则本帧立即起跳（实现“长按自动连跳”）
    if (m_jumpHeld && m_onGround) {
        m_vy = m_jumpSpeed;
        m_onGround = false;
    }

    // 1. 水平移动输入
    float moveInput = 0.0f;
    if (m_moveLeft) moveInput -= 1.0f;
    if (m_moveRight) moveInput += 1.0f;

    // 泰拉瑞亚式移动：有输入时加速到目标速度，无输入时立即停止
    if (std::abs(moveInput) > 0.01f) {
        // 根据是否在地面调整加速度
        float accel = m_onGround ? m_moveSpeed * 20.0f : m_moveSpeed * 20.0f * m_airControl;
        m_vx += moveInput * accel * dt;
        // 限制最大水平速度
        m_vx = std::clamp(m_vx, -m_moveSpeed, m_moveSpeed);
    } else {
        // 无输入时立即停止（泰拉瑞亚效果）
        m_vx = 0.0f;
    }

    // 2. 重力（y 轴向上为正，因此重力应使 vy 逐渐减小）
    m_vy -= m_gravity * dt;

    // 限制最大下落速度（向下为负值）
    m_vy = std::max(m_vy, -40.0f);

    // 3. 水平移动 + 碰撞
    float newX = m_x + m_vx * dt;
    if (checkCollision(newX, m_y, tilemap)) {
        // 使用“旧位置”对齐到最近的网格边界，避免大步长时嵌入过深
        if (m_vx > 0.0f) {
            // 向右：对齐到旧位置右侧最近的整格左边缘
            int ceilRight = (int)std::ceil(m_x + m_width - 1e-6f);
            newX = (float)ceilRight - m_width - 0.001f;
        } else if (m_vx < 0.0f) {
            // 向左：对齐到旧位置左侧最近的整格右边缘
            int floorLeft = (int)std::floor(m_x + 1e-6f);
            newX = (float)floorLeft + 0.001f;
        }
        m_vx = 0.0f;
    }
    m_x = newX;

    // 4. 垂直移动 + 碰撞
    float newY = m_y + m_vy * dt;
    if (checkCollision(m_x, newY, tilemap)) {
        if (m_vy > 0.0f) {
            // 向上移动（头撞到天花板）：对齐到“旧位置顶部”上方最近的整格边界下方
            int ceilTop = (int)std::ceil(m_y + m_height - 1e-6f);
            newY = (float)ceilTop - m_height - 0.001f;
            m_vy = 0.0f;
        } else if (m_vy < 0.0f) {
            // 向下移动（落地）：对齐到“旧位置底部”下方最近的整格边界上方
            int floorBottom = (int)std::floor(m_y + 1e-6f);
            newY = (float)floorBottom + 0.001f;
            m_vy = 0.0f;
            m_onGround = true;
        }
    } else {
        m_onGround = false;
    }
    m_y = newY;

    // 5. 再次检查是否站在地面上（更稳健的脚底探测：只检测脚下一薄层）
    if (!m_onGround) {
        const float EPS = 1e-6f;
        int feetY = (int)std::floor(m_y - EPS); // 正下方那一格
        int x0 = (int)std::floor(m_x + EPS);
        int x1 = (int)std::floor(m_x + m_width - EPS);
        for (int x = x0; x <= x1; ++x) {
            if (isSolid(x, feetY, tilemap)) { m_onGround = true; break; }
        }
    }

    // 防止玩家掉出地图
    if (m_y < 0.0f) {
        m_y = 0.0f;
        m_vy = 0.0f;
        m_onGround = true;
    }
    if (m_y > tilemap.height() - m_height) {
        m_y = tilemap.height() - m_height;
        m_vy = 0.0f;
    }
    if (m_x < 0.0f) {
        m_x = 0.0f;
        m_vx = 0.0f;
    }
    if (m_x > tilemap.width() - m_width) {
        m_x = tilemap.width() - m_width;
        m_vx = 0.0f;
    }
}

void Player::jump()
{
    if (m_onGround) {
        m_vy = m_jumpSpeed;
        m_onGround = false;
    }
}

} // namespace Tina::Game
