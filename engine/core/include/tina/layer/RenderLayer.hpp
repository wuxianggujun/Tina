#pragma once

#include "Layer.hpp"

namespace Tina {

class RenderLayer : public Layer {
public:
    RenderLayer() : Layer("RenderLayer") {}

    virtual void onAttach() override {
        // 初始化渲染资源
    }

    virtual void onDetach() override {
        // 清理渲染资源
    }

    virtual void onUpdate(float deltaTime) override {
        // 更新场景对象
    }

    virtual void onRender() override {
        // 执行渲染
    }

    virtual void onImGuiRender() override {
        // 渲染调试信息或编辑器UI
    }
};

} // namespace Tina
