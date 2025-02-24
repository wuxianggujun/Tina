//
// Created by wuxianggujun on 2025/2/24.
//

//#include "tina/core/Engine.hpp"
//#include "tina/scene/Scene.hpp"
//#include "tina/component/Transform.hpp"
//#include "tina/component/SpriteRenderer.hpp"
//
//using namespace Tina;
//
//class GameScene : public Scene {
//public:
//    GameScene() : Scene("GameScene") {}
//
//    void onEnter() override {
//        // 创建精灵节点
//        auto* spriteNode = createNode("Sprite");
//        
//        // 添加变换组件
//        auto* transform = spriteNode->addComponent<Transform>();
//        transform->setPosition({0.0f, 0.0f, 0.0f});
//        
//        // 添加精灵渲染组件
//        auto* renderer = spriteNode->addComponent<SpriteRenderer>();
//        auto texture = Engine::getInstance().getResourceManager()->loadSync<TextureResource>("test.png", "resources/textures/");
//        renderer->setTexture(texture);
//    }
//};
//
//int main() {
//    auto& engine = Engine::getInstance();
//    
//    if (!engine.initialize()) {
//        return -1;
//    }
//
//    // 创建并设置场景
//    auto* scene = new GameScene();
//    engine.getSceneManager()->pushScene(scene);
//
//    engine.run();
//    engine.shutdown();
//
//    return 0;
//}