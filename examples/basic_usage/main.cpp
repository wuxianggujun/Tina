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
        // transform->setPosition({400.0f, 300.0f, 0.0f}); // 设置到窗口中心位置
        // 设置精灵在屏幕左上角
        transform->setPosition({0.0f, 0.0f, 0.0f});
        // 添加精灵渲染组件
        auto* renderer = spriteNode->addComponent<SpriteRenderer>();
        auto texture = Engine::getInstance()->getResourceManager()->loadSync<TextureResource>("test.png", "resources/textures/");
        
        // 输出纹理信息
        if (texture && texture->isLoaded()) {
            TINA_ENGINE_INFO("Texture loaded - Width: {}, Height: {}", 
                texture->getWidth(), texture->getHeight());
        } else {
            TINA_ENGINE_ERROR("Failed to load texture!");
        }
        
        renderer->setTexture(texture);
        
        // 保存组件引用以便在事件处理中使用
        m_spriteRenderer = renderer;
        m_transform = transform;
    }

    void onUpdate(float deltaTime) override {
        // 处理键盘输入
        const float moveSpeed = 200.0f * deltaTime;
        auto* input = Engine::getInstance()->getInputManager();
        
        if (input->isKeyPressed(KeyCode::W)) {
            m_transform->translate({0.0f, -moveSpeed, 0.0f});
        }
        if (input->isKeyPressed(KeyCode::S)) {
            m_transform->translate({0.0f, moveSpeed, 0.0f});
        }
        if (input->isKeyPressed(KeyCode::A)) {
            m_transform->translate({-moveSpeed, 0.0f, 0.0f});
        }
        if (input->isKeyPressed(KeyCode::D)) {
            m_transform->translate({moveSpeed, 0.0f, 0.0f});
        }
    }

private:
    SpriteRenderer* m_spriteRenderer{nullptr};
    Transform* m_transform{nullptr};
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