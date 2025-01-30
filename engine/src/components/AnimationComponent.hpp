//
// Created by wuxianggujun on 2025/1/30.
//

#pragma once
#include "graphics/Animation.hpp"

namespace Tina
{
    // 动画组件（用于ECS系统）
    struct AnimationComponent
    {
        Animation animation;
        bool autoPlay = true;

        // 可选：动画状态机
        std::unordered_map<std::string, Animation> animations;
        std::string currentAnimation;

        void switchAnimation(const std::string& name)
        {
            if (currentAnimation != name && animations.count(name) > 0)
            {
                currentAnimation = name;
                animations[name].reset();
                animations[name].play();
            }
        }
    };
}
