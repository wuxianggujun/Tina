#include <tina/core/Engine.hpp>
#include <tina/layer/Render2DLayer.hpp>
#include <tina/layer/GameLayer.hpp>
#include <glm/glm.hpp>
#include "tina/scene/Scene.hpp"
#include "tina/log/Logger.hpp"

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
        // 创建背景精灵(层级0)
        auto& background = createSprite("test", "resources/textures/test.png");
        background.setLayer(0);
        background.setScale({2.0f, 2.0f});
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
