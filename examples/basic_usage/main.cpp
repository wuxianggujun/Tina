#include <tina/core/Engine.hpp>
#include <tina/scene/Scene.hpp>
#include <tina/view/GameView.hpp>
#include <tina/view/UIView.hpp>
#include <tina/components/Transform2DComponent.hpp>
#include <tina/components/SpriteComponent.hpp>
#include <tina/components/RectangleComponent.hpp>
#include <tina/log/Logger.hpp>
#include <tina/renderer/Color.hpp>
#include <random>

using namespace Tina;

// 创建测试实体
void createTestEntities(Scene* scene) {
    auto& registry = scene->getRegistry();
    
    // 创建5个精灵实体
    for (int i = 0; i < 5; i++) {
        auto entity = registry.create();
        
        // 添加Transform组件
        auto& transform = registry.emplace<Transform2DComponent>(entity);
        transform.setPosition({i * 150.0f, 50.0f});
        transform.setScale({0.2f, 0.2f});
        
        // 添加Sprite组件
        auto& sprite = registry.emplace<SpriteComponent>(entity);
        sprite.setTexture(Core::Engine::get().getTextureManager().getTexture("test" + std::to_string(i)));
        sprite.setLayer(0);
        sprite.setVisible(true);
        
        TINA_LOG_DEBUG("Created sprite entity {} at position ({}, {})", 
            static_cast<uint32_t>(entity), 
            transform.getPosition().x, 
            transform.getPosition().y);
    }
    
    // 创建95个彩色矩形实体
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
    
    int rows = 10;
    int cols = 10;
    float rectWidth = 50.0f;
    float rectHeight = 50.0f;
    float spacing = 10.0f;
    float startX = 50.0f;
    float startY = 200.0f;
    
    for (int i = 0; i < 95; i++) {
        int row = i / cols;
        int col = i % cols;
        
        auto entity = registry.create();
        
        // 添加Transform组件
        auto& transform = registry.emplace<Transform2DComponent>(entity);
        transform.setPosition({
            startX + col * (rectWidth + spacing),
            startY + row * (rectHeight + spacing)
        });
        
        // 添加Rectangle组件
        auto& rect = registry.emplace<RectangleComponent>(entity);
        rect.setSize({rectWidth, rectHeight});
        rect.setColor(Color(
            colorDist(gen),
            colorDist(gen),
            colorDist(gen),
            1.0f
        ));
        rect.setLayer(1);
        rect.setVisible(true);
        
        TINA_LOG_DEBUG("Created rectangle entity {} at position ({}, {})", 
            static_cast<uint32_t>(entity), 
            transform.getPosition().x, 
            transform.getPosition().y);
    }
    
    TINA_LOG_INFO("Created 5 sprites and 95 rectangles");
}

// 创建UI十字架
void createUICross(UIView* uiView) {
    uint32_t width, height;
    Core::Engine::get().getWindowSize(width, height);
    
    // 设置线条样式 - 增加粗细并使用更鲜艳的颜色
    LineStyle style;
    style.thickness = 5.0f;  // 增加线条粗细
    style.color = {1.0f, 0.0f, 0.0f, 1.0f}; // 保持鲜艳的红色
    
    TINA_LOG_INFO("Drawing UI cross at window size {}x{}", width, height);
    
    // 计算中心点
    float centerX = width * 0.5f;
    float centerY = height * 0.5f;
    
    // 绘制垂直线(连接屏幕顶部和底部)
    uiView->drawLine(
        {centerX, 0.0f},             // 顶部中点
        {centerX, (float)height},    // 底部中点
        style
    );
    
    // 绘制水平线(连接屏幕左边和右边)
    uiView->drawLine(
        {0.0f, centerY},           // 左边中点
        {(float)width, centerY},   // 右边中点
        style
    );
    
    TINA_LOG_INFO("Created UI cross at center ({}, {})", centerX, centerY);
}

int main() {
    // 初始化日志系统
    auto& logger = Tina::Logger::getInstance();
    logger.init("app.log", true, true);
    logger.setLogLevel(Tina::Logger::Level::Debug);
    
    try {
        // 创建引擎实例
        UniquePtr<Core::Engine> engine = MakeUnique<Core::Engine>();
        
        // 初始化引擎
        if (!engine->initialize()) {
            TINA_LOG_ERROR("Failed to initialize engine");
            return -1;
        }
        
        // 创建场景
        Scene* scene = engine->createScene("Main Scene");
        if (!scene) {
            TINA_LOG_ERROR("Failed to create scene");
            return -1;
        }
        
        // 创建游戏视图
        auto gameView = new GameView();
        gameView->setZOrder(0);  // 设置较低的zOrder
        scene->addView(gameView);
        
        // 创建UI视图
        auto uiView = new UIView("UIView");
        // UIView构造函数中已经设置了zOrder为100
        scene->addView(uiView);
        
        // 预加载纹理
        for (int i = 0; i < 5; i++) {
            auto textureName = "test" + std::to_string(i);
            if (!gameView->loadTexture(textureName, "textures/test.png")) {
                TINA_LOG_WARN("Failed to load texture: {}", textureName);
            }
        }
        
        // 创建测试实体
        createTestEntities(scene);
        
        // 创建UI十字架
        createUICross(uiView);
        
        TINA_LOG_INFO("Views initialized - GameView(zOrder: {}), UIView(zOrder: {})",
            gameView->getZOrder(), uiView->getZOrder());
        
        // 运行引擎
        if (!engine->run()) {
            TINA_LOG_ERROR("Engine run failed");
            return -1;
        }
        
        return 0;
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Application error: {}", e.what());
        return 1;
    }
}
