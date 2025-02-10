#include <memory>
#include <tina/core/Engine.hpp>
#include <tina/layer/Render2DLayer.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "tina/scene/Scene.hpp"

using namespace Tina;

class TestLayer : public Layer {
public:
    TestLayer() : Layer("TestLayer") {}

    virtual void onRender() override {
        // 设置正交投影矩阵
        float width = 800.0f;
        float height = 600.0f;
        glm::mat4 projection = glm::ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
        glm::mat4 view = glm::mat4(1.0f);

        Renderer2D::beginScene(projection * view);

        // 绘制一些测试图形
        // 红色方块
        Renderer2D::drawQuad({100.0f, 100.0f}, {50.0f, 50.0f}, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        // 蓝色方块
        Renderer2D::drawQuad({200.0f, 200.0f}, {100.0f, 100.0f}, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
        // 绿色方块
        Renderer2D::drawQuad({400.0f, 300.0f}, {75.0f, 75.0f}, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));

        Renderer2D::endScene();
    }
};

int main() {
    // 创建引擎实例
    auto engine = std::make_shared<Core::Engine>();
    
    // 初始化引擎
    if (!engine->initialize()) {
        return -1;
    }

    // 创建场景
    auto scene = std::make_shared<Scene>("Test Scene");
    
    // 添加测试层
    scene->pushLayer(std::make_shared<TestLayer>());

    // 设置场景
    engine->setActiveScene(scene.get());

    // 运行引擎
    engine->run();

    return 0;
}
