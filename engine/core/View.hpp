#ifndef TINA_VIEW_HPP
#define TINA_VIEW_HPP

#include <iostream>
#include <bgfx/bgfx.h>
#include <vector>

namespace Tina {

    class Layer;

    class View {
    public:
        View();
        void addLayer(Layer layer);
        void submit();

        static bgfx::ViewId nextViewId;
    private:
        bgfx::ViewId view;
        std::vector<Layer> layers;
    };

}


#endif
