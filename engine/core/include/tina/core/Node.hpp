//
// Created by wuxianggujun on 2025/2/16.
//
#pragma once
#include <vector>
#include <string>
#include "tina/core/Core.hpp"
#include "tina/event/Event.hpp"
#include "tina/math/Rect.hpp"
#include <glm/glm.hpp>

namespace Tina
{
    class TINA_CORE_API Node
    {
    public:
        virtual ~Node() = default;

        Node* getParent() const
        {
            return parent;
        }

        void setParent(Node* parent)
        {
            this->parent = parent;
        }

        bool isVisible() const
        {
            return visible;
        }

        void setVisible(const bool visible)
        {
            this->visible = visible;
        }

        int32_t getZOrder() const
        {
            return zOrder;
        }

        void setZOrder(const int32_t zOrder)
        {
            this->zOrder = zOrder;
        }

        void addChild(Node* child)
        {
            if (child->parent)
            {
                child->parent->removeChild(child);
            }
            child->parent = this;
            children.push_back(child);
        }

        void removeChild(Node* child)
        {
            const auto it = std::find(children.begin(), children.end(), child);
            if (it != children.end())
            {
                (*it)->parent = nullptr;
                children.erase(it);
            }
        }

        std::vector<Node*> getChildren() const
        {
            return children;
        }

        virtual bool onEvent(Event& event)
        {
            bool handled = false;

            // 先处理自己的事件
            switch (event.type)
            {
            case Event::MouseMove:
                handled = handleMouseMove(event);
                break;
            case Event::MouseButton:
                handled = handleMouseButton(event);
                break;
            case Event::WindowResize:
                handled = handleWindowResize(event);
                break;
            default:
                break;
            }

            // 如果自己没处理，再传递给子节点
            if (!handled && !children.empty())
            {
                // 正向遍历子节点（从前到后）
                for (auto* child : children)
                {
                    if (child && child->onEvent(event))
                    {
                        handled = true;
                        break;
                    }
                }
            }

            return handled;
        }

        // 生命周期
        virtual void onAttach() {}
        virtual void onDetach() {}
        virtual void onUpdate(const float deltaTime) {}

        // 坐标转换
        glm::vec2 globalToLocal(const glm::vec2& point) const
        {
            if (parent)
            {
                return parent->globalToLocal(point - position);
            }
            return point - position;
        }

        glm::vec2 localToGlobal(const glm::vec2& point) const
        {
            if (parent)
            {
                return parent->localToGlobal(point + position);
            }
            return point + position;
        }

    protected:
        Node* parent = nullptr;
        std::vector<Node*> children;
        bool visible = true;
        int32_t zOrder = 0;
        std::string name;

        // 节点属性
        glm::vec2 position{0.0f};
        glm::vec2 size{0.0f};
        Rect bounds{0.0f, 0.0f, 0.0f, 0.0f};

        // 具体事件处理方法
        virtual bool handleMouseMove(Event& event)
        {
            // 检查鼠标是否在边界内
            glm::vec2 localPos = globalToLocal({event.mousePos.x, event.mousePos.y});
            return bounds.contains(localPos);
        }

        virtual bool handleMouseButton(Event& event)
        {
            glm::vec2 localPos = globalToLocal({event.mousePos.x, event.mousePos.y});
            return bounds.contains(localPos);
        }

        virtual bool handleWindowResize(Event& event)
        {
            // 默认实现：更新边界
            bounds.setWidth(static_cast<float>(event.windowResize.width));
            bounds.setHegiht(static_cast<float>(event.windowResize.height));
            return false;
        }
    };
}
