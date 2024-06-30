//
// Created by wuxianggujun on 2024/6/24.
//

#ifndef TINA_VIEW_HPP
#define TINA_VIEW_HPP

#include <iostream>
#include <vector>
#include <bgfx/bgfx.h>
#include "Layer.hpp"

namespace Tina {

    class View {
    public:
        View();

        void addLayer(Layer &layer);

        void submit();

        static bgfx::ViewId nextViewId;

    private:
        bgfx::ViewId viewId;
        std::vector<Layer> layers;
    };

} // Tina

#endif //TINA_VIEW_HPP
