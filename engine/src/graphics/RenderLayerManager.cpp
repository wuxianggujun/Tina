#include "graphics/RenderLayerManager.hpp"

namespace Tina
{
    void RenderLayerManager::addToLayer(entt::entity entity, RenderLayer layer)
    {
        // 如果实体已经在某个层中，先移除
        removeFromLayer(entity);
        
        // 添加到新层
        m_layers[layer].push_back(entity);
        m_entityLayers[entity] = layer;
    }

    void RenderLayerManager::removeFromLayer(entt::entity entity)
    {
        auto it = m_entityLayers.find(entity);
        if (it != m_entityLayers.end())
        {
            RenderLayer layer = it->second;
            auto& entities = m_layers[layer];
            entities.erase(std::remove(entities.begin(), entities.end(), entity), entities.end());
            m_entityLayers.erase(it);
        }
    }

    void RenderLayerManager::changeLayer(entt::entity entity, RenderLayer newLayer)
    {
        addToLayer(entity, newLayer);
    }

    const std::vector<entt::entity>& RenderLayerManager::getEntitiesInLayer(RenderLayer layer)
    {
        return m_layers[layer];
    }

    RenderLayer RenderLayerManager::getEntityLayer(entt::entity entity)
    {
        auto it = m_entityLayers.find(entity);
        return it != m_entityLayers.end() ? it->second : RenderLayer::Default;
    }

    void RenderLayerManager::clear()
    {
        m_layers.clear();
        m_entityLayers.clear();
    }
} 