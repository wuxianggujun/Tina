//
// Created by wuxianggujun on 2025/2/24.
//

#include "tina/core/SceneManager.hpp"

#include "tina/log/Log.hpp"

namespace Tina
{
    bool SceneManager::initialize()
    {
        TINA_ENGINE_INFO("SceneManager initialize");
        return true;
    }

    void SceneManager::shutdown()
    {
        while (!m_scenes.empty())
        {
            popScene();
            TINA_ENGINE_INFO("SceneManager shutdown");
        }
    }

    void SceneManager::update(float deltaTime)
    {
        if (!m_scenes.empty())
        {
            m_scenes.back()->update(deltaTime);
        }
    }

    void SceneManager::render()
    {
        if (!m_scenes.empty())
        {
            m_scenes.back()->render();
        }
    }

    void SceneManager::pushScene(Scene* scene)
    {
        if (scene)
        {
            m_scenes.push_back(std::unique_ptr<Scene>(scene));
            scene->onEnter();
            TINA_ENGINE_INFO("Pushed new scene: {}", scene->getName());
        }
    }

    void SceneManager::popScene()
    {
        if (!m_scenes.empty())
        {
            m_scenes.back()->onExit();
            m_scenes.pop_back();
            TINA_ENGINE_INFO("Popped scene");
        }
    }

    void SceneManager::replaceScene(Scene* scene)
    {
        if (scene)
        {
            if (!m_scenes.empty())
            {
                popScene();
            }
            pushScene(scene);
        }
    }

    Scene* SceneManager::getCurrentScene() const
    {
        return m_scenes.empty() ? nullptr : m_scenes.back().get();
    }

    SceneManager::~SceneManager()
    {
        shutdown();
    }
} // Tina
