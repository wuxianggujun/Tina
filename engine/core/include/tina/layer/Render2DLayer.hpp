#pragma once

#include "Layer.hpp"
#include "tina/renderer/Renderer2D.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Tina {

class Render2DLayer : public Layer {
public:
    Render2DLayer() : Layer("Render2DLayer") {}

    virtual void onAttach() override {
        Renderer2D::init();
    }

    virtual void onDetach() override {
        Renderer2D::shutdown();
    }

    virtual void onUpdate(float deltaTime) override {
        // 在这里更新2D对象
    }

    virtual void onRender() override {
        // 设置正交投影矩阵
        float width = 800.0f;  // 获取窗口宽度
        float height = 600.0f; // 获取窗口高度
        glm::mat4 projection = glm::ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
        glm::mat4 view = glm::mat4(1.0f);
        
        Renderer2D::beginScene(projection * view);

        // 示例：绘制一个矩形
        Renderer2D::drawQuad({100.0f, 100.0f}, {50.0f, 50.0f}, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

        Renderer2D::endScene();
    }
};

} // namespace Tina
