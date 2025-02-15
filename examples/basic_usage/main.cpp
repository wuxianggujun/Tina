#include <tina/core/Engine.hpp>
#include <tina/layer/Render2DLayer.hpp>
#include <tina/layer/GameLayer.hpp>
#include <glm/glm.hpp>
#include "tina/scene/Scene.hpp"
#include "tina/log/Logger.hpp"
#include "tina/renderer/Color.hpp"
#include <random>

using namespace Tina;

// 自定义游戏层
class MyGameLayer : public GameLayer
{
public:
    MyGameLayer(Scene& scene) : GameLayer(scene)
    {
    }

    void onAttach() override
    {
        // 创建5个test纹理精灵
        for(int i = 0; i < 5; i++) {
            auto& sprite = createSprite("test" + std::to_string(i), "resources/textures/test.png");
            sprite.setLayer(0);
            // 设置位置 - 横向排列
            sprite.setPosition({i * 150.0f, 50.0f});
            sprite.setScale({0.5f, 0.5f}); // 缩小一点以便显示更多
        }

        // 创建95个纯色矩形
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

        for(int i = 0; i < 95; i++) {
            int row = i / cols;
            int col = i % cols;
            
            // 生成随机颜色
            Color randomColor(
                colorDist(gen),
                colorDist(gen),
                colorDist(gen),
                1.0f
            );

            // 计算位置
            float x = startX + col * (rectWidth + spacing);
            float y = startY + row * (rectHeight + spacing);

            // 创建矩形
            auto& rect = createRectangle({x, y}, {rectWidth, rectHeight}, randomColor);
            rect.setLayer(1); // 设置在精灵后面
        }

        TINA_LOG_INFO("Created 5 sprites and 95 rectangles");
    }

    void onUpdate(float deltaTime) override
    {
        // 更新玩家位置
        if (auto* transform = m_scene.getRegistry().try_get<Transform2DComponent>(m_playerEntity))
        {
            auto pos = transform->getPosition();
            // 简单的移动逻辑
            pos.x += m_velocity.x * deltaTime;
            pos.y += m_velocity.y * deltaTime;
            transform->setPosition(pos);
        }
    }

    void onEvent(Event& event) override
    {
        // 处理输入事件来更新速度
        // TODO: 添加输入处理
    }

private:
    entt::entity m_playerEntity;
    glm::vec2 m_velocity{50.0f, 0.0f}; // 默认向右移动
};

int main()
{
    // 初始化日志系统
    auto& logger = Tina::Logger::getInstance();
    logger.init("app.log", true, true); // 文件名，是否清空文件，是否输出到控制台

    // 设置日志级别
    logger.setLogLevel(Tina::Logger::Level::Debug);

    try
    {
        // 创建引擎实例
        UniquePtr<Core::Engine> engine = MakeUnique<Core::Engine>();

        // 初始化引擎
        if (!engine->initialize())
        {
            TINA_LOG_ERROR("Failed to initialize engine");
            return -1;
        }

        // 创建场景
        Scene* scene = engine->createScene("Main Scene");
        if (!scene)
        {
            TINA_LOG_ERROR("Failed to create scene");
            return -1;
        }

        // 添加游戏层
        auto gameLayer = new MyGameLayer(*scene);
        scene->pushLayer(gameLayer);

        // 运行引擎
        if (!engine->run())
        {
            TINA_LOG_ERROR("Engine run failed");
            return -1;
        }

        // 引擎将在这里自动销毁
        return 0;
    }
    catch (const std::exception& e)
    {
        TINA_LOG_ERROR("Application error: {}", e.what());
        return 1;
    }
}
