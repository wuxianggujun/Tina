#pragma once

#include "Layer.hpp"
#include <vector>
#include <memory>

namespace Tina {

class LayerStack {
public:
    LayerStack() = default;
    ~LayerStack();

    void pushLayer(const std::shared_ptr<Layer>& layer);
    void pushOverlay(const std::shared_ptr<Layer>& overlay);
    void popLayer(const std::shared_ptr<Layer>& layer);
    void popOverlay(const std::shared_ptr<Layer>& overlay);

    std::vector<std::shared_ptr<Layer>>::iterator begin() { return m_Layers.begin(); }
    std::vector<std::shared_ptr<Layer>>::iterator end() { return m_Layers.end(); }
    std::vector<std::shared_ptr<Layer>>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
    std::vector<std::shared_ptr<Layer>>::reverse_iterator rend() { return m_Layers.rend(); }

private:
    std::vector<std::shared_ptr<Layer>> m_Layers;
    unsigned int m_LayerInsertIndex = 0;
};

} // namespace Tina
