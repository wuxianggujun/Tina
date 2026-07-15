//
// AI系统实现 - 简单的随机游荡
//

#include "AISystem.hpp"
#include <cstdlib>
#include <ctime>

namespace Tina::ECS {

void AISystem::update(entt::registry& registry, float dt) {
    // 初始化随机数（只需一次）
    static bool initialized = false;
    if (!initialized) {
        std::srand(static_cast<unsigned>(std::time(nullptr)));
        initialized = true;
    }

    auto view = registry.view<AIController>();
    for (auto entity : view) {
        auto& ai = view.get<AIController>(entity);

        // 更新思考计时器
        ai.thinkTimer += dt;

        // 到达思考时间，更新AI决策
        if (ai.thinkTimer >= ai.nextThinkTime) {
            ai.thinkTimer = 0.0f;
            ai.nextThinkTime = 1.0f + (std::rand() % 2000) / 1000.0f; // 1-3秒

            switch (ai.behavior) {
                case AIController::Behavior::Idle:
                    // 待机：不做任何事
                    ai.wantMoveLeft = false;
                    ai.wantMoveRight = false;
                    ai.wantJump = false;
                    break;

                case AIController::Behavior::Wander:
                    // 随机游荡：随机改变方向
                    {
                        int choice = std::rand() % 4;
                        if (choice == 0) {
                            // 向左走
                            ai.wanderDirection = -1.0f;
                            ai.wantMoveLeft = true;
                            ai.wantMoveRight = false;
                        } else if (choice == 1) {
                            // 向右走
                            ai.wanderDirection = 1.0f;
                            ai.wantMoveLeft = false;
                            ai.wantMoveRight = true;
                        } else if (choice == 2) {
                            // 停止
                            ai.wantMoveLeft = false;
                            ai.wantMoveRight = false;
                        } else {
                            // 跳跃
                            ai.wantJump = (std::rand() % 3 == 0); // 1/3概率跳跃
                        }
                    }
                    break;

                case AIController::Behavior::Follow:
                    // TODO: 实现跟随逻辑
                    break;

                case AIController::Behavior::Flee:
                    // TODO: 实现逃跑逻辑
                    break;
            }
        }
    }
}

} // namespace Tina::ECS
