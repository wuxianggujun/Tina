#include "tina/layer/LayerStack.hpp"

namespace Tina {

LayerStack::~LayerStack() {
    for (const auto& layer : m_Layers) {
        layer->onDetach();
    }
}

void LayerStack::pushLayer(const std::shared_ptr<Layer>& layer) {
    m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
    m_LayerInsertIndex++;
    layer->onAttach();
}

void LayerStack::pushOverlay(const std::shared_ptr<Layer>& overlay) {
    m_Layers.emplace_back(overlay);
    overlay->onAttach();
}

void LayerStack::popLayer(const std::shared_ptr<Layer>& layer) {
    auto it = std::find(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex, layer);
    if (it != m_Layers.begin() + m_LayerInsertIndex) {
        layer->onDetach();
        m_Layers.erase(it);
        m_LayerInsertIndex--;
    }
}

void LayerStack::popOverlay(const std::shared_ptr<Layer>& overlay) {
    auto it = std::find(m_Layers.begin() + m_LayerInsertIndex, m_Layers.end(), overlay);
    if (it != m_Layers.end()) {
        overlay->onDetach();
        m_Layers.erase(it);
    }
}

} // namespace Tina
