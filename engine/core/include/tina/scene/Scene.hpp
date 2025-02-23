// #pragma once
//
// #include "tina/core/Core.hpp"
// #include <entt/entt.hpp>
// #include <string>
// #include "tina/view/GameView.hpp"
//
// namespace Tina
// {
//     class GameView;
//
//     class TINA_CORE_API Scene
//     {
//     public:
//         explicit Scene(std::string name = "Scene");
//         ~Scene();
//
//         // 禁止拷贝和赋值
//         Scene(const Scene&) = delete;
//         Scene& operator=(const Scene&) = delete;
//
//         // Scene lifecycle
//         void onUpdate(float deltaTime);
//         
//         // Getters & Setters
//         [[nodiscard]] const std::string& getName() const { return m_name; }
//         void setName(const std::string& name) { m_name = name; }
//
//         // View management
//         void addView(View* view);
//         void removeView(View* view);
//         void onEvent(Event& event);
//         void onRender();
//
//         // ECS Registry access
//         [[nodiscard]] entt::registry& getRegistry() { return m_registry; }
//         [[nodiscard]] const entt::registry& getRegistry() const { return m_registry; }
//
//     private:
//         // View lifecycle management
//         void initializeView(View* view);
//         void cleanupView(View* view);
//         void updateViewHierarchy(View* view, float deltaTime);
//         void renderViewHierarchy(View* view);
//         void propagateEventToView(View* view, Event& event);
//
//         std::string m_name;
//         View* m_rootView;
//         
//         // ECS registry
//         entt::registry m_registry;
//     };
// } // namespace Tina
