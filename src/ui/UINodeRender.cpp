#include "UINode.hpp"
#include "UICore.hpp"

namespace Tina::UI {

void UINode::render(uint16_t viewId, UIRenderer& renderer)
{
    if (!m_visible) return;

    const int previousLayer = renderer.currentLayer();
    const int targetLayer = m_layer != 0 ? m_layer : previousLayer;
    const bool changedLayer = targetLayer != previousLayer;
    if (changedLayer) renderer.pushLayer(targetLayer);

    onRender(viewId, renderer);
    for (auto& child : m_children) {
        if (child) child->render(viewId, renderer);
    }

    if (changedLayer) renderer.popLayer();
}

} // namespace Tina::UI
