#pragma once

#include <string>
#include "tina/core/Core.hpp"

namespace Tina
{
    class Scene;
    class Event;

    class TINA_CORE_API Layer
    {
    public:
        explicit Layer(std::string  name = "Layer");
        virtual ~Layer() = default;

        virtual void onAttach()
        {
        } // 当Layer被添加到Scene时调用
        virtual void onDetach()
        {
        } // 当Layer从Scene中移除时调用
        virtual void onUpdate(float deltaTime)
        {
        } // 更新逻辑
        virtual void onRender()
        {
        } // 渲染逻辑
        virtual void onImGuiRender()
        {
        } // ImGui渲染
        virtual void onEvent(Event& event)
        {
        } // 事件处理

        [[nodiscard]] const std::string& getName() const { return m_debugName; }
        
        void setScene(Scene* scene) { m_scene = scene; }
        [[nodiscard]] Scene* getScene() const { return m_scene; }

    protected:
        std::string m_debugName;
        Scene* m_scene = nullptr;
    };
} // namespace Tina
