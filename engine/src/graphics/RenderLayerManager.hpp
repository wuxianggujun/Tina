#pragma once

#include <vector>
#include <entt/entt.hpp>
#include "graphics/RenderLayer.hpp"

namespace Tina
{
    class RenderLayerManager
    {
    public:
        // 添加实体到指定层
        void addToLayer(entt::entity entity, RenderLayer layer);

        // 从层中移除实体
        void removeFromLayer(entt::entity entity);

        // 更改实体的层
        void changeLayer(entt::entity entity, RenderLayer newLayer);

        // 获取指定层的所有实体
        const std::vector<entt::entity>& getEntitiesInLayer(RenderLayer layer);

        // 获取实体当前所在的层
        RenderLayer getEntityLayer(entt::entity entity);

        // 清除所有层
        void clear();

    private:
        // 每个层存储的实体列表
        std::unordered_map<RenderLayer, std::vector<entt::entity>> m_layers;
        // 实体到层的映射
        std::unordered_map<entt::entity, RenderLayer> m_entityLayers;
    };
}