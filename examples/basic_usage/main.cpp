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

        // 确认节点创建成功
        if (!spriteNode) {
            TINA_ENGINE_ERROR("创建节点失败!");
            return;
        }

        // 记录调试信息
        TINA_ENGINE_INFO("创建节点: {}", spriteNode->getName());
        
        // 添加变换组件
        auto* transform = spriteNode->addComponent<Transform>();
        if (!transform) {
            TINA_ENGINE_ERROR("添加Transform组件失败!");
            return;
        }
        TINA_ENGINE_INFO("添加Transform组件成功!");
        // 设置精灵在屏幕左上角
        transform->setPosition({0.0f, 0.0f, 0.0f});
        // 添加精灵渲染组件
        // 添加精灵渲染组件
        auto* renderer = spriteNode->addComponent<SpriteRenderer>();
        if (!renderer) {
            TINA_ENGINE_ERROR("添加SpriteRenderer组件失败!");
            return;
        }
        TINA_ENGINE_INFO("添加SpriteRenderer组件成功!");
    
        // 确保精灵渲染器能够找到Transform组件
        if (!spriteNode->getComponent<Transform>()) {
            TINA_ENGINE_ERROR("无法获取Transform组件!");
        }
        
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
        
        // 添加输入状态日志（每5秒输出一次，避免日志过多）
        static float logTimer = 0.0f;
        logTimer += deltaTime;
        if (logTimer > 5.0f) {
            TINA_ENGINE_INFO("Input状态检查 - W键: {}, 鼠标左键: {}, 鼠标位置: ({}, {})",
                            input->isKeyPressed(KeyCode::W),
                            input->isMouseButtonPressed(MouseButton::Left),
                            input->getMousePosition().x,
                            input->getMousePosition().y);
            logTimer = 0.0f;
        }
        
        // 键盘移动控制
        if (input->isKeyPressed(KeyCode::W)) {
            m_transform->translate({0.0f, -moveSpeed, 0.0f});
            TINA_ENGINE_TRACE("按下W键 - 向上移动");
        }
        if (input->isKeyPressed(KeyCode::S)) {
            m_transform->translate({0.0f, moveSpeed, 0.0f});
            TINA_ENGINE_TRACE("按下S键 - 向下移动");
        }
        if (input->isKeyPressed(KeyCode::A)) {
            m_transform->translate({-moveSpeed, 0.0f, 0.0f});
            TINA_ENGINE_TRACE("按下A键 - 向左移动");
        }
        if (input->isKeyPressed(KeyCode::D)) {
            m_transform->translate({moveSpeed, 0.0f, 0.0f});
            TINA_ENGINE_TRACE("按下D键 - 向右移动");
        }
        
        // 添加鼠标输入测试
        if (input->isMouseButtonPressed(MouseButton::Left)) {
            // 鼠标左键点击时，将精灵移动到鼠标位置
            Math::Vec2f mousePos = input->getMousePosition();
            m_transform->setPosition({mousePos.x, mousePos.y, 0.0f});
            TINA_ENGINE_TRACE("鼠标左键点击 - 移动到位置: ({}, {})", mousePos.x, mousePos.y);
        }
        
        // 鼠标右键测试
        if (input->isMouseButtonJustPressed(MouseButton::Right)) {
            TINA_ENGINE_INFO("鼠标右键刚刚按下！当前鼠标位置: ({}, {})", 
                            input->getMousePosition().x, 
                            input->getMousePosition().y);
        }
        
        // 鼠标滚轮测试
        Math::Vec2f scrollDelta = input->getScrollDelta();
        if (scrollDelta.y != 0.0f) {
            // 使用滚轮控制精灵缩放
            float scale = m_transform->getScale().x;
            scale += scrollDelta.y * 0.1f;
            // scale = Math::clamp(scale, 0.1f, 5.0f);
            m_transform->setScale({scale, scale, 1.0f});
            TINA_ENGINE_INFO("鼠标滚轮滚动: {} - 调整缩放至: {}", scrollDelta.y, scale);
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