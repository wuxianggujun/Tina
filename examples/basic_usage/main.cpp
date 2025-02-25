//
// Created by wuxianggujun on 2025/2/24.
//

#include "tina/core/Engine.hpp"
#include "tina/core/Node.hpp"
#include "tina/core/Scene.hpp"
#include "tina/core/Transform.hpp"
#include "tina/renderer/SpriteRenderer.hpp"

using namespace Tina;

class GameScene : public Scene {
public:
    GameScene() : Scene("GameScene") {}

    void onEnter() override {
        // 创建精灵节点
        auto* spriteNode = createNode("Sprite");

        // 添加变换组件
        auto* transform = spriteNode->addComponent<Transform>();
        transform->setPosition({0.0f, 0.0f, 0.0f});

        // 添加精灵渲染组件
        auto* renderer = spriteNode->addComponent<SpriteRenderer>();
        auto texture = Engine::getInstance()->getResourceManager()->loadSync<TextureResource>("test.png", "resources/textures/");
        renderer->setTexture(texture);
    }
};

int main() {
    auto* engine = Engine::getInstance();
    
    Window::WindowConfig config;
    config.title = "Tina Engine Example";
    config.width = 1280;
    config.height = 720;
    config.vsync = true;
    config.fullscreen = false;

    if (!engine->initialize(config)) {
        TINA_ENGINE_ERROR("Failed to initialize engine");
        return -1;
    }

    // 设置目标帧率（可选，默认已经是60FPS）
    // engine->getSceneManager()->setTargetFPS(60.0f);

    // 创建并设置场景
    auto* scene = new GameScene();
    engine->getSceneManager()->pushScene(scene);

    // 运行引擎
    engine->run();

    return 0;
}