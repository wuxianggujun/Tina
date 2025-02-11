#pragma once

#include <string>

namespace Tina
{
    class Event;

    class Layer
    {
    public:
        explicit Layer(const std::string& name = "Layer");
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

        [[nodiscard]] const std::string& getName() const { return m_Name; }

    protected:
        std::string m_Name;
        bool m_Enabled = true;
    };
} // namespace Tina
