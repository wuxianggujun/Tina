#pragma once

#include "Layer.hpp"
#include <vector>
#include <memory>
#include "tina/log/Logger.hpp"

namespace Tina {

class LayerStack {
public:
    LayerStack() = default;
    ~LayerStack();

    // 禁止拷贝构造和赋值
    LayerStack(const LayerStack&) = delete;
    LayerStack& operator=(const LayerStack&) = delete;

    void pushLayer(const std::shared_ptr<Layer>& layer);
    void pushOverlay(const std::shared_ptr<Layer>& overlay);
    void popLayer(const std::shared_ptr<Layer>& layer);
    void popOverlay(const std::shared_ptr<Layer>& overlay);

    std::vector<std::shared_ptr<Layer>>::iterator begin() { return m_Layers.begin(); }
    std::vector<std::shared_ptr<Layer>>::iterator end() { return m_Layers.end(); }
    std::vector<std::shared_ptr<Layer>>::reverse_iterator rbegin() { return m_Layers.rbegin(); }
    std::vector<std::shared_ptr<Layer>>::reverse_iterator rend() { return m_Layers.rend(); }

    // 添加const迭代器
    std::vector<std::shared_ptr<Layer>>::const_iterator begin() const { return m_Layers.begin(); }
    std::vector<std::shared_ptr<Layer>>::const_iterator end() const { return m_Layers.end(); }
    std::vector<std::shared_ptr<Layer>>::const_reverse_iterator rbegin() const { return m_Layers.rbegin(); }
    std::vector<std::shared_ptr<Layer>>::const_reverse_iterator rend() const { return m_Layers.rend(); }

    // 添加一些辅助方法
    bool empty() const { return m_Layers.empty(); }
    size_t size() const { return m_Layers.size(); }
    void clear();

private:
    std::vector<std::shared_ptr<Layer>> m_Layers;
    unsigned int m_LayerInsertIndex = 0;
};

} // namespace Tina
