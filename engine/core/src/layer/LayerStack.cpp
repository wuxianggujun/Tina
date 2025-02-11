#include "tina/layer/LayerStack.hpp"

namespace Tina {

LayerStack::~LayerStack() {
    try {
        // 直接清理所有层
        for (auto& layer : m_Layers) {
            if (layer) {
                try {
                    TINA_LOG_DEBUG("Detaching layer: {}", layer->getName());
                    layer->onDetach();
                }
                catch (const std::exception& e) {
                    TINA_LOG_ERROR("Error detaching layer: {}", e.what());
                }
            }
        }
        m_Layers.clear();
        m_LayerInsertIndex = 0;
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Error in LayerStack destructor: {}", e.what());
    }
    catch (...) {
        TINA_LOG_ERROR("Unknown error in LayerStack destructor");
    }
}

void LayerStack::pushLayer(const std::shared_ptr<Layer>& layer) {
    if (!layer) {
        TINA_LOG_WARN("Attempting to push null layer");
        return;
    }
    try {
        m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
        m_LayerInsertIndex++;
        layer->onAttach();
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Error pushing layer: {}", e.what());
    }
}

void LayerStack::pushOverlay(const std::shared_ptr<Layer>& overlay) {
    if (!overlay) {
        TINA_LOG_WARN("Attempting to push null overlay");
        return;
    }
    try {
        m_Layers.emplace_back(overlay);
        overlay->onAttach();
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Error pushing overlay: {}", e.what());
    }
}

void LayerStack::popLayer(const std::shared_ptr<Layer>& layer) {
    if (!layer) {
        TINA_LOG_WARN("Attempting to pop null layer");
        return;
    }
    try {
        auto it = std::find(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex, layer);
        if (it != m_Layers.begin() + m_LayerInsertIndex) {
            m_Layers.erase(it);
            m_LayerInsertIndex--;
        }
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Error popping layer: {}", e.what());
    }
}

void LayerStack::popOverlay(const std::shared_ptr<Layer>& overlay) {
    if (!overlay) {
        TINA_LOG_WARN("Attempting to pop null overlay");
        return;
    }
    try {
        auto it = std::find(m_Layers.begin() + m_LayerInsertIndex, m_Layers.end(), overlay);
        if (it != m_Layers.end()) {
            m_Layers.erase(it);
        }
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Error popping overlay: {}", e.what());
    }
}

void LayerStack::clear() {
    try {
        m_Layers.clear();
        m_LayerInsertIndex = 0;
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Error clearing layer stack: {}", e.what());
    }
}

} // namespace Tina
