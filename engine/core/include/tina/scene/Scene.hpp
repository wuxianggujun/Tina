#pragma once

#include "tina/core/Core.hpp"
#include <entt/entt.hpp>
#include <string>
#include "tina/view/GameView.hpp"

namespace Tina
{
    class GameView;

    class TINA_CORE_API Scene
    {
    public:
        explicit Scene(std::string name = "Scene");
        ~Scene();

        // 禁止拷贝和赋值
        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        // Scene lifecycle
        void onUpdate(float deltaTime);
        // Getters
        [[nodiscard]] const std::string& getName() const { return m_name; }
        void setName(const std::string& name) { m_name = name; }

        void addView(View* view);

        void removeView(View* view);

        void onEvent(Event& event);

        void onRender();

    private:
        void renderView(View* view);

        std::string m_name;
        View* m_rootView;
    };
} // namespace Tina
