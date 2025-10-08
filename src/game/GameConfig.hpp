//
// 全局常量配置（魔法数字上移）

#pragma once

#include <cstdint>

namespace Tina::GameConfig {
    // === 地形编辑 ===
    constexpr float EXCAVATE_RADIUS = 3.5f;

    // === 玩家颜色 ===
    constexpr float PLAYER_COLOR_R = 0.3f;
    constexpr float PLAYER_COLOR_G = 0.5f;
    constexpr float PLAYER_COLOR_B = 0.9f;
    constexpr float PLAYER_COLOR_A = 0.95f;

    // === 地图配置 ===
    constexpr int DEFAULT_MAP_WIDTH = 160;
    constexpr int DEFAULT_MAP_HEIGHT = 90;
    constexpr int DEFAULT_MAP_SEED = 1337;

    // === 相机配置 ===
    constexpr float DEFAULT_VIEW_HEIGHT = 60.0f;
    constexpr float CAMERA_SMOOTH_FACTOR = 0.1f;  // 相机平滑跟随系数（0-1，越大越快）

    // === 渲染配置 ===
    constexpr uint32_t CLEAR_COLOR = 0x303030ff;  // 深灰色背景

    // === 粒子系统配置 ===
    constexpr float PARTICLE_GRAVITY_X = 0.0f;
    constexpr float PARTICLE_GRAVITY_Y = -12.0f;
    constexpr float PARTICLE_DRAG = 0.10f;

    // === 爆炸粒子参数 ===
    constexpr int EXPLODE_PARTICLE_COUNT = 260;
    constexpr float EXPLODE_SPEED_MIN = 6.0f;
    constexpr float EXPLODE_SPEED_MAX = 14.0f;
    constexpr float EXPLODE_SIZE_MIN = 0.30f;
    constexpr float EXPLODE_SIZE_MAX = 0.90f;
    constexpr float EXPLODE_LIFE_MIN = 0.6f;
    constexpr float EXPLODE_LIFE_MAX = 1.6f;

    // === 出生点搜索 ===
    constexpr int SPAWN_SEARCH_MAX_RADIUS = 40;
    constexpr int SPAWN_MIN_Y = 2;
    constexpr int SPAWN_OFFSET_FROM_BOTTOM = 3;

    // === NPC 生成偏移 ===
    constexpr float NPC_SPAWN_OFFSET_1 = 5.0f;
    constexpr float NPC_SPAWN_OFFSET_2 = 5.0f;
    constexpr float NPC_SPAWN_OFFSET_3 = 10.0f;

    // === 水模拟 ===
    constexpr int WATER_STEP_COUNT = 2;  // 每帧水模拟步数
    constexpr int WATER_MAX_LEVEL = 255;  // 满水位

    // === UI 配置 ===
    constexpr float UI_HUD_PADDING_X = 16.0f;
    constexpr float UI_HUD_PADDING_Y = 12.0f;
    constexpr float UI_HUD_WIDTH_MARGIN = 32.0f;
    constexpr float UI_HUD_HEIGHT = 28.0f;

    // === 音频（BGM/SFX） ===
    constexpr const char* BGM_PATH = "resources/audio/bgm.mp3"; // 若不存在，将记录警告
    constexpr int BGM_FADEIN_MS = 1200;
    constexpr int BGM_FADEOUT_MS = 400;
    constexpr float BGM_VOLUME = 0.8f; // 0.0 ~ 1.0
}
