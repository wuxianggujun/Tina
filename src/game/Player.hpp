//
// 玩家角色系统
// - 2格高的角色（宽0.8格，高2格）
// - ASDW移动 + 空格跳跃
// - 基础物理：重力、碰撞检测、跳跃
//

#pragma once

#include "../core/Container.hpp"
#include "TileMap.hpp"

namespace Tina::Game {

class Player {
public:
    Player();
    ~Player() = default;

    // 初始化玩家位置（世界坐标）
    void spawn(float x, float y);

    // 物理更新（需要传入地图进行碰撞检测）
    void update(float dt, const TileMap& tilemap);

    // 控制接口
    void moveLeft(bool pressed) { m_moveLeft = pressed; }
    void moveRight(bool pressed) { m_moveRight = pressed; }
    void jump();
    // 跳跃保持（长按自动连跳）
    void setJumpHeld(bool held) { m_jumpHeld = held; }

    // 获取状态
    float x() const { return m_x; }
    float y() const { return m_y; }
    float vx() const { return m_vx; }
    float vy() const { return m_vy; }
    float width() const { return m_width; }
    float height() const { return m_height; }
    bool isOnGround() const { return m_onGround; }

    // 相机跟随用的中心点
    float centerX() const { return m_x + m_width * 0.5f; }
    float centerY() const { return m_y + m_height * 0.5f; }

    // 获取渲染用的AABB（世界坐标）
    void getAABB(float& outX, float& outY, float& outW, float& outH) const {
        outX = m_x; outY = m_y; outW = m_width; outH = m_height;
    }

private:
    // 碰撞检测辅助
    bool isSolid(int tx, int ty, const TileMap& tilemap) const;
    bool checkCollision(float nx, float ny, const TileMap& tilemap) const;

private:
    // 位置与速度（世界坐标，单位=格）
    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_vx = 0.0f;
    float m_vy = 0.0f;

    // 尺寸（世界坐标，单位=格）
    float m_width = 0.8f;  // 略小于1格，便于穿过1格宽通道
    float m_height = 2.0f; // 2格高

    // 物理参数
    float m_gravity = 32.0f;      // 重力加速度
    float m_moveSpeed = 8.0f;     // 水平移动速度
    float m_jumpSpeed = 14.0f;    // 跳跃初速度
    float m_airControl = 0.3f;    // 空中控制力（0-1）
    float m_friction = 12.0f;     // 地面摩擦力

    // 状态
    bool m_onGround = false;
    bool m_moveLeft = false;
    bool m_moveRight = false;
    bool m_jumpHeld = false; // 是否长按跳跃
};

} // namespace Tina::Game
