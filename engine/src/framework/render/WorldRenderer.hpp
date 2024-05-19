//
// Created by wuxianggujun on 2024/5/19.
//

#ifndef TINA_RENDER_WORLDRENDERER_HPP
#define TINA_RENDER_WORLDRENDERER_HPP

#include "Renderer.hpp"

namespace Tina {

    class WorldRenderer : public Renderer{
    public:
        void initialize() override;

        void update(const float *viewMatrix, const float *projectionMatrix) override;

        void render(float deltaTime) override;

        [[nodiscard]] bool isEnable() const override;

    };

} // Tina

#endif //TINA_RENDER_WORLDRENDERER_HPP
