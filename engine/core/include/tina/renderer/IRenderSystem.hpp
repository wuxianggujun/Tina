#pragma once

#include <entt/entt.hpp>
#include "tina/core/OrthographicCamera.hpp"

namespace Tina {

class IRenderSystem {
public:
    virtual ~IRenderSystem() = default;

    // 初始化渲染系统
    virtual void init() = 0;
    
    // 关闭渲染系统
    virtual void shutdown() = 0;
    
    // 开始场景渲染
    virtual void beginScene(const OrthographicCamera& camera) = 0;
    
    // 结束场景渲染
    virtual void endScene() = 0;
    
    // 渲染实体
    virtual void render(entt::registry& registry) = 0;
    
    // 提交渲染命令
    virtual void submit() = 0;
    
    // 刷新渲染状态
    virtual void flush() = 0;
};

} // namespace Tina 