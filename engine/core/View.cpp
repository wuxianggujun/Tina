#include "View.hpp"

namespace Tina {

    bgfx::ViewId View::nextViewId = 0;

    View::View() {
        view = nextViewId;
        nextViewId++;
    }

    void View::addLayer(Layer layer) {
        layers.push_back(layer);
    }

    void View::submit() {
        bgfx::setViewRect(view, 0, 0, bgfx::BackbufferRatio::Equal);
        bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0xdddddddd, 1.0f);

        bgfx::touch(view);

        for (int i = 0; i < layers.size(); i++) {
            bgfx::setState(
                BGFX_STATE_WRITE_R
                | BGFX_STATE_WRITE_G
                | BGFX_STATE_WRITE_B
                | BGFX_STATE_WRITE_A
            );

            layers.at(i).draw(view);
        }
    }

}
