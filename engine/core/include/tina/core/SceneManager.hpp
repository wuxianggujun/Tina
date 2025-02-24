//
// Created by wuxianggujun on 2025/2/24.
//

#pragma once

#include "tina/core/Core.hpp"
#include "tina/core/Singleton.hpp"
#include <vector>

namespace Tina
{
    class Scene;

    class TINA_CORE_API SceneManager : public Singleton<SceneManager>
    {
        friend class Singleton<SceneManager>;

    public:
        bool initialize();
        void shutdown();

        void update(float deltaTime);
        void render();

        void pushScene(Scene* scene);
        void popScene();
        void replaceScene(Scene* scene);

        Scene* getCurrentScene() const;

    private:
        SceneManager() = default;
        ~SceneManager() override;

        std::vector<UniquePtr<Scene>> m_scenes;
    };
} // Tina
